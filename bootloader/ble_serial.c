#include "ble_serial.h"
#include <string.h>
#ifdef KLQ_BLE_TEST
extern volatile uint32_t board_ms;
extern uint8_t ble_test_connected(void),ble_test_tx_ready(void),ble_test_tx_complete(void);
extern void ble_test_tx(uint8_t byte);
extern uint32_t ble_test_lock(void);
extern void ble_test_unlock(uint32_t mask);
#define CONNECTED() ble_test_connected()
#define TX_READY() ble_test_tx_ready()
#define TX_COMPLETE() ble_test_tx_complete()
#define PUT_BYTE(x) ble_test_tx(x)
#define LOCK() ble_test_lock()
#define UNLOCK(x) ble_test_unlock(x)
#else
#include "board.h"
#include "apm32e10x_usart.h"
#define CONNECTED() ((GPIOA->IDATA&GPIO_PIN_8)!=0u)
#define TX_READY() (UART5->STS_B.TXBEFLG!=0u)
#define TX_COMPLETE() (UART5->STS_B.TXCFLG!=0u)
#define PUT_BYTE(x) (UART5->DATA=(x))
static uint32_t lock(void) { uint32_t p=__get_PRIMASK();__disable_irq();return p; }
#define LOCK() lock()
#define UNLOCK(x) __set_PRIMASK(x)
#endif
#define RX_N 1024u
#define AT_MS 500u
#define TX_MS 8000u
#define BATCH_N 20u
#define BATCH_MS 20u
typedef struct { const char *query,*prefix,*value,*set; } Config;
static const Config config[]={
    {"AT\r\n",0,0,0},
    {"AT+ECHO?\r\n","+ECHO:","0","AT+ECHO=0\r\n"},
    {"AT+ROLEMODE?\r\n","+ROLEMODE:","2","AT+ROLE=2\r\n"},
    {"AT+ROLE?\r\n","+ROLE:","Peripheral",0},
    {"AT+NAME?\r\n","+NAME:","KLQ2","AT+NAME=KLQ2\r\n"},
    {"AT+MODE?\r\n","+MODE:","2","AT+MODE=2\r\n"},
    {"AT+SLEEP?\r\n","+SLEEP:","0","AT+SLEEP=0\r\n"},
    {"AT+CONNOTIFY?\r\n","+CONNOTIFY:","0","AT+CONNOTIFY=0\r\n"},
    {"AT+BTAT?\r\n","+BTAT:","0","AT+BTAT=0\r\n"},
    {"AT+SUUID?\r\n","+SUUID:","FFF0","AT+SUUID=FFF0\r\n"},
    {"AT+RUUID?\r\n","+RUUID:","FFF1","AT+RUUID=FFF1\r\n"},
    {"AT+WUUID?\r\n","+WUUID:","FFF2","AT+WUUID=FFF2\r\n"},
    {"AT+MAC?\r\n","+MAC:",0,0},
    {"AT+VER?\r\n","+VER:",0,0}
};
static volatile uint8_t rx[RX_N], reset_seen;
static volatile uint16_t rh,rt;
static volatile uint32_t epoch,rx_bytes,uart_errors,rx_overflows;
static uint8_t ready,connected,configured,stage,error,step,attempts;
static uint32_t tx_bytes,tx_timeouts,init_timeouts,config_writes;
static const uint8_t *tx_data;
static uint32_t tx_left,tx_started,tx_at,tx_epoch;
static uint8_t tx_state,batch;
static uint16_t write_mask;
static uint8_t at_mode,at_pos,at_ok,at_got,at_match,at_bad,line_n,line_drop,resets,changed;
static uint32_t at_deadline;
static char line[64],name[21],version[16],mac[13];

