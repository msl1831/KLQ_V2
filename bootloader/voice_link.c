#include "voice_link.h"
#include <string.h>
#ifdef KLQ_VOICE_TEST
extern volatile uint32_t board_ms;
extern uint8_t voice_test_tx_ready(void);
extern void voice_test_tx(uint8_t byte);
#else
#include "board.h"
#include "apm32e10x_usart.h"
#endif
#define RING_N 64u
#define FRAME_N 30u
#define ACK_MS 500u
#define PLAY_MS 3000u
#define INTENT_MS 100u
static volatile uint8_t rx[RING_N], rx_epoch[RING_N], rh, rt;
static volatile uint8_t epoch;
static uint8_t frame_epoch, frame_mixed;
static volatile uint32_t rx_at[RING_N];
static uint32_t frame_at;
static volatile uint32_t rx_bytes, uart_errors, rx_overflows;
static uint8_t tx[RING_N], th, tt, frame[FRAME_N], used, expected, sequence, pending_seq;
static uint8_t events, intent_pending, play_events, dedup_next;
static uint32_t last_byte, tx_deadline, request_deadline, play_deadline;
static voice_link_status_t state;
static voice_link_intent_t intent;
static struct { uint32_t semantic; uint8_t seq, score, valid; } dedup[4];
static uint16_t g16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint32_t g32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void put32(uint8_t *p,uint32_t v) { unsigned i;for(i=0;i<4u;i++)p[i]=(uint8_t)(v>>(i*8u)); }
static uint8_t enqueue(uint8_t type,uint8_t cmd,uint8_t seq,const uint8_t *data,uint8_t n)
{
    uint8_t f[FRAME_N],i,total=(uint8_t)(n+10u);uint16_t sum=(uint16_t)(type+cmd+seq);
    if(n>20u || ((tt-th-1u)&(RING_N-1u))<total)return 0u;
    f[0]=0xa5u;f[1]=0xfcu;f[2]=n;f[3]=0u;f[4]=type;f[5]=cmd;f[6]=seq;
    for(i=0u;i<n;i++){f[7u+i]=data[i];sum=(uint16_t)(sum+data[i]);}
    f[7u+n]=(uint8_t)sum;f[8u+n]=(uint8_t)(sum>>8);f[9u+n]=0xfbu;
    if(th==tt)tx_deadline=board_ms+100u;
    for(i=0u;i<total;i++){tx[th]=f[i];th=(uint8_t)((th+1u)&(RING_N-1u));}
    ++state.tx_frames;return 1u;
}
static void receive_byte(uint8_t b)
{
    uint8_t h=rh,next=(uint8_t)((h+1u)&(RING_N-1u));++rx_bytes;
    if(next==rt){++rx_overflows;return;}
    rx[h]=b;rx_epoch[h]=epoch;rx_at[h]=board_ms;rh=next;
}
#ifdef KLQ_VOICE_TEST
void voice_link_test_rx(uint8_t byte) { receive_byte(byte); }
#else
void UART4_IRQHandler(void)
{
    uint32_t s=UART4->STS;uint8_t b;
    if(!(s&0x2fu))return;b=(uint8_t)UART4->DATA;
    if(s&0x0fu)++uart_errors;else if(s&USART_FLAG_RXBNE)receive_byte(b);
}
#endif
void voice_link_drop_intents(void)
{
    if(intent_pending)++state.dropped_intents;intent_pending=0u;++epoch;
}
static void reset_peer(void)
{
    state.awake=state.version_valid=state.request=state.play_state=0u;
    state.last_error=0xfbu;th=tt=0u;voice_link_drop_intents();memset(dedup,0,sizeof(dedup));dedup_next=0u;
}
static void play_done(void)
{
    state.play_state=0u;++state.play_done_count;
}
static void valid(void)
{
    uint8_t n=frame[2],type=frame[4],cmd=frame[5],seq=frame[6],i,zero=0u;const uint8_t *d=frame+7u;
    ++state.rx_frames;state.last_rx_ms=board_ms;
    if(type==0xa3u && cmd==0x9au && n==2u && !d[0] && d[1]>=0xb0u && d[1]<=0xb4u){
        i=(uint8_t)(d[1]-0xb0u);++state.event_count[i];events|=(uint8_t)(1u<<i);
        if(i==0u)reset_peer();
        else if(i==1u)state.awake=1u;
        else if(i==2u){state.awake=0u;voice_link_drop_intents();}
        else if(i==3u && (state.play_state==1u || state.play_state==2u))play_events|=1u;
        else if(i==4u && (state.play_state==1u || state.play_state==2u)){
            play_events|=2u;if(state.play_state==2u)play_done();
        }
        return;
    }
    if(type==0xa0u && cmd==0x91u && n==7u && !d[4] && !d[5]){
        uint32_t semantic=g32(d);
        if(!enqueue(0xa2u,0x91u,seq,&zero,1u)){++state.dropped_intents;return;}
        for(i=0u;i<4u;i++)if(dedup[i].valid && dedup[i].seq==seq && dedup[i].score==d[6] && dedup[i].semantic==semantic){++state.duplicate_count;return;}
        dedup[dedup_next].semantic=semantic;dedup[dedup_next].seq=seq;dedup[dedup_next].score=d[6];dedup[dedup_next].valid=1u;dedup_next=(uint8_t)((dedup_next+1u)&3u);
        if(frame_mixed || frame_epoch!=epoch || (uint32_t)(board_ms-frame_at)>INTENT_MS){++state.dropped_intents;return;}
        if(intent_pending)++state.dropped_intents;
        intent.semantic=state.last_semantic=semantic;intent.received_ms=frame_at;intent.seq=seq;intent.score=d[6];intent_pending=1u;++state.recognition_count;
        if(semantic==0x4b510001u)state.awake=1u;
        return;
    }
    if(type==0xa2u && state.request && seq==pending_seq){
        if(cmd==0xaau && n==2u && d[1]==state.request && (d[0]==0xfdu || d[0]==0xfcu || d[0]==0xfeu)){
            state.last_error=d[0];state.request=0u;
            if(state.play_state==1u)state.play_state=(uint8_t)(play_events?3u:0u);
            return;
        }
        if(cmd==state.request && cmd==0x94u && n==4u && d[0]=='v'){
            memcpy(state.version,d+1u,3u);state.version_valid=1u;state.request=0u;state.last_error=0u;return;
        }
        if(cmd==state.request && cmd==0x92u && n==1u && d[0]==0x80u){
            state.last_ack=d[0];state.last_error=0u;state.request=0u;++state.play_ack_count;state.play_state=2u;
            if(play_events&2u)play_done();return;
        }
    }
    ++state.bad_frames;
}
static void resync(void)
{
    uint8_t i;
    for(i=1u;i+1u<used;i++)if(frame[i]==0xa5u && frame[i+1u]==0xfcu){used=(uint8_t)(used-i);memmove(frame,frame+i,used);expected=0u;return;}
    if(used && frame[used-1u]==0xa5u){frame[0]=0xa5u;used=1u;}else used=0u;
    expected=0u;
}
static void parse(uint8_t b,uint8_t at_epoch,uint32_t at_ms)
{
    uint8_t i,n;uint16_t sum;
    if(used && (uint32_t)(at_ms-last_byte)>20u){++state.bad_frames;used=expected=0u;}
    last_byte=at_ms;
    if(!used){if(b==0xa5u){frame[used++]=b;frame_epoch=at_epoch;frame_mixed=0u;frame_at=at_ms;}return;}
    if(at_epoch!=frame_epoch)frame_mixed=1u;
    if(used==1u && b!=0xfcu){used=(uint8_t)(b==0xa5u);return;}
    if(used>=FRAME_N){++state.bad_frames;used=expected=0u;return;}
    frame[used++]=b;
    for(;;){
        if(used>=4u && !expected){
            if(g16(frame+2u)>20u){++state.bad_frames;resync();continue;}
            expected=(uint8_t)(frame[2]+10u);
        }
        if(!expected || used<expected)return;
        n=frame[2];sum=0u;for(i=4u;i<(uint8_t)(7u+n);i++)sum=(uint16_t)(sum+frame[i]);
        if(frame[9u+n]!=0xfbu || g16(frame+7u+n)!=sum){++state.bad_frames;resync();continue;}
        valid();
        if(used==expected){used=expected=0u;return;}
        used=(uint8_t)(used-expected);memmove(frame,frame+expected,used);expected=0u;
        if(frame[0]!=0xa5u || (used>1u && frame[1]!=0xfcu))resync();
    }
}
void voice_link_init(void)
{
    memset(&state,0,sizeof(state));memset(dedup,0,sizeof(dedup));rh=rt=th=tt=used=expected=sequence=pending_seq=events=intent_pending=play_events=dedup_next=0u;
    rx_bytes=uart_errors=rx_overflows=0u;epoch=frame_epoch=frame_mixed=0u;
#ifndef KLQ_VOICE_TEST
    USART_EnableInterrupt(UART4,USART_INT_RXBNE);USART_EnableInterrupt(UART4,USART_INT_ERR);USART_EnableInterrupt(UART4,USART_INT_PE);
    NVIC_SetPriority(UART4_IRQn,1u);NVIC_EnableIRQ(UART4_IRQn);
#endif
}
void voice_link_poll(void)
{
    uint8_t budget=RING_N;
    if(state.uart_errors!=uart_errors || state.rx_overflows!=rx_overflows){
        state.uart_errors=uart_errors;state.rx_overflows=rx_overflows;rt=rh;used=expected=0u;++state.bad_frames;voice_link_drop_intents();
    }
    if(used && (uint32_t)(board_ms-last_byte)>20u){++state.bad_frames;used=expected=0u;}
    while(rt!=rh && budget--){uint8_t t=rt,b=rx[t],e=rx_epoch[t];uint32_t at=rx_at[t];rt=(uint8_t)((t+1u)&(RING_N-1u));parse(b,e,at);}
    if(state.request && (int32_t)(board_ms-request_deadline)>=0){
        ++state.timeouts;state.last_error=0xffu;state.request=0u;th=tt=0u;
        if(state.play_state==1u)state.play_state=3u;
    }
    if(state.play_state && (int32_t)(board_ms-play_deadline)>=0){state.play_state=0u;state.last_error=0xffu;++state.timeouts;}
    if(th!=tt){
        uint8_t ready;
#ifdef KLQ_VOICE_TEST
        ready=voice_test_tx_ready();
#else
        ready=(uint8_t)(UART4->STS_B.TXBEFLG!=0u);
#endif
        if(ready){
#ifdef KLQ_VOICE_TEST
            voice_test_tx(tx[tt]);
#else
            UART4->DATA=tx[tt];
#endif
            tt=(uint8_t)((tt+1u)&(RING_N-1u));tx_deadline=board_ms+100u;
        }else if((int32_t)(board_ms-tx_deadline)>=0){
            th=tt=0u;++state.tx_errors;state.last_error=0xffu;state.request=0u;
            if(state.play_state)state.play_state=3u;
        }
    }
}
static uint8_t request(uint8_t cmd,const uint8_t *data,uint8_t n)
{
    uint8_t seq;
    if(state.request || state.play_state || th!=tt)return 0u;
    seq=(uint8_t)(sequence+1u);
    if(!enqueue(0xa1u,cmd,seq,data,n))return 0u;
    sequence=pending_seq=seq;state.request=state.last_cmd=cmd;state.last_seq=seq;state.last_ack=state.last_error=0u;request_deadline=board_ms+ACK_MS;return 1u;
}
uint8_t voice_link_query_version(void)
{
    uint8_t cmd=0x8au;return request(0x94u,&cmd,1u);
}
uint8_t voice_link_play(uint32_t id)
{
    uint8_t d[6];d[0]=0x80u;d[1]=0x90u;put32(d+2u,id);
    if(!request(0x92u,d,6u))return 0u;
    state.play_id=id;state.play_state=1u;play_events=0u;play_deadline=board_ms+PLAY_MS;return 1u;
}
uint8_t voice_link_take_intent(voice_link_intent_t *out)
{
    if(!intent_pending || !out)return 0u;
    if((uint32_t)(board_ms-intent.received_ms)>INTENT_MS){voice_link_drop_intents();return 0u;}
    *out=intent;intent_pending=0u;return 1u;
}
uint8_t voice_link_take_events(void) { uint8_t e=events;events=0u;return e; }
void voice_link_get_status(voice_link_status_t *out)
{
    if(!out)return;*out=state;out->rx_bytes=rx_bytes;out->uart_errors=uart_errors;out->rx_overflows=rx_overflows;
}
