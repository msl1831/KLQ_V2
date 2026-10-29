#include <assert.h>
#include <string.h>
#include "../firmware/boot/update.h"
static uint8_t flash[0x6000],written[0x6000/128];
static uint8_t valid,fail_write,fail_invalidate,fail_commit;
static unsigned int writes;
static int flash_read(uint32_t a,uint8_t *p,uint16_t n)
{
    if (a<MOTOR_FLASH_BASE || a-MOTOR_FLASH_BASE+n>sizeof(flash)) return 0;
    memcpy(p,flash+a-MOTOR_FLASH_BASE,n); return 1;
}
static int invalidate(void)
{
    if (fail_invalidate) return 0;
    memset(flash+MOTOR_META_BASE-MOTOR_FLASH_BASE,0xFF,128U);
    valid=0; return 1;
}
static int erase_app(uint32_t n)
{
    if (!n || n>MOTOR_APP_CAPACITY) return 0;
    memset(flash+MOTOR_APP_BASE-MOTOR_FLASH_BASE,0xFF,(n+127U)&~127UL);
    memset(written,0,sizeof(written)); return 1;
}
static int write_app(uint32_t a,const uint8_t *p,uint16_t n)
{
    uint32_t index=(a-MOTOR_FLASH_BASE)/128U;
    if (fail_write || a<MOTOR_APP_BASE || a+n>MOTOR_META_BASE) return 0;
    assert(!(a&127U) && n<=128U && !written[index]);
    written[index]=1; ++writes;
    memcpy(flash+a-MOTOR_FLASH_BASE,p,n); return 1;
}
static int commit(uint16_t board,uint16_t hw,uint16_t n,uint32_t crc)
{
    if (fail_commit || board!=MOTOR_BOARD_ID || hw!=MOTOR_HW_COMPAT || n!=144U || !crc) return 0;
    valid=1; return 1;
}
static uint32_t crc32(const uint8_t *p,uint16_t n)
{
    uint32_t c=0xFFFFFFFFUL;
    uint16_t i;
    uint8_t b;
    for(i=0;i<n;++i) {
        c^=p[i];
        for(b=0;b<8U;++b) c=(c>>1)^((0UL-(c&1UL))&0xEDB88320UL);
    }
    return ~c;
}
static void wr32(uint8_t *p,uint32_t n)
{ p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); p[2]=(uint8_t)(n>>16); p[3]=(uint8_t)(n>>24); }
int main(void)
{
    uint8_t app[144];
    uint32_t crc;
    motor_update_t u;
    motor_flash_ops_t io={flash_read,invalidate,erase_app,write_app,commit};
    memset(app,0xA5,sizeof(app)); wr32(app,MOTOR_RAM_END); wr32(app+4U,MOTOR_APP_BASE+9U);
    crc=crc32(app,sizeof(app)); memset(flash,0xFF,sizeof(flash));
    motor_update_init(&u,&io);
    assert(motor_update_begin(&u,99U,MOTOR_HW_COMPAT,144U,crc)==2U);
    assert(motor_update_begin(&u,MOTOR_BOARD_ID,1U,144U,crc)==2U);
    assert(motor_update_begin(&u,MOTOR_BOARD_ID,MOTOR_HW_COMPAT,MOTOR_APP_CAPACITY+2U,crc)==2U);
    assert(motor_update_begin(&u,MOTOR_BOARD_ID,MOTOR_HW_COMPAT,143U,crc)==2U);
    assert(motor_update_begin(&u,MOTOR_BOARD_ID,MOTOR_HW_COMPAT,144U,crc)==0U);
    assert(!valid);
    assert(motor_update_data(&u,128U,app+128U,16U)==2U);
    assert(motor_update_data(&u,0U,app,96U)==2U);
    assert(motor_update_data(&u,0U,app,128U)==0U);
    assert(motor_update_data(&u,0U,app,128U)==0U && writes==1U);
    assert(motor_update_begin(&u,MOTOR_BOARD_ID,MOTOR_HW_COMPAT,144U,crc)==0U && u.next_offset==128U);
    assert(motor_update_end(&u)==2U);
    fail_write=1;
    assert(motor_update_data(&u,128U,app+128U,16U)==6U && !u.active);
    fail_write=0;
    assert(motor_update_data(&u,128U,app+128U,16U)==4U);
    assert(motor_update_begin(&u,MOTOR_BOARD_ID,MOTOR_HW_COMPAT,144U,crc)==0U);
    assert(motor_update_data(&u,0U,app,128U)==0U);
    assert(motor_update_data(&u,128U,app+128U,16U)==0U);
    assert(motor_update_end(&u)==0U && valid && u.complete);
    assert(motor_update_end(&u)==0U);
    fail_invalidate=1;
    assert(motor_update_begin(&u,MOTOR_BOARD_ID,MOTOR_HW_COMPAT,144U,crc)==6U && valid && !u.active);
    fail_invalidate=0;
    assert(motor_update_begin(&u,MOTOR_BOARD_ID,MOTOR_HW_COMPAT,144U,crc)==0U && !valid);
    motor_update_init(&u,&io); /* Power loss before commit leaves no valid application. */
    assert(!valid && motor_update_end(&u)==4U);
    assert(motor_update_begin(&u,MOTOR_BOARD_ID,MOTOR_HW_COMPAT,144U,crc)==0U);
    assert(motor_update_data(&u,0U,app,128U)==0U);
    assert(motor_update_data(&u,128U,app+128U,16U)==0U);
    flash[MOTOR_APP_BASE-MOTOR_FLASH_BASE+143U]^=1U;
    assert(motor_update_end(&u)==2U && !valid && !u.active);
    assert(motor_update_begin(&u,MOTOR_BOARD_ID,MOTOR_HW_COMPAT,144U,crc)==0U && u.next_offset==0U);
    assert(motor_update_data(&u,0U,app,128U)==0U);
    assert(motor_update_data(&u,128U,app+128U,16U)==0U);
    fail_commit=1;
    assert(motor_update_end(&u)==6U && !valid && !u.active);
    fail_commit=0;
    wr32(app,MOTOR_RAM_END+8U); crc=crc32(app,sizeof(app));
    assert(motor_update_begin(&u,MOTOR_BOARD_ID,MOTOR_HW_COMPAT,144U,crc)==0U);
    assert(motor_update_data(&u,0U,app,128U)==0U);
    assert(motor_update_data(&u,128U,app+128U,16U)==0U);
    assert(motor_update_end(&u)==2U && !valid);
    return 0;
}