static void receive(uint8_t byte,uint8_t bad)
{
    uint16_t next=(uint16_t)((rh+1u)&(RX_N-1u));++rx_bytes;
    if (bad || next==rt) {
        if(bad)++uart_errors;else ++rx_overflows;
        rt=rh=0u;++epoch;reset_seen=1u;return;
    }
    rx[rh]=byte;rh=next;
}
#ifdef KLQ_BLE_TEST
void ble_serial_test_receive(uint8_t byte,uint8_t bad) { receive(byte,bad); }
#else
void UART5_IRQHandler(void)
{
    uint32_t s=UART5->STS;uint8_t b;
    if(!(s&0x2fu))return;b=(uint8_t)UART5->DATA;
    if(s&0x0fu)receive(b,1u);else if(s&USART_FLAG_RXBNE)receive(b,0u);
}
#endif
static int pull(void)
{
    int b=-1;uint32_t k=LOCK();
    if(rt!=rh){b=rx[rt];rt=(uint16_t)((rt+1u)&(RX_N-1u));}
    UNLOCK(k);return b;
}
static void session(void)
{
    uint32_t k=LOCK();rt=rh=0u;++epoch;reset_seen=1u;UNLOCK(k);
    if(tx_state==BLE_TX_BUSY)tx_state=BLE_TX_FAILED;
}
static uint8_t equal(const char *a,const char *b)
{
    uint8_t x,y;
    do{x=(uint8_t)*a++;y=(uint8_t)*b++;if(x>='a'&&x<='z')x-=32u;if(y>='a'&&y<='z')y-=32u;if(x!=y)return 0u;}while(x);
    return 1u;
}
static void copy(char *to,unsigned max,const char *from)
{
    unsigned n=0u;while(n+1u<max&&from[n]){to[n]=from[n];++n;}while(n<max)to[n++]=0;
}
static const char *command(void)
{ return at_mode==2u?"AT+RST\r\n":at_mode?config[step].set:config[step].query; }
static void begin(void)
{
    at_pos=at_ok=at_got=at_match=at_bad=line_n=line_drop=0u;
    at_deadline=board_ms+AT_MS;stage=1u;
}
static void retry(uint8_t why)
{
    error=why;if(why==1u)++init_timeouts;
    if(at_mode==2u){at_mode=0u;step=0u;stage=0u;at_deadline=board_ms+200u;return;}
    /* Re-query a setting after a lost ACK; never blindly repeat a Flash write. */
    at_mode=0u;
    if(attempts<2u){++attempts;stage=0u;at_deadline=board_ms+100u;}
    else {attempts=0u;step=resets=changed=0u;stage=5u;at_deadline=board_ms+1000u;}
}
static void result(void)
{
    if(at_mode==2u){at_mode=0u;step=0u;changed=0u;attempts=0u;stage=0u;at_deadline=board_ms+200u;return;}
    if(at_mode==1u){++config_writes;at_mode=0u;goto next;}
    if(config[step].value&&!at_match){
        if(config[step].set){
            uint16_t bit=(uint16_t)(1u<<step);
            if(!(write_mask&bit)){write_mask|=bit;changed=1u;at_mode=1u;begin();return;}
            if(changed&&resets<2u){++resets;at_mode=2u;begin();return;}
            retry(3u);return;
        }
        if(step==3u&&!resets){++resets;at_mode=2u;begin();return;}
        retry(3u);return;
    }
next:
    attempts=0u;++step;
    if(step<sizeof(config)/sizeof(config[0])){begin();return;}
    if(changed&&resets<2u){++resets;at_mode=2u;begin();return;}
    ready=configured=1u;stage=3u;error=0u;
}
static void at_line(void)
{
    const char *prefix,*value;unsigned n,i;
    if(!strcmp(line,"ERROR")){at_bad=1u;return;}
    if(!strcmp(line,"OK")){at_ok=1u;return;}
    if(at_mode||!(prefix=config[step].prefix))return;
    n=strlen(prefix);if(strncmp(line,prefix,n))return;value=line+n;
    if(step==12u){
        if(strlen(value)!=12u)return;
        for(i=0u;i<12u;i++)if(!((value[i]>='0'&&value[i]<='9')||(value[i]>='a'&&value[i]<='f')||(value[i]>='A'&&value[i]<='F')))return;
        copy(mac,sizeof(mac),value);
    }else if(step==13u){if(!*value)return;copy(version,sizeof(version),value);}
    else if(step==4u)copy(name,sizeof(name),value);
    at_got=1u;at_match=(uint8_t)(!config[step].value||(step==4u?!strcmp(value,config[step].value):equal(value,config[step].value)));
}
void ble_serial_init(void)
{
#ifndef KLQ_BLE_TEST
    GPIO_Config_T io;USART_Config_T u;
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_GPIOA|RCM_APB2_PERIPH_GPIOC|RCM_APB2_PERIPH_GPIOD);
    RCM_EnableAPB1PeriphClock(RCM_APB1_PERIPH_UART5);
    USART_Disable(UART5);
    io.pin=GPIO_PIN_12;io.mode=GPIO_MODE_AF_PP;io.speed=GPIO_SPEED_50MHz;GPIO_Config(GPIOC,&io);
    io.pin=GPIO_PIN_2;io.mode=GPIO_MODE_IN_FLOATING;GPIO_Config(GPIOD,&io);
    io.pin=GPIO_PIN_8;GPIO_Config(GPIOA,&io);
    USART_ConfigStructInit(&u);u.baudRate=115200u;u.mode=USART_MODE_TX_RX;USART_Config(UART5,&u);
    USART_Enable(UART5);USART_EnableInterrupt(UART5,USART_INT_RXBNE);USART_EnableInterrupt(UART5,USART_INT_ERR);USART_EnableInterrupt(UART5,USART_INT_PE);
    NVIC_SetPriority(UART5_IRQn,1u);NVIC_ClearPendingIRQ(UART5_IRQn);NVIC_EnableIRQ(UART5_IRQn);
