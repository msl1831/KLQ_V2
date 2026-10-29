#include <assert.h>
#include <string.h>
#include <stdio.h>
#ifndef BOOT_SIMULATOR
#include <setjmp.h>
#endif
#ifdef BOOT_SIMULATOR
#include <io.h>
#include <fcntl.h>
#endif
#define main boot_firmware_main
#include "../src/main.c"
#undef main

uint32_t mock_uid[3]={0x12345678UL,0x34567890UL,0x567890ABUL};
static uint8_t sent[148],flash[0x6000],valid;
static uint16_t sent_size;
static unsigned jumps,writes,commits;
static uint16_t fail_after=0xFFFFU;
static uint8_t drain_ok=1U;
static unsigned drains;
#ifndef BOOT_SIMULATOR
static jmp_buf poll_escape;
static uint8_t poll_mode,poll_bytes[16];
static unsigned poll_pos,poll_idle;
#endif
void boot_hw_init(void) {}
uint8_t boot_uart_get(uint8_t *b)
{
#ifndef BOOT_SIMULATOR
    if(poll_mode) {
        ms=600U;
        if(poll_mode==1U && poll_pos<16U) { *b=poll_bytes[poll_pos++]; return 1U; }
        if(++poll_idle==4U) longjmp(poll_escape,1);
    }
#else
    (void)b;
#endif
    return 0U;
}
uint8_t boot_uart_put(uint8_t b)
{
    if(sent_size==fail_after) return 0U;
    assert(sent_size<sizeof(sent)); sent[sent_size++]=b; return 1U;
}
uint8_t boot_uart_drain(void) { ++drains; return drain_ok; }
uint8_t boot_image_valid(void) { return valid; }
void boot_jump_app(void)
{
    ++jumps;
#ifndef BOOT_SIMULATOR
    if(poll_mode) longjmp(poll_escape,2);
#endif
}
static int read_flash(uint32_t a,uint8_t *p,uint16_t n)
{ assert(a>=SENSOR_APP_BASE && a+n<=SENSOR_META_BASE); memcpy(p,flash+a-SENSOR_FLASH_BASE,n); return 1; }
static int invalidate(void) { valid=0; return 1; }
static int erase_app(uint32_t n)
{ assert(n<=SENSOR_APP_CAPACITY); memset(flash+SENSOR_BOOT_SIZE,0xFF,(n+127U)&~127UL); return 1; }
static int write_app(uint32_t a,const uint8_t *p,uint16_t n)
{ assert(a>=SENSOR_APP_BASE && a+n<=SENSOR_META_BASE); memcpy(flash+a-SENSOR_FLASH_BASE,p,n); ++writes; return 1; }
static int commit(uint16_t b,uint16_t h,uint16_t n,uint32_t crc)
{ assert(b==SENSOR_BOARD_ID && h==SENSOR_HW_COMPAT && n>=8U && n<=SENSOR_APP_CAPACITY); (void)crc; valid=1; ++commits; return 1; }
const sensor_flash_ops_t boot_flash_ops={read_flash,invalidate,erase_app,write_app,commit};
static void command(uint16_t cmd,const uint8_t *p,uint16_t n,uint32_t status)
{
    uint8_t f[148];
    uint16_t i;
    memcpy(f,"KLQ1",4U); wr32(f+4U,0x12345678UL); wr16(f+8U,cmd); wr16(f+10U,n);
    if(n) memcpy(f+16U,p,n);
    wr32(f+12U,frame_crc(f,(uint16_t)(16U+n)));
    sent_size=0;
    for(i=0U;i<16U+n;++i) receive(f[i]);
    assert(sent_size>=20U && rd32(sent+4U)==0x12345678UL);
    assert(rd16(sent+8U)==(cmd|RESPONSE_BIT) && rd32(sent+16U)==status);
    assert(rd32(sent+12U)==frame_crc(sent,sent_size));
}
#ifndef BOOT_SIMULATOR
static void run_tx_boundary(uint16_t limit,uint8_t tc)
{
    memcpy(poll_bytes,"KLQ1",4U); wr32(poll_bytes+4U,7U);
    wr16(poll_bytes+8U,0x0203U); wr16(poll_bytes+10U,0U);
    wr32(poll_bytes+12U,frame_crc(poll_bytes,16U));
    valid=1U; hold=image_ok=0U; used=expected=sent_size=0U; ms=0U;
    jumps=drains=poll_pos=poll_idle=0U; fail_after=limit; drain_ok=tc; poll_mode=1U;
    if(setjmp(poll_escape)==0) boot_firmware_main();
    poll_mode=0U;
    assert(hold && image_ok && valid && ms>=500U);
    if(limit<20U) {
        assert(!jumps && !drains && sent_size==limit);
    } else {
        assert(sent_size==20U && drains==1U && jumps==(tc?1U:0U));
    }
    if(!jumps) {
        fail_after=0xFFFFU; drain_ok=1U; drains=0U;
        command(0x0203U,0,0U,0U);
        assert(jumps==1U && drains==1U);
    }
}
static void default_boot_boundary(uint8_t app_valid)
{
    valid=app_valid; hold=image_ok=0U; used=expected=sent_size=0U; ms=0U;
    jumps=drains=poll_idle=0U; poll_mode=2U;
    if(setjmp(poll_escape)==0) boot_firmware_main();
    poll_mode=0U;
    assert(!hold && jumps==(app_valid?1U:0U) && !sent_size && !drains);
}
int main(void)
{
    uint8_t app[240],p[132],f[16];
    uint32_t crc=0xFFFFFFFFUL;
    uint16_t i;
    sensor_update_init(&update,&boot_flash_ops);
    command(1U,0,0U,0U);
    assert(sent_size==56U && sent[22U]==0U && sent[23U]==SENSOR_DEVICE_CLASS);
    assert(rd16(sent+24U)==SENSOR_BOARD_ID && rd16(sent+26U)==SENSOR_HW_COMPAT);
    assert(rd16(sent+28U)==SENSOR_DEVICE_TYPE && rd32(sent+44U)==mock_uid[0]);
    assert(rd32(sent+32U)==0x00010002UL);
    command(0x0204U,0,0U,0U);
    assert(sent_size==36U && rd32(sent+20U)==SENSOR_APP_BASE);
    assert(rd16(sent+24U)==SENSOR_APP_CAPACITY && rd16(sent+26U)==128U);
    command(0x0203U,0,0U,6U); assert(!jumps);
    command(0x0009U,0,0U,0U); assert(hold);
    command(0x0200U,p,9U,2U); assert(!update.active);
    memset(app,0xA5,sizeof(app)); wr32(app,SENSOR_RAM_END); wr32(app+4U,SENSOR_APP_BASE+9U);
    wr32(app+SENSOR_ID_OFFSET,0x3144494BUL); wr32(app+SENSOR_ID_OFFSET+4U,SENSOR_BOARD_ID|((uint32_t)SENSOR_HW_COMPAT<<16));
    wr32(app+SENSOR_ID_OFFSET+8U,SENSOR_APP_BASE); wr32(app+SENSOR_ID_OFFSET+12U,1U);
    for(i=0U;i<sizeof(app);++i) crc=crc_step(crc,app[i]);
    wr16(p,SENSOR_BOARD_ID+1U); wr16(p+2U,SENSOR_HW_COMPAT); wr16(p+4U,sizeof(app)); wr32(p+6U,~crc);
    command(0x0200U,p,10U,2U); assert(!writes && !commits);
    wr16(p,SENSOR_BOARD_ID); command(0x0200U,p,10U,0U);
    wr16(p,0U); memcpy(p+2U,app,128U); command(0x0201U,p,130U,0U);
    command(0x0201U,p,130U,0U); assert(writes==1U);
    wr16(p,128U); memcpy(p+2U,app+128U,112U); command(0x0201U,p,114U,0U);
    command(0x0202U,0,0U,0U); command(0x0202U,0,0U,0U); assert(valid && commits==1U);
    command(0x0203U,0,0U,0U); assert(jumps==1U);
    memcpy(f,"KLQ1",4U); wr32(f+4U,7U); wr16(f+8U,1U); wr16(f+10U,0U); wr32(f+12U,frame_crc(f,16U)^1U);
    sent_size=0; for(i=0U;i<16U;++i) receive(f[i]); assert(!sent_size);
    wr16(f+10U,133U); for(i=0U;i<16U;++i) receive(f[i]); assert(!used && !sent_size);
    command(0x7777U,0,0U,1U); command(1U,p,1U,2U);
    for(i=0U;i<20U;++i) run_tx_boundary(i,1U);
    run_tx_boundary(0xFFFFU,0U);
    run_tx_boundary(0xFFFFU,1U);
    default_boot_boundary(1U);
    default_boot_boundary(0U);
    printf("boot parser/protocol PASS, board=%u\n",SENSOR_BOARD_ID);
    return 0;
}
#else
int main(void)
{
    uint8_t header[2];
    unsigned i,n;
    int byte;
    _setmode(_fileno(stdin),_O_BINARY); _setmode(_fileno(stdout),_O_BINARY);
    sensor_update_init(&update,&boot_flash_ops);
    while (fread(header,1,2,stdin)==2U) {
        n=(unsigned)header[0]|((unsigned)header[1]<<8);
        if(n>148U) return 2;
        sent_size=0U;
        for(i=0U;i<n;++i) {
            byte=getchar(); if(byte==EOF) return 3;
            receive((uint8_t)byte);
        }
        header[0]=(uint8_t)sent_size; header[1]=(uint8_t)(sent_size>>8);
        fwrite(header,1,2,stdout); fwrite(sent,1,sent_size,stdout); fflush(stdout);
    }
    return 0;
}
#endif
