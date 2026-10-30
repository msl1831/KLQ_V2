#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
volatile uint32_t board_ms;
static uint8_t sent[8192],ready=1u;
static unsigned sent_n,checks;
uint8_t voice_test_tx_ready(void){return ready;}
void voice_test_tx(uint8_t b){if(sent_n>=sizeof(sent))abort();sent[sent_n++]=b;}
#define CHECK(c) do{++checks;if(!(c)){printf("voice wire line%u: %s\n",__LINE__,#c);exit(1);}}while(0)
#include "../bootloader/voice_link.c"
static void setup(void){board_ms=1000u;sent_n=0u;ready=1u;voice_link_init();}
static void pump(void){unsigned n=200u;while(th!=tt && n--){++board_ms;voice_link_poll();}CHECK(th==tt);}
static unsigned wire(uint8_t *f,uint8_t type,uint8_t cmd,uint8_t seq,const uint8_t *d,unsigned n)
{
    unsigned i,sum=type+cmd+seq;f[0]=0xa5u;f[1]=0xfcu;f[2]=(uint8_t)n;f[3]=0;f[4]=type;f[5]=cmd;f[6]=seq;
    for(i=0;i<n;i++){f[7+i]=d[i];sum+=d[i];}f[7+n]=(uint8_t)sum;f[8+n]=(uint8_t)(sum>>8);f[9+n]=0xfb;return n+10u;
}
static void feed(const uint8_t *f,unsigned n){unsigned i;for(i=0;i<n;i++)voice_link_test_rx(f[i]);voice_link_poll();}
static void message(uint8_t type,uint8_t cmd,uint8_t seq,const uint8_t *d,unsigned n){uint8_t f[30];feed(f,wire(f,type,cmd,seq,d,n));}
static void event(uint8_t kind){uint8_t d[2]={0,0};d[1]=kind;message(0xa3,0x9a,9,d,2);}
static void recognition(uint8_t seq,uint32_t semantic,uint8_t score){uint8_t d[7];put32(d,semantic);d[4]=d[5]=0;d[6]=score;message(0xa0,0x91,seq,d,7);}
static void version(uint8_t seq){uint8_t d[4]={'v',0,2,0};message(0xa2,0x94,seq,d,4);}
static void playack(uint8_t seq,uint8_t result){message(0xa2,0x92,seq,&result,1);}
static void golden(void)
{
    static const uint8_t query[]={0xa5,0xfc,1,0,0xa1,0x94,1,0x8a,0xc0,1,0xfb};
    static const uint8_t play[]={0xa5,0xfc,6,0,0xa1,0x92,2,0x80,0x90,0x8a,0x13,0,0,0xe2,2,0xfb};
    voice_link_status_t s;setup();CHECK(voice_link_query_version());CHECK(!voice_link_play(5002));pump();CHECK(sent_n==sizeof(query)&&!memcmp(sent,query,sizeof(query)));
    version(7);CHECK(state.request==0x94&&!state.version_valid);version(1);CHECK(!state.request&&state.version_valid);voice_link_get_status(&s);CHECK(s.version[0]==0&&s.version[1]==2&&s.version[2]==0&&s.rx_bytes==28);
    sent_n=0;CHECK(voice_link_play(5002));pump();CHECK(sent_n==sizeof(play)&&!memcmp(sent,play,sizeof(play)));
    event(0xb3);CHECK(!voice_link_query_version());playack(2,0x80);CHECK(state.play_state==2&&state.play_ack_count==1);event(0xb4);CHECK(state.play_state==0&&state.play_done_count==1);CHECK(voice_link_query_version());
}
static void recognition_and_dedup(void)
{
    voice_link_intent_t in;unsigned before;setup();recognition(255,0x4b510001UL,88);CHECK(state.awake&&voice_link_take_intent(&in));CHECK(in.semantic==0x4b510001UL&&in.seq==255&&in.score==88);pump();
    CHECK(sent_n==11&&sent[4]==0xa2&&sent[5]==0x91&&sent[6]==255&&sent[7]==0);
    before=sent_n;board_ms+=4000;recognition(255,0x4b510001UL,88);CHECK(!voice_link_take_intent(&in)&&state.duplicate_count==1);pump();CHECK(sent_n==before+11);
    recognition(0,0x4b510101UL,90);recognition(1,0x4b510103UL,91);CHECK(voice_link_take_intent(&in)&&in.semantic==0x4b510103UL&&state.dropped_intents==1);pump();
    recognition(2,0x4b510102UL,70);board_ms+=101;CHECK(!voice_link_take_intent(&in)&&state.dropped_intents==2);pump();
    recognition(3,0x4b510101UL,99);event(0xb2);CHECK(!state.awake&&!voice_link_take_intent(&in));pump();
    event(0xb1);CHECK(state.awake);CHECK(voice_link_take_events()&6u);CHECK(!voice_link_take_events());
}
static void peer_reset_and_tuple_reuse(void)
{
    voice_link_intent_t in;unsigned i;setup();
    recognition(42,0x4b510001UL,90);CHECK(voice_link_take_intent(&in));pump();event(0xb0);pump();
    recognition(42,0x4b510101UL,90);CHECK(voice_link_take_intent(&in)&&in.semantic==0x4b510101UL);pump();
    recognition(42,0x4b510101UL,90);CHECK(!voice_link_take_intent(&in)&&state.duplicate_count==1);pump();
    recognition(42,0x4b510102UL,90);CHECK(voice_link_take_intent(&in)&&in.semantic==0x4b510102UL);pump();
    recognition(42,0x4b510102UL,91);CHECK(voice_link_take_intent(&in)&&in.score==91);pump();
    for(i=0;i<256;i++){recognition((uint8_t)i,0x4b510001UL,90);CHECK(voice_link_take_intent(&in));pump();}
    recognition(42,0x4b510102UL,91);CHECK(voice_link_take_intent(&in));pump();
}
static void epoch_and_audio(void)
{
    uint8_t f[30],d[7];unsigned n,i;voice_link_intent_t in;
    setup();put32(d,0x4b510101UL);d[4]=d[5]=0;d[6]=90;n=wire(f,0xa0,0x91,10,d,7);
    for(i=0;i<n;i++)voice_link_test_rx(f[i]);voice_link_drop_intents();voice_link_poll();CHECK(!voice_link_take_intent(&in)&&state.dropped_intents==1);pump();
    feed(f,n);CHECK(!voice_link_take_intent(&in)&&state.duplicate_count==1);pump();
    CHECK(voice_link_query_version());pump();n=wire(f,0xa2,0x94,pending_seq,(const uint8_t *)"v\0\2\0",4);for(i=0;i<n;i++)voice_link_test_rx(f[i]);voice_link_drop_intents();voice_link_poll();CHECK(state.version_valid&&!state.request);
    put32(d,0x4b510102UL);d[6]=90;n=wire(f,0xa0,0x91,11,d,7);for(i=0;i<5;i++)voice_link_test_rx(f[i]);voice_link_poll();voice_link_drop_intents();for(;i<n;i++)voice_link_test_rx(f[i]);voice_link_poll();CHECK(!voice_link_take_intent(&in));pump();
}
static void delayed_rx_is_not_new_intent(void)
{
    uint8_t f[30],d[7];unsigned n,i;voice_link_intent_t in;
    setup();put32(d,0x4b510101UL);d[4]=d[5]=0;d[6]=91;n=wire(f,0xa0,0x91,5,d,7);
    for(i=0;i<n;i++)voice_link_test_rx(f[i]);board_ms+=300;voice_link_poll();CHECK(!voice_link_take_intent(&in)&&state.dropped_intents==1);pump();
    feed(f,n);CHECK(!voice_link_take_intent(&in)&&state.duplicate_count==1);pump();
    CHECK(voice_link_query_version());pump();n=wire(f,0xa2,0x94,pending_seq,(const uint8_t *)"v\0\2\0",4);for(i=0;i<n;i++)voice_link_test_rx(f[i]);board_ms+=300;voice_link_poll();CHECK(state.version_valid&&!state.request);
}
static void corruption_and_recovery(void)
{
    uint8_t f[30],d[20];unsigned n,i,bad;voice_link_intent_t in;setup();memset(d,0,sizeof(d));put32(d,0x4b510101UL);d[6]=95;n=wire(f,0xa0,0x91,1,d,7);
    voice_link_test_rx(0x55);voice_link_test_rx(0xa5);feed(f,n);CHECK(voice_link_take_intent(&in));pump();
    f[7]^=1;feed(f,n);CHECK(!voice_link_take_intent(&in));f[7]^=1;f[n-1]=0;feed(f,n);CHECK(!voice_link_take_intent(&in));f[n-1]=0xfb;
    voice_link_test_rx(0xa5);voice_link_test_rx(0xfc);voice_link_test_rx(21);voice_link_test_rx(0);voice_link_poll();
    f[6]=2;n=wire(f,0xa0,0x91,2,d,7);feed(f,n);CHECK(voice_link_take_intent(&in));pump();CHECK(state.bad_frames>=3);
    n=wire(f,0xa0,0x91,3,d,20);feed(f,n);CHECK(!voice_link_take_intent(&in));
    n=wire(f,0xa0,0x91,3,d,7);feed(f,8);board_ms+=21;voice_link_poll();feed(f,n);CHECK(voice_link_take_intent(&in));pump();
    d[4]=1;n=wire(f,0xa0,0x91,4,d,7);feed(f,n);CHECK(!voice_link_take_intent(&in));
    bad=state.bad_frames;for(i=0;i<100;i++)voice_link_test_rx(0xa5);voice_link_poll();CHECK(rx_overflows>0&&state.bad_frames>bad&&!used);n=wire(f,0xa0,0x91,5,(const uint8_t *)"\1\1\121\113\0\0\132",7);feed(f,n);CHECK(voice_link_take_intent(&in));pump();
}
static void nested_recovery_keeps_exit(void)
{
    uint8_t f[30],d[2]={0,0xb1};unsigned n;
    setup();memset(f,0,sizeof(f));f[0]=0xa5;f[1]=0xfc;f[2]=20;
    n=wire(f+4,0xa3,0x9a,1,d,2);CHECK(n==12);d[1]=0xb2;wire(f+16,0xa3,0x9a,2,d,2);
    feed(f,sizeof(f));CHECK(state.event_count[1]==1&&state.event_count[2]==1&&!state.awake);CHECK((voice_link_take_events()&6u)==6u);
}
static void playback_serialization(void)
{
    uint8_t d[2];setup();event(0xb4);CHECK(voice_link_play(5000));pump();event(0xb3);event(0xb4);CHECK(state.play_state==1);playack(pending_seq,0x80);CHECK(!state.play_state&&state.play_done_count==1);
    CHECK(voice_link_play(5001));pump();playack(pending_seq,0);CHECK(state.request==0x92&&state.play_state==1);board_ms=request_deadline;voice_link_poll();CHECK(!state.request&&state.play_state==3&&!voice_link_play(5002));event(0xb4);CHECK(state.play_state==3);board_ms=play_deadline;voice_link_poll();CHECK(!state.play_state&&voice_link_play(5002));pump();
    d[0]=0xfd;d[1]=0x92;message(0xa2,0xaa,pending_seq,d,2);CHECK(!state.request&&!state.play_state&&state.last_error==0xfd);
    CHECK(voice_link_play(5003));pump();event(0xb3);d[0]=0xfc;message(0xa2,0xaa,pending_seq,d,2);CHECK(state.play_state==3);board_ms=play_deadline;voice_link_poll();CHECK(!state.play_state);
    CHECK(voice_link_query_version());pump();d[0]=0xfe;d[1]=0x94;message(0xa2,0xaa,pending_seq,d,2);CHECK(!state.request&&state.last_error==0xfe);
}
static void wrap_and_stall(void)
{
    unsigned i;setup();board_ms=0xfffffff0UL;CHECK(voice_link_query_version());pump();version(pending_seq);CHECK(state.version_valid);
    for(i=0;i<256;i++){CHECK(voice_link_query_version());pump();version(pending_seq);}CHECK(sequence==1);
    setup();ready=0;CHECK(voice_link_play(5000));board_ms+=100;voice_link_poll();CHECK(state.tx_errors==1&&!state.request&&state.play_state==3&&th==tt);ready=1;pump();CHECK(!sent_n);board_ms=play_deadline;voice_link_poll();CHECK(voice_link_query_version());pump();
    recognition(9,0x4b510101UL,80);event(0xb0);CHECK(!state.awake&&!state.version_valid&&!state.request&&!state.play_state&&!intent_pending);CHECK(voice_link_take_events()&1u);
}
int main(void)
{
    golden();recognition_and_dedup();peer_reset_and_tuple_reuse();epoch_and_audio();corruption_and_recovery();playback_serialization();wrap_and_stall();nested_recovery_keeps_exit();delayed_rx_is_not_new_intent();
    printf("VMUP UART4 transport: %u assertions passed; stateRAM=%u bytes\n",checks,(unsigned)(sizeof(rx)+sizeof(rx_epoch)+sizeof(rx_at)+sizeof(tx)+sizeof(frame)+sizeof(state)+sizeof(intent)+sizeof(dedup)));return 0;
}