#endif
    rh=rt=0u;epoch=1u;reset_seen=1u;rx_bytes=uart_errors=rx_overflows=0u;
    tx_bytes=tx_timeouts=init_timeouts=config_writes=0u;tx_state=BLE_TX_IDLE;
    ready=connected=configured=stage=error=step=attempts=0u;resets=changed=at_mode=0u;write_mask=0u;
    memset(name,0,sizeof(name));memset(version,0,sizeof(version));memset(mac,0,sizeof(mac));
    at_deadline=board_ms+100u;
    if(CONNECTED()){connected=ready=1u;stage=4u;}
}
void ble_serial_poll(void)
{
    uint8_t on=(uint8_t)CONNECTED(),budget=64u;int b;
    if(on!=connected){
        connected=on;session();
        if(on){ready=1u;stage=4u;}
        else if(!configured){ready=0u;stage=step=attempts=resets=changed=at_mode=0u;at_deadline=board_ms+100u;}
        else stage=3u;
    }
    if(connected){
        if(tx_state!=BLE_TX_BUSY)return;
        if(tx_epoch!=epoch||board_ms-tx_started>=TX_MS){tx_state=BLE_TX_FAILED;++tx_timeouts;error=5u;return;}
        if(!tx_left){if(TX_COMPLETE())tx_state=BLE_TX_DONE;return;}
        if(!batch){if((int32_t)(board_ms-tx_at)<0)return;batch=BATCH_N;tx_at=board_ms+BATCH_MS;}
        if(TX_READY()){PUT_BYTE(*tx_data++);--tx_left;--batch;++tx_bytes;}
        return;
    }
    if(configured){while(budget--&&pull()>=0){}return;}
    if(stage==0u||stage==5u){if((int32_t)(board_ms-at_deadline)<0)return;begin();}
    while(budget--&&(b=pull())>=0){
        if(b=='\n') {if(!line_drop){line[line_n]=0;at_line();}line_n=line_drop=0u;}
        else if(b!='\r'){if(line_n+1u<sizeof(line)&&!line_drop)line[line_n++]=(char)b;else line_drop=1u;}
    }
    if(at_bad){retry(2u);return;}
    if(stage==1u&&TX_READY()){
        const char *c=command();
        if(c[at_pos]){PUT_BYTE((uint8_t)c[at_pos++]);++tx_bytes;}
        else if(TX_COMPLETE()){stage=2u;at_deadline=board_ms+AT_MS;}
    }
    if(stage==2u&&at_ok&&(at_mode||!config[step].prefix||at_got)){result();return;}
    if((int32_t)(board_ms-at_deadline)>=0)retry(1u);
}
int ble_serial_read(void) { return connected&&ready&&!reset_seen?pull():-1; }
bool ble_serial_reset_seen(void)
{
    uint8_t v;uint32_t k=LOCK();v=reset_seen;
    if(v&&tx_state==BLE_TX_BUSY){tx_state=BLE_TX_FAILED;tx_left=0u;tx_data=0;batch=0u;}
    reset_seen=0u;UNLOCK(k);return v!=0u;
}
bool ble_serial_ready(void) { return ready!=0u; }
bool ble_serial_connected(void) { return connected!=0u; }
uint8_t ble_serial_tx_state(void) { return tx_state; }
bool ble_serial_write(const uint8_t *p,uint32_t n)
{
    uint32_t k=LOCK();
    if(!connected||!ready||reset_seen||tx_state==BLE_TX_BUSY||!p||!n||n>1048u){UNLOCK(k);return false;}
    tx_data=p;tx_left=n;tx_started=tx_at=board_ms;tx_epoch=epoch;batch=0u;tx_state=BLE_TX_BUSY;
    UNLOCK(k);return true;
}
static void put32(uint8_t *p,uint32_t x) { unsigned i;for(i=0;i<4u;i++)p[i]=(uint8_t)(x>>(8u*i)); }
uint16_t ble_serial_status(uint8_t *out)
{
    uint32_t k;if(!out)return 0u;memset(out,0,BLE_STATUS_SIZE);
    out[0]=1u;out[1]=ready;out[2]=connected;out[3]=configured;out[4]=stage;out[5]=error;out[6]=step;out[7]=attempts;
    k=LOCK();put32(out+8u,rx_bytes);put32(out+16u,uart_errors);put32(out+20u,rx_overflows);UNLOCK(k);
    put32(out+12u,tx_bytes);put32(out+24u,tx_timeouts);put32(out+28u,init_timeouts);put32(out+32u,config_writes);
    memcpy(out+36u,name,sizeof(name));memcpy(out+57u,version,sizeof(version));memcpy(out+73u,mac,sizeof(mac));return BLE_STATUS_SIZE;
}
