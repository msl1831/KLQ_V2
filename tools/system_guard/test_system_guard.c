#include "guard_mock.h"
#include "guard_source.c"
volatile uint32_t board_ms;
GuardRCM mock_rcm;
GuardIWDT mock_iwdt;
GuardDBGMCU mock_dbg;
GuardSCB mock_scb;
GuardGPIO mock_gpioc;
static unsigned checks,reset_calls,dsb_calls,dmb_calls,detach_calls,clear_calls,feed_calls,access_faults;
static uint32_t delay_total,dmb_magic;
static jmp_buf reset_jump,off_jump;
static uint8_t off_hook,key_pending;
static unsigned key_count;
static uint32_t key_log[16],key_psc[16],key_reload[16];
static volatile LONG tick_running;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL line=%u condition=%s\n",__LINE__,#x);exit(1);}}while(0)
static void flush_key(void)
{
    if(key_pending){CHECK(key_count<16);key_log[key_count]=IWDT->KEY;key_psc[key_count]=IWDT->PSC;key_reload[key_count]=IWDT->CNTRLD;++key_count;key_pending=0;}
}
volatile uint32_t *guard_key_slot(void)
{
    flush_key();key_pending=1;return &IWDT->KEY;
}
void stub_dsb(void){++dsb_calls;}
void stub_dmb(void){++dmb_calls;dmb_magic=((volatile uint32_t *)FAULT_ADDRESS)[0];}
void NVIC_SystemReset(void){flush_key();++reset_calls;longjmp(reset_jump,1);}
void usb_detach(void){++detach_calls;}
void display_clear(void){++clear_calls;}
void delay_ms(uint32_t ms){delay_total+=ms;board_ms+=ms;}
void board_watchdog_feed(void)
{
    ++feed_calls;
    if(off_hook){CHECK(GPIOC->BC==GPIO_PIN_5);CHECK(clear_calls==1);CHECK(!delay_total);longjmp(off_jump,1);}
    production_watchdog_feed();flush_key();
}
static void setup(void)
{
    memset(&mock_rcm,0,sizeof(mock_rcm));memset(&mock_iwdt,0,sizeof(mock_iwdt));
    memset(&mock_dbg,0,sizeof(mock_dbg));memset(&mock_scb,0,sizeof(mock_scb));memset(&mock_gpioc,0,sizeof(mock_gpioc));
    memset((void *)FAULT_ADDRESS,0,32);board_ms=0;watchdog_enabled=0;watchdog_at=0;
    reset_calls=dsb_calls=dmb_calls=detach_calls=clear_calls=feed_calls=access_faults=0;
    delay_total=dmb_magic=0;off_hook=key_pending=0;key_count=0;mock_iwdt.key_slot=guard_key_slot;
}
static DWORD WINAPI clock_thread(LPVOID ignored)
{
    (void)ignored;while(tick_running){Sleep(1);board_ms+=10u;}return 0;
}
static HANDLE start_clock(void)
{
    HANDLE h;tick_running=1;h=CreateThread(0,0,clock_thread,0,0,0);CHECK(h!=0);return h;
}
static void stop_clock(HANDLE h)
{
    tick_running=0;CHECK(WaitForSingleObject(h,2000)==WAIT_OBJECT_0);CHECK(CloseHandle(h));
}
static void watchdog_tests(void)
{
#ifdef KLQ_DEMO_APP
    HANDLE tick;
    setup();board_ms=100000;IWDT->KEY=0x1234;board_watchdog_feed();CHECK(IWDT->KEY==0x1234&&!board_watchdog_enabled());
    setup();board_ms=100;RCM->CSTS_B.LSIRDYFLG=1;board_watchdog_init();flush_key();
    CHECK(key_count==3);CHECK(key_log[0]==0xCCCC&&key_psc[0]==0&&key_reload[0]==0);
    CHECK(key_log[1]==0x5555&&key_log[2]==0xAAAA&&key_psc[1]==6&&key_reload[1]==312);
    CHECK(board_watchdog_enabled()&&RCM->CSTS_B.LSIEN==1);CHECK(IWDT->KEY==0xAAAA&&IWDT->PSC==6&&IWDT->CNTRLD==312);
    CHECK((DBGMCU->CFG&(1u<<8))&&watchdog_at==350);
    IWDT->KEY=0x1234;board_ms=349;board_watchdog_feed();CHECK(IWDT->KEY==0x1234&&watchdog_at==350);
    board_ms=350;board_watchdog_feed();CHECK(IWDT->KEY==0xAAAA&&watchdog_at==600);
    IWDT->KEY=0x1234;board_ms=599;board_watchdog_feed();CHECK(IWDT->KEY==0x1234);board_ms=600;board_watchdog_feed();CHECK(IWDT->KEY==0xAAAA&&watchdog_at==850);
    IWDT->KEY=0x1234;board_ms=2000;board_watchdog_feed();CHECK(IWDT->KEY==0xAAAA&&watchdog_at==2250);
    IWDT->KEY=0x1234;board_watchdog_feed();CHECK(IWDT->KEY==0x1234&&watchdog_at==2250);
    setup();board_ms=0xFFFFFFF0u;RCM->CSTS_B.LSIRDYFLG=1;board_watchdog_init();flush_key();CHECK(watchdog_at==234);
    IWDT->KEY=0x1234;board_ms=233;board_watchdog_feed();CHECK(IWDT->KEY==0x1234);board_ms=234;board_watchdog_feed();CHECK(IWDT->KEY==0xAAAA&&watchdog_at==484);
    setup();tick=start_clock();board_watchdog_init();stop_clock(tick);
    CHECK(board_ms>=100&&!board_watchdog_enabled()&&IWDT->KEY==0&&IWDT->PSC==0&&IWDT->CNTRLD==0);
    CHECK(RCM->CSTS_B.LSIEN==1);board_ms+=1000;board_watchdog_feed();CHECK(IWDT->KEY==0&&!board_watchdog_enabled());
    setup();RCM->CSTS_B.LSIRDYFLG=1;IWDT->STS=1;tick=start_clock();
    if(!setjmp(reset_jump)){board_watchdog_init();stop_clock(tick);CHECK(0);}
    stop_clock(tick);CHECK(board_ms>=200&&!board_watchdog_enabled()&&IWDT->KEY==0x5555);
    CHECK(IWDT->PSC==6&&IWDT->CNTRLD==312&&DBGMCU->CFG==0);
    CHECK(reset_calls==1&&dsb_calls==1&&((volatile uint32_t *)BOOT_REQUEST_ADDRESS)[0]==BOOT_REQUEST_MAGIC);
    CHECK(((volatile uint32_t *)BOOT_REQUEST_ADDRESS)[1]==~BOOT_REQUEST_MAGIC);
    CHECK(key_count==2&&key_log[0]==0xCCCC&&key_log[1]==0x5555);
#else
    setup();board_ms=100000;RCM->CSTS_B.LSIRDYFLG=1;board_watchdog_init();board_watchdog_feed();
    CHECK(!board_watchdog_enabled()&&!RCM->CSTS_B.LSIEN&&!IWDT->KEY&&!IWDT->PSC&&!IWDT->CNTRLD);
#endif
}
static void invoke_fault(uint32_t *frame,uint32_t exception,uint32_t pc,uint32_t lr)
{
    volatile uint32_t *f=(volatile uint32_t *)FAULT_ADDRESS;
    setup();SCB->CFSR=0x82;
    if(!setjmp(reset_jump)){
        __try{board_fault_capture(frame,exception);}
        __except(EXCEPTION_EXECUTE_HANDLER){++access_faults;}
        CHECK(0);
    }
    CHECK(!access_faults&&reset_calls==1&&dmb_calls==1&&dsb_calls==1&&dmb_magic==0);
    CHECK(f[0]==FAULT_MAGIC&&f[1]==exception&&f[2]==pc&&f[3]==lr&&f[4]==0x82);
    CHECK(fault_valid()&&board_fault_code()==exception&&board_fault_pc()==pc);
    CHECK(((volatile uint32_t *)BOOT_REQUEST_ADDRESS)[0]==BOOT_REQUEST_MAGIC);
    CHECK(((volatile uint32_t *)BOOT_REQUEST_ADDRESS)[1]==~BOOT_REQUEST_MAGIC);
}
static void fault_tests(void)
{
    static const uint32_t bad[]={0,0x1000,0x1FFFFFFC,0x20000001,0x2001FFC4,FAULT_ADDRESS,0x20020000,0xFFFFFFFF};
    static const uint32_t good[]={0x20000000,0x20008000,FAULT_ADDRESS-32};
    volatile uint32_t *f=(volatile uint32_t *)FAULT_ADDRESS;
    unsigned i;uint32_t *frame;
    setup();CHECK(!fault_valid()&&!board_fault_code()&&!board_fault_pc());
    for(i=0;i<sizeof(good)/sizeof(good[0]);i++){
        frame=(uint32_t *)good[i];frame[5]=0x08009001+i*4;frame[6]=0x0800A001+i*4;
        invoke_fault(frame,3+i,frame[6],frame[5]);
    }
    for(i=0;i<sizeof(bad)/sizeof(bad[0]);i++)invoke_fault((uint32_t *)bad[i],6,0,0);
    CHECK(board_consume_bootloader_request());CHECK(!board_consume_bootloader_request());CHECK(board_fault_code()==6&&fault_valid());
    for(i=0;i<6;i++){f[i]^=1;CHECK(!fault_valid()&&!board_fault_code()&&!board_fault_pc());f[i]^=1;CHECK(fault_valid());}
}
static void off_tests(void)
{
    unsigned usb;
    for(usb=0;usb<2;usb++){
        setup();board_ms=123;GPIOC->IDATA=GPIO_PIN_4;off_hook=1;
        if(!setjmp(off_jump)){board_power_off((bool)usb);CHECK(0);}
        CHECK(detach_calls==usb&&clear_calls==1&&feed_calls==1);
        CHECK(GPIOC->BC==GPIO_PIN_5&&!GPIOC->BSC&&board_ms==123&&!delay_total&&!reset_calls);
    }
    setup();usb_disconnect();CHECK(detach_calls==1&&delay_total==300&&board_ms==300);
}
int main(void)
{
    void *ram=VirtualAlloc((void *)0x20000000,128*1024,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(ram==(void *)0x20000000);memset(ram,0xA5,128*1024);
    setvbuf(stdout,0,_IONBF,0);watchdog_tests();fault_tests();off_tests();CHECK(VirtualFree(ram,0,MEM_RELEASE));
#ifdef KLQ_DEMO_APP
    printf("RESULT PASS app assertions=%u\n",checks);
#else
    printf("RESULT PASS boot-extract assertions=%u\n",checks);
#endif
    return 0;
}
