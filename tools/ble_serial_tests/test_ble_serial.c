#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ble_serial.h"
volatile uint32_t board_ms;
static unsigned checks,sets,resets,queries,commands,write_n,uart_bytes;
static uint8_t link_up,tx_ready=1,tx_complete=1,drop_set_ack,drop_query_ack,deny_hello,malformed_mac,noise_ok,ignore_mode,defer_name,defer_mode;
static char pending_name[21];static unsigned pending_mode;
static char cmd[80],name[21],advname[21],ver[16],uuid[3][8];
static unsigned used,echo_on,role_config,role_actual,sleep_on,mode,notify_on,btat_on;
static uint8_t written[1200],irq_enabled=1u,inject_lock,inject_unlock;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL line=%u condition=%s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t g32(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
uint8_t ble_test_connected(void){return link_up;}
uint8_t ble_test_tx_ready(void){return tx_ready;}
uint8_t ble_test_tx_complete(void){return tx_complete;}
static void rx(const char *p){while(*p)ble_serial_test_receive((uint8_t)*p++,0u);}
static void number(const char *prefix,unsigned n){char b[80];sprintf(b,"%s%u\r\nOK\r\n",prefix,n);rx(b);}
static void string(const char *prefix,const char *p){char b[80];sprintf(b,"%s%s\r\nOK\r\n",prefix,p);rx(b);}
static void process(void)
{
    ++commands;
    if(echo_on){rx(cmd);rx("\r\n");}
    if(!strcmp(cmd,"AT")){
        if(noise_ok){rx("CONNECT OK\r\n");return;}
        if(!deny_hello)rx("OK\r\n");return;
    }
    if(!strcmp(cmd,"AT+RST")){++resets;role_actual=role_config==2u?2u:1u;if(pending_name[0]){strcpy(name,pending_name);pending_name[0]=0;}if(pending_mode){mode=pending_mode;pending_mode=0;}strcpy(advname,name);rx("OK\r\n");return;}
    if(strchr(cmd,'?')){
        ++queries;if(drop_query_ack)return;
        if(!strcmp(cmd,"AT+ECHO?"))number("+ECHO:",echo_on);
        else if(!strcmp(cmd,"AT+ROLEMODE?"))number("+ROLEMODE:",role_config);
        else if(!strcmp(cmd,"AT+ROLE?"))string("+ROLE:",role_actual==2u?"Peripheral":"Central");
        else if(!strcmp(cmd,"AT+NAME?"))string("+NAME:",name);
        else if(!strcmp(cmd,"AT+MODE?"))number("+MODE:",mode);
        else if(!strcmp(cmd,"AT+SLEEP?"))number("+SLEEP:",sleep_on);
        else if(!strcmp(cmd,"AT+CONNOTIFY?"))number("+CONNOTIFY:",notify_on);
        else if(!strcmp(cmd,"AT+BTAT?"))number("+BTAT:",btat_on);
        else if(!strcmp(cmd,"AT+SUUID?"))string("+SUUID:",uuid[0]);
        else if(!strcmp(cmd,"AT+RUUID?"))string("+RUUID:",uuid[1]);
        else if(!strcmp(cmd,"AT+WUUID?"))string("+WUUID:",uuid[2]);
        else if(!strcmp(cmd,"AT+MAC?"))string("+MAC:",malformed_mac?"Z123":"A1B2C3D4E5F6");
        else if(!strcmp(cmd,"AT+VER?"))string("+VER:",ver);
        else {CHECK(0);}
        return;
    }
    ++sets;
    if(!strcmp(cmd,"AT+ECHO=0"))echo_on=0u;
    else if(!strcmp(cmd,"AT+ROLE=2"))role_config=2u;
    else if(!strcmp(cmd,"AT+NAME=KLQ2")){if(defer_name)strcpy(pending_name,"KLQ2");else strcpy(name,"KLQ2");}
    else if(!strcmp(cmd,"AT+MODE=2")){if(!ignore_mode){if(defer_mode)pending_mode=2u;else mode=2u;}}
    else if(!strcmp(cmd,"AT+SLEEP=0"))sleep_on=0u;
    else if(!strcmp(cmd,"AT+CONNOTIFY=0"))notify_on=0u;
    else if(!strcmp(cmd,"AT+BTAT=0"))btat_on=0u;
    else if(!strcmp(cmd,"AT+SUUID=FFF0"))strcpy(uuid[0],"FFF0");
    else if(!strcmp(cmd,"AT+RUUID=FFF1"))strcpy(uuid[1],"FFF1");
    else if(!strcmp(cmd,"AT+WUUID=FFF2"))strcpy(uuid[2],"FFF2");
    else CHECK(0);
    if(drop_set_ack){drop_set_ack=0u;return;}
    rx("OK\r\n");
}
void ble_test_tx(uint8_t b)
{
    ++uart_bytes;
    if(link_up){CHECK(write_n<sizeof(written));written[write_n++]=b;return;}
    CHECK(used+1u<sizeof(cmd));
    if(b=='\n') {CHECK(used&&cmd[used-1u]=='\r');cmd[used-1u]=0;process();used=0u;}
    else cmd[used++]=(char)b;
}
uint32_t ble_test_lock(void)
{
    uint32_t old=irq_enabled;
    if(irq_enabled&&inject_lock){inject_lock=0u;ble_serial_test_receive(0u,1u);}
    irq_enabled=0u;return old;
}
void ble_test_unlock(uint32_t old)
{
    CHECK(!irq_enabled);irq_enabled=(uint8_t)old;
    if(irq_enabled&&inject_unlock){inject_unlock=0u;ble_serial_test_receive(0u,1u);}
}
static void defaults(unsigned configured)
{
    board_ms=0u;link_up=used=sets=resets=queries=commands=write_n=uart_bytes=0u;
    irq_enabled=1u;inject_lock=inject_unlock=0u;tx_ready=tx_complete=1u;drop_set_ack=drop_query_ack=deny_hello=malformed_mac=noise_ok=ignore_mode=defer_name=defer_mode=0u;pending_name[0]=0;pending_mode=0;
    echo_on=configured?0u:1u;role_config=configured?2u:0u;role_actual=configured?2u:1u;sleep_on=configured?0u:1u;mode=configured?2u:0u;notify_on=configured?0u:1u;btat_on=configured?0u:1u;
    strcpy(name,configured?"KLQ2":"@ECB02_DEFAULT");strcpy(advname,name);strcpy(ver,"1.0.0");
    strcpy(uuid[0],configured?"FFF0":"ABCD");strcpy(uuid[1],configured?"FFF1":"ABCE");strcpy(uuid[2],configured?"FFF2":"ABCF");
    ble_serial_init();CHECK(ble_serial_reset_seen());CHECK(!ble_serial_reset_seen());
}
static void advance(unsigned n){while(n--){++board_ms;ble_serial_poll();}}
static void configured(void)
{
    uint8_t s[88];unsigned n=0u;
    while(!ble_serial_ready()&&n++<10000u)advance(1);
    CHECK(ble_serial_ready()&&!ble_serial_connected());CHECK(ble_serial_status(s)==88u&&s[0]==1u&&s[3]==1u&&s[4]==3u&&s[5]==0u);
    CHECK(!strcmp((char *)s+36,"KLQ2")&&!strcmp((char *)s+57,"1.0.0")&&!strcmp((char *)s+73,"A1B2C3D4E5F6"));CHECK(s[86]==0u&&s[87]==0u);
}
static void initialization(void)
{
    uint8_t s[88];unsigned old_sets,old_resets;
    defaults(0);advance(99);CHECK(!commands&&!ble_serial_ready());configured();
    CHECK(sets==10u&&resets==2u&&!strcmp(advname,"KLQ2"));ble_serial_status(s);CHECK(g32(s+32)==10u);
    old_sets=sets;old_resets=resets;ble_serial_init();used=0u;configured();CHECK(sets==old_sets&&resets==old_resets);
    puts("PASS persistent config difference writes, actual role reset, advertising reset, immutable defaults re-query");
    defaults(0);drop_set_ack=1u;configured();CHECK(sets==10u);ble_serial_status(s);CHECK(g32(s+28)>=1u);
    puts("PASS lost setting ACK re-queries rather than blind Flash write retry");
    defaults(1);noise_ok=1u;advance(1800);CHECK(!ble_serial_ready()&&sets==0u);ble_serial_status(s);CHECK(g32(s+28)>0u);
    defaults(1);deny_hello=1u;advance(2200);CHECK(!ble_serial_ready()&&sets==0u);
    defaults(1);malformed_mac=1u;advance(2500);CHECK(!ble_serial_ready()&&!sets);
    defaults(1);drop_query_ack=1u;advance(2000);CHECK(!ble_serial_ready()&&!sets);
    puts("PASS strict OK line, unavailable device, missing query fields, malformed identity all bounded");
    defaults(1);strcpy(name,"OLD");strcpy(advname,"OLD");mode=0u;defer_name=defer_mode=1u;configured();CHECK(sets==2u&&resets==1u&&!strcmp(advname,"KLQ2"));
    defaults(1);mode=0u;ignore_mode=1u;advance(6000);CHECK(!ble_serial_ready()&&sets==1u);
    puts("PASS deferred setting effects verified after one reset; acknowledged non-sticking setting cannot wear Flash by retries");
}
static void connected_boot(void)
{
    uint8_t s[88];unsigned i;
    defaults(0);link_up=1u;ble_serial_init();advance(3000);CHECK(!commands&&!uart_bytes&&ble_serial_ready()&&ble_serial_connected());
    ble_serial_status(s);CHECK(!s[3]&&s[4]==4u&&!s[36]&&!s[57]&&!s[73]);CHECK(ble_serial_reset_seen());
    for(i=0;i<200u;i++)ble_serial_test_receive((uint8_t)i,0u);
    for(i=0;i<200u;i++)CHECK(ble_serial_read()==(int)(uint8_t)i);CHECK(ble_serial_read()==-1);
    link_up=0u;advance(1);CHECK(ble_serial_reset_seen()&&!ble_serial_ready()&&!ble_serial_connected());configured();
    puts("PASS already-connected APP/BOOT starts pure binary without AT, defer config until disconnect");
}
static void transfers(void)
{
    uint8_t b[1048],s[88];unsigned i,old;
    defaults(1);configured();link_up=1u;advance(1);CHECK(ble_serial_reset_seen());for(i=0;i<sizeof(b);i++)b[i]=(uint8_t)i;
    CHECK(!ble_serial_write(0,1)&&!ble_serial_write(b,0)&&!ble_serial_write(b,1049));
    CHECK(ble_serial_write(b,41));CHECK(!ble_serial_write(b,1));
    for(i=0;i<100u;i++)ble_serial_poll();CHECK(write_n==20u&&ble_serial_tx_state()==BLE_TX_BUSY);
    board_ms+=19u;for(i=0;i<100u;i++)ble_serial_poll();CHECK(write_n==20u);
    ++board_ms;for(i=0;i<100u;i++)ble_serial_poll();CHECK(write_n==40u);
    board_ms+=20u;tx_complete=0u;for(i=0;i<100u;i++)ble_serial_poll();CHECK(write_n==41u&&ble_serial_tx_state()==BLE_TX_BUSY);
    tx_complete=1u;ble_serial_poll();CHECK(ble_serial_tx_state()==BLE_TX_DONE&&!memcmp(written,b,41));
    old=write_n;CHECK(ble_serial_write(b,sizeof(b)));advance(1500);CHECK(ble_serial_tx_state()==BLE_TX_DONE&&write_n-old==sizeof(b)&&!memcmp(written+old,b,sizeof(b)));
    CHECK(ble_serial_write(b,1u));tx_ready=0u;advance(7999);CHECK(ble_serial_tx_state()==BLE_TX_BUSY);advance(1);CHECK(ble_serial_tx_state()==BLE_TX_FAILED);ble_serial_status(s);CHECK(g32(s+24)==1u);
    tx_ready=1u;CHECK(ble_serial_write(b,1u));link_up=0u;advance(1);CHECK(ble_serial_tx_state()==BLE_TX_FAILED&&ble_serial_reset_seen()&&ble_serial_read()==-1);
    puts("PASS fixed TX ownership, 20B/20ms batches, full1048, TC-before-DONE, 8s timeout and disconnect");
}
static void ring_faults(void)
{
    uint8_t s[88];unsigned i;uint32_t before;
    defaults(1);configured();link_up=1u;advance(1);CHECK(ble_serial_reset_seen());ble_serial_status(s);before=g32(s+8);
    for(i=0;i<1023u;i++)ble_serial_test_receive((uint8_t)i,0u);ble_serial_test_receive(0,0u);
    CHECK(ble_serial_read()==-1&&ble_serial_reset_seen()&&ble_serial_read()==-1);ble_serial_status(s);CHECK(g32(s+20)==1u&&g32(s+8)-before==1024u);
    ble_serial_test_receive(123,0u);CHECK(ble_serial_read()==123);ble_serial_test_receive(17,1u);CHECK(ble_serial_reset_seen());ble_serial_status(s);CHECK(g32(s+16)==1u);
    CHECK(ble_serial_status(0)==0u);
    puts("PASS full ring and UART faults reset epoch without accepting a mixed frame");
}
static void reset_borrow(void)
{
    uint8_t old[40],fresh[40];unsigned before,i;
    defaults(1);link_up=1u;ble_serial_init();memset(old,0x11,sizeof(old));memset(fresh,0x22,sizeof(fresh));
    CHECK(ble_serial_tx_state()==BLE_TX_IDLE&&!ble_serial_write(old,sizeof(old)));
    CHECK(ble_serial_reset_seen()&&ble_serial_tx_state()==BLE_TX_IDLE);
    CHECK(ble_serial_write(old,sizeof(old)));ble_serial_poll();before=write_n;
    /* IRQ lands after the protocol's poll, before it consumes reset. */
    ble_serial_test_receive(0,1u);CHECK(ble_serial_tx_state()==BLE_TX_BUSY);
    CHECK(ble_serial_reset_seen()&&ble_serial_tx_state()==BLE_TX_FAILED);
    memset(old,0xee,sizeof(old));for(i=0;i<50u;i++)ble_serial_poll();CHECK(write_n==before);
    CHECK(ble_serial_write(fresh,sizeof(fresh)));ble_serial_test_receive(0,1u);
    CHECK(ble_serial_reset_seen()&&ble_serial_tx_state()==BLE_TX_FAILED);
    /* Arrival at LOCK entry must be seen before validation and borrowing. */
    inject_lock=1u;CHECK(!ble_serial_write(fresh,sizeof(fresh)));
    CHECK(ble_serial_reset_seen()&&irq_enabled);
    /* Arrival as the lock exits belongs to the next reset and releases borrow. */
    inject_unlock=1u;CHECK(ble_serial_write(fresh,sizeof(fresh)));
    CHECK(ble_serial_tx_state()==BLE_TX_BUSY&&ble_serial_reset_seen()&&ble_serial_tx_state()==BLE_TX_FAILED);
    CHECK(ble_serial_write(fresh,1u));ble_serial_poll();ble_serial_poll();CHECK(ble_serial_tx_state()==BLE_TX_DONE);
    ble_serial_test_receive(0,1u);CHECK(ble_serial_reset_seen()&&ble_serial_tx_state()==BLE_TX_DONE);
    irq_enabled=0u;ble_serial_test_receive(0,1u);CHECK(ble_serial_reset_seen()&&!irq_enabled);irq_enabled=1u;
    puts("PASS reset consumption releases borrow atomically; pending reset blocks starts, IRQ-at-lock and IRQ-after-start safe");
}
int main(void)
{
    initialization();connected_boot();transfers();ring_faults();reset_borrow();
    printf("RESULT PASS assertions=%u\n",checks);return 0;
}
