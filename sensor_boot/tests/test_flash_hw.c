/* Actual boot_hw.c, except its unexecuted ARM MSP/BX routine, runs in this process. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sensor_hw.h"
#include "boot_hw.h"

MockFlashRegisters mock_flash;
MockRCC mock_rcc;
MockSysTick mock_systick;
MockUSART mock_usart;
MockNVIC mock_nvic;
MockSCB mock_scb;
uint32_t mock_primask;
static uint32_t sr,cr,op_address,op_error,sr_reads;
static unsigned pending,operations,unlock_stage,blocked_unlock,force_busy,busy_after,corrupt_readback;
static unsigned checks;
static uint8_t saved_page[128];
static char trace[128];
static unsigned trace_n;
#define CHECK(n) do {++checks;if(!(n)){printf("FAIL %s:%u %s\n",__FILE__,__LINE__,#n);exit(1);}}while(0)
static void event(char ch) { CHECK(trace_n+1U<sizeof(trace));trace[trace_n++]=ch;trace[trace_n]=0; }

uint32_t mock_read(unsigned id)
{
    if(id==1U)return cr;
    if(id==2U)return 0U;
    ++sr_reads;
    if(force_busy)return FLASH_SR_BSY;
    if(pending) {
        uint8_t *p=(uint8_t *)op_address;
        if(busy_after)return FLASH_SR_BSY;
        if(cr&FLASH_CR_PER)CHECK(*(uint32_t *)p==0xFFU);
        if(cr&FLASH_CR_PG)CHECK(cr&FLASH_CR_PGSTRT);
        if(op_error)memcpy(p,saved_page,sizeof(saved_page));
        else if(cr&FLASH_CR_PER)memset(p,0xFF,128);
        if(corrupt_readback)p[17]^=1U;
        sr=op_error;
        if(!op_error&&(cr&FLASH_CR_EOPIE))sr|=FLASH_SR_EOP;
        pending=0;operations++;event('D');
    }
    return sr;
}

void mock_write(unsigned id,uint32_t value)
{
    if(id==0U) {
        if((sr&FLASH_SR_EOP)&&(value&FLASH_SR_EOP))event('C');
        sr&=~value;return;
    }
    if(id==2U) {
        if(value==FLASH_KEY1)unlock_stage=1;
        else if(value==FLASH_KEY2&&unlock_stage==1U) {
            if(!blocked_unlock)cr&=~FLASH_CR_LOCK;
            unlock_stage=0;event('U');
        } else CHECK(0);
        return;
    }
    if(!(cr&FLASH_CR_EOPIE)&&(value&FLASH_CR_EOPIE))event('E');
    if((cr&FLASH_CR_EOPIE)&&!(value&FLASH_CR_EOPIE)) {
        CHECK(!(sr&FLASH_SR_EOP));event('e');
    }
    if((value&(FLASH_CR_PER|FLASH_CR_PG))&&!(cr&(FLASH_CR_PER|FLASH_CR_PG))) {
        CHECK(!(value&FLASH_CR_LOCK));
        memcpy(saved_page,(void *)op_address,sizeof(saved_page));
        pending=1;event((value&FLASH_CR_PER)?'A':'P');
        if(value&FLASH_CR_PG)CHECK(mock_primask==1U);
    }
    if(!(cr&FLASH_CR_PGSTRT)&&(value&FLASH_CR_PGSTRT))event('S');
    if(!(cr&FLASH_CR_LOCK)&&(value&FLASH_CR_LOCK)) {
        CHECK(!(value&(FLASH_CR_EOPIE|FLASH_CR_PER|FLASH_CR_PG)));
        CHECK(!(sr&FLASH_SR_EOP));event('L');
    }
    cr=value;
}

void sensor_hw_init(void) {}
uint8_t sensor_hw_uart_get(uint8_t *b) { (void)b;return 0U; }
uint8_t sensor_hw_uart_put(uint8_t b) { (void)b;return 1U; }
uint8_t sensor_hw_uart_drain(void) { return 1U; }

#include "boot_hw_source.c"

static void reset_model(uint32_t address)
{
    memset((void *)SENSOR_FLASH_BASE,0xA5,0x6000);
    sr=0;cr=FLASH_CR_LOCK;op_address=address;op_error=sr_reads=0;
    pending=operations=unlock_stage=blocked_unlock=force_busy=busy_after=corrupt_readback=0;
    mock_primask=0;trace_n=0;trace[0]=0;
    mock_rcc.ICSCR=4UL<<13;
}
static void clean_and_order(const char *operation)
{
    const char *e=strchr(trace,'E'),*o=strchr(trace,*operation),*d=strchr(trace,'D');
    const char *c=strchr(trace,'C'),*off=strchr(trace,'e'),*lock=strchr(trace,'L');
    CHECK(e&&o&&d&&c&&off&&lock&&e<=o&&o<d&&d<c&&c<off&&off<lock);
    CHECK((cr&(FLASH_CR_LOCK|FLASH_CR_EOPIE|FLASH_CR_PER|FLASH_CR_PG))==FLASH_CR_LOCK);
    CHECK(!(sr&FLASH_SR_EOP));
}
static void calibrations(void)
{
    CHECK(mock_flash.TS0==0x12U&&mock_flash.TS3==0x34U&&mock_flash.TS1==0x156U);
    CHECK(mock_flash.TS2P==0x78U&&mock_flash.TPS3==0x456U);
    CHECK(mock_flash.PERTPE==0x23456U&&mock_flash.SMERTPE==0x34567U);
    CHECK(mock_flash.PRGTPE==0x5678U&&mock_flash.PRETPE==0x1234U);
}
static void test_old_driver_false_failure(void)
{
    unsigned i;
    reset_model(SENSOR_META_BASE);
    CHECK(!boot_flash_ops.invalidate());
    CHECK(operations==1U&&!strchr(trace,'E'));
    for(i=0;i<128U;i++)CHECK(((uint8_t *)SENSOR_META_BASE)[i]==0xFFU);
    puts("PASS old 1.0.1: invalidate returned failure after metadata was erased; EOPIE missing");
}
static void test_erasure_and_program(void)
{
    uint8_t bytes[48],check[48];unsigned i;
    reset_model(SENSOR_META_BASE);
    CHECK(boot_flash_ops.invalidate());clean_and_order("A");calibrations();
    for(i=0;i<128;i++)CHECK(((uint8_t *)SENSOR_META_BASE)[i]==0xFFU);
    CHECK(((uint8_t *)SENSOR_META_BASE)[-1]==0xA5U);
    reset_model(SENSOR_APP_BASE);
    CHECK(boot_flash_ops.erase_app(128));clean_and_order("A");
    CHECK(((uint8_t *)SENSOR_APP_BASE)[128]==0xA5U);
    reset_model(SENSOR_APP_BASE);memset((void *)SENSOR_APP_BASE,0xFF,128);
    for(i=0;i<sizeof(bytes);i++)bytes[i]=(uint8_t)(i^0x6DU);
    CHECK(boot_flash_ops.write_app(SENSOR_APP_BASE,bytes,sizeof(bytes)));
    clean_and_order("P");CHECK(strchr(trace,'S')&&strchr(trace,'S')<strchr(trace,'D'));CHECK(mock_primask==0U);
    CHECK(boot_flash_ops.read(SENSOR_APP_BASE,check,sizeof(check)));
    CHECK(!memcmp(bytes,check,sizeof(bytes)));
    for(i=sizeof(bytes);i<128U;i++)CHECK(((uint8_t *)SENSOR_APP_BASE)[i]==0xFFU);
    reset_model(SENSOR_META_BASE);mock_primask=1U;
    CHECK(boot_flash_ops.commit(SENSOR_BOARD_ID,SENSOR_HW_COMPAT,272,0x12345678UL));
    clean_and_order("P");CHECK(mock_primask==1U);
    CHECK(((uint32_t *)SENSOR_META_BASE)[0]==(SENSOR_BOARD_ID|((uint32_t)SENSOR_HW_COMPAT<<16)));
    CHECK(((uint32_t *)SENSOR_META_BASE)[1]==272U&&((uint32_t *)SENSOR_META_BASE)[2]==0x12345678UL);
    CHECK(((uint32_t *)SENSOR_META_BASE)[3]==0x314D4C4BUL);
    puts("PASS actual erase/program/metadata, EOP lifecycle, factory timings and PRIMASK restore");
}
static void test_rejections_and_failures(void)
{
    uint8_t bytes[128];uint32_t error;unsigned i;
    memset(bytes,0x55,sizeof(bytes));reset_model(SENSOR_APP_BASE);
    CHECK(!boot_flash_ops.erase_app(0)&&!boot_flash_ops.erase_app(SENSOR_APP_CAPACITY+1));
    CHECK(!boot_flash_ops.write_app(SENSOR_APP_BASE-128,bytes,128));
    CHECK(!boot_flash_ops.write_app(SENSOR_APP_BASE+1,bytes,2));
    CHECK(!boot_flash_ops.write_app(SENSOR_META_BASE,bytes,2));
    CHECK(!boot_flash_ops.write_app(SENSOR_APP_BASE,bytes,0));
    CHECK(!boot_flash_ops.write_app(SENSOR_APP_BASE,bytes,129));
    CHECK(!boot_flash_ops.read(SENSOR_APP_BASE-1,bytes,1));
    CHECK(!boot_flash_ops.read(SENSOR_META_BASE+128,bytes,1));
    CHECK(!boot_flash_ops.commit(SENSOR_BOARD_ID+1,SENSOR_HW_COMPAT,272,0));
    CHECK(operations==0&&sr_reads==0);
    reset_model(SENSOR_META_BASE);mock_rcc.ICSCR=0;
    CHECK(!boot_flash_ops.invalidate()&&operations==0);
    reset_model(SENSOR_META_BASE);blocked_unlock=1;
    CHECK(!boot_flash_ops.invalidate()&&operations==0);
    reset_model(SENSOR_META_BASE);force_busy=1;
    CHECK(!boot_flash_ops.invalidate()&&sr_reads>=4000000UL&&operations==0);
    for(i=0;i<2;i++) {
        error=i?FLASH_SR_OPTVERR:FLASH_SR_WRPERR;
        reset_model(SENSOR_META_BASE);op_error=error;
        CHECK(!boot_flash_ops.invalidate());CHECK(sr&error);CHECK(cr&FLASH_CR_LOCK);
        reset_model(SENSOR_APP_BASE);op_error=error;
        CHECK(!boot_flash_ops.write_app(SENSOR_APP_BASE,bytes,128));CHECK(sr&error);
        CHECK(mock_primask==0U&&!(cr&FLASH_CR_EOPIE)&&(cr&FLASH_CR_LOCK));
    }
    reset_model(SENSOR_META_BASE);corrupt_readback=1;
    CHECK(!boot_flash_ops.invalidate());clean_and_order("A");
    reset_model(SENSOR_APP_BASE);corrupt_readback=1;
    CHECK(!boot_flash_ops.write_app(SENSOR_APP_BASE,bytes,128));clean_and_order("P");
    puts("PASS bounds, clock/unlock/busy failure, WRP/option errors and corrupt readback rejected");
}
int main(void)
{
    uint32_t *timing;
    void *flash=VirtualAlloc((void *)SENSOR_FLASH_BASE,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    void *cal=VirtualAlloc((void *)0x1FFF0000UL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(flash==(void *)SENSOR_FLASH_BASE&&cal==(void *)0x1FFF0000UL);
    timing=(uint32_t *)0x1FFF011CUL;
    timing[0]=0x05586812UL;timing[1]=0x04560078UL;timing[2]=0x23456UL;
    timing[3]=0x34567UL;timing[4]=0x12345678UL;
#ifdef MOCK_OLD_DRIVER
    test_old_driver_false_failure();
#else
    test_erasure_and_program();test_rejections_and_failures();
#endif
    CHECK(VirtualFree(flash,0,MEM_RELEASE)&&VirtualFree(cal,0,MEM_RELEASE));
    printf("Flash hardware contract: PASS %u checks, local process simulation only\n",checks);
    return 0;
}
