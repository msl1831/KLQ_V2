#include "boot_hw.h"
#include "motor_hw.h"
#define MOTOR_META_MAGIC 0x314D4C4BUL
#define FLASH_WAIT_LIMIT 4000000UL
static uint32_t page[32];
static uint16_t rd16(const uint8_t *p)
{ return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }
static uint32_t rd32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint32_t image_crc(uint16_t length)
{
    const uint8_t *p = (const uint8_t *)MOTOR_APP_BASE;
    uint32_t c = 0xFFFFFFFFUL;
    uint16_t i;
    uint8_t b;
    for (i=0U; i<length; ++i) {
        c ^= p[i];
        for (b=0U; b<8U; ++b) c=(c>>1)^((0UL-(c&1UL))&0xEDB88320UL);
    }
    return ~c;
}
static uint8_t flash_wait(void)
{
    uint32_t n=FLASH_WAIT_LIMIT;
    while ((FLASH->SR & FLASH_SR_BSY) && n--) { }
    return !(FLASH->SR & (FLASH_SR_BSY|FLASH_SR_WRPERR|FLASH_SR_OPTVERR));
}
static uint8_t flash_start(void)
{
    const uint32_t *t=(const uint32_t *)0x1FFF011CUL;
    uint32_t n=FLASH_WAIT_LIMIT;
    while ((FLASH->SR & FLASH_SR_BSY) && n--) { }
    if ((FLASH->SR & FLASH_SR_BSY) ||
        ((RCC->ICSCR & RCC_ICSCR_HSI_FS)>>RCC_ICSCR_HSI_FS_Pos)!=4U) return 0U;
    if (FLASH->CR & FLASH_CR_LOCK) { FLASH->KEYR=FLASH_KEY1; FLASH->KEYR=FLASH_KEY2; }
    if (FLASH->CR & FLASH_CR_LOCK) return 0U;
    /* Puya HAL timing table for 24 MHz HSI; use factory calibration. */
    FLASH->TS0=t[0]&0x1FFU; FLASH->TS1=(t[0]>>18)&0x3FFU;
    FLASH->TS3=(t[0]>>9)&0x1FFU; FLASH->TS2P=t[1]&0x1FFU;
    FLASH->TPS3=(t[1]>>16)&0xFFFU; FLASH->PERTPE=t[2]&0x3FFFFUL;
    FLASH->SMERTPE=t[3]&0x3FFFFUL; FLASH->PRGTPE=t[4]&0xFFFFU;
    FLASH->PRETPE=(t[4]>>16)&0x3FFFU;
    FLASH->SR=FLASH_SR_EOP|FLASH_SR_WRPERR|FLASH_SR_OPTVERR;
    return 1U;
}
static uint8_t erase_page(uint32_t addr)
{
    uint8_t ok;
    uint16_t i;
    if (addr<MOTOR_APP_BASE || addr>MOTOR_META_BASE ||
        (addr & (MOTOR_FLASH_PAGE_SIZE-1U)) || !flash_start()) return 0U;
    FLASH->CR |= FLASH_CR_PER;
    *(__IO uint32_t *)addr=0xFFU;
    ok=flash_wait() && (FLASH->SR & FLASH_SR_EOP);
    FLASH->CR &= ~FLASH_CR_PER; FLASH->CR |= FLASH_CR_LOCK;
    for (i=0U; i<MOTOR_FLASH_PAGE_SIZE; ++i)
        if (((const uint8_t *)addr)[i]!=0xFFU) ok=0U;
    return ok;
}
static uint8_t program_page(uint32_t addr)
{
    uint8_t i,ok;
    uint32_t mask;
    if (addr<MOTOR_APP_BASE || addr>MOTOR_META_BASE ||
        (addr & (MOTOR_FLASH_PAGE_SIZE-1U)) || !flash_start()) return 0U;
    mask=__get_PRIMASK(); __disable_irq();
    FLASH->CR |= FLASH_CR_PG;
    /* PGSTRT is set immediately before the final word of a 128 B page. */
    for (i=0U; i<32U; ++i) {
        if (i==31U) FLASH->CR |= FLASH_CR_PGSTRT;
        ((__IO uint32_t *)addr)[i]=page[i];
    }
    ok=flash_wait() && (FLASH->SR & FLASH_SR_EOP);
    FLASH->CR &= ~FLASH_CR_PG; FLASH->CR |= FLASH_CR_LOCK;
    __set_PRIMASK(mask);
    for (i=0U; i<32U; ++i) if (((const uint32_t *)addr)[i]!=page[i]) ok=0U;
    return ok;
}
static int flash_read(uint32_t addr,uint8_t *data,uint16_t n)
{
    uint16_t i;
    if (addr<MOTOR_APP_BASE || addr>MOTOR_META_BASE+MOTOR_FLASH_PAGE_SIZE ||
        n>MOTOR_META_BASE+MOTOR_FLASH_PAGE_SIZE-addr) return 0;
    for (i=0U; i<n; ++i) data[i]=((const uint8_t *)addr)[i];
    return 1;
}
static int flash_invalidate(void) { return erase_page(MOTOR_META_BASE); }
static int flash_erase_app(uint32_t length)
{
    uint32_t a;
    if (!length || length>MOTOR_APP_CAPACITY) return 0;
    for (a=MOTOR_APP_BASE; a<MOTOR_APP_BASE+length; a+=MOTOR_FLASH_PAGE_SIZE)
        if (!erase_page(a)) return 0;
    return 1;
}
static int flash_write_app(uint32_t addr,const uint8_t *p,uint16_t n)
{
    uint16_t i;
    if (!n || n>MOTOR_FLASH_PAGE_SIZE || addr<MOTOR_APP_BASE ||
        addr>=MOTOR_META_BASE || n>MOTOR_META_BASE-addr ||
        (addr & (MOTOR_FLASH_PAGE_SIZE-1U))) return 0;
    for (i=0U; i<32U; ++i) page[i]=0xFFFFFFFFUL;
    for (i=0U; i<n; ++i) ((uint8_t *)page)[i]=p[i];
    return program_page(addr);
}
static int flash_commit(uint16_t board,uint16_t hw,uint16_t length,uint32_t crc)
{
    uint8_t i;
    if (board!=MOTOR_BOARD_ID || hw!=MOTOR_HW_COMPAT || length<8U || length>MOTOR_APP_CAPACITY) return 0;
    for (i=0U; i<32U; ++i) page[i]=0xFFFFFFFFUL;
    page[0]=(uint32_t)board|((uint32_t)hw<<16);
    page[1]=length; page[2]=crc; page[3]=MOTOR_META_MAGIC;
    return program_page(MOTOR_META_BASE);
}
const motor_flash_ops_t boot_flash_ops={flash_read,flash_invalidate,flash_erase_app,flash_write_app,flash_commit};
uint8_t boot_image_valid(void)
{
    const uint8_t *m=(const uint8_t *)MOTOR_META_BASE,*a=(const uint8_t *)MOTOR_APP_BASE;
    uint16_t len=rd16(m+4U);
    uint32_t sp=rd32(a),pc=rd32(a+4U);
    if (rd32(m+12U)!=MOTOR_META_MAGIC || rd16(m)!=MOTOR_BOARD_ID ||
        rd16(m+2U)!=MOTOR_HW_COMPAT || len<8U || len>MOTOR_APP_CAPACITY ||
        (len&1U) || (sp&7U) || sp<=0x20000000UL || sp>MOTOR_RAM_END ||
        !(pc&1U) || (pc&~1UL)<MOTOR_APP_BASE || (pc&~1UL)>=MOTOR_APP_BASE+len) return 0U;
    return image_crc(len)==rd32(m+8U);
}
void boot_hw_init(void) { motor_hw_init(); }
uint8_t boot_uart_get(uint8_t *b) { return motor_hw_uart_get(b); }
uint8_t boot_uart_put(uint8_t b) { return motor_hw_uart_put(b); }
void boot_uart_drain(void) { motor_hw_uart_drain(); }
__asm static void jump(uint32_t sp,uint32_t pc)
{
    MSR MSP,r0
    BX r1
}
void boot_jump_app(void)
{
    uint32_t sp=*(const uint32_t *)MOTOR_APP_BASE,pc=*(const uint32_t *)(MOTOR_APP_BASE+4U);
    __disable_irq(); SysTick->CTRL=0U; USART1->CR1=0U;
    NVIC->ICER[0]=0xFFFFFFFFUL; NVIC->ICPR[0]=0xFFFFFFFFUL;
    SCB->ICSR=SCB_ICSR_PENDSTCLR_Msk|SCB_ICSR_PENDSVCLR_Msk;
    SCB->VTOR=MOTOR_APP_BASE; __DSB(); __ISB();
    jump(sp,pc);
    for (;;) { }
}
