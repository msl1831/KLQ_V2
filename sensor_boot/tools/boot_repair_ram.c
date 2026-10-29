#include "py32f0xx.h"

#define REPAIR_MAGIC 0x4B425231UL
#define CONTROL ((volatile uint32_t *)0x20000800UL)
#define DATA ((const uint32_t *)0x20000820UL)
#define WAIT_LIMIT 4000000UL
#define FLAGS (FLASH_SR_EOP|FLASH_SR_WRPERR|FLASH_SR_OPTVERR|FLASH_SR_BSY)

static uint32_t ready(void)
{
    uint32_t n=WAIT_LIMIT;
    while ((FLASH->SR & FLASH_SR_BSY) && --n) { }
    return !(FLASH->SR & FLASH_SR_BSY);
}

static uint32_t unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR=FLASH_KEY1; FLASH->KEYR=FLASH_KEY2;
    }
    if (FLASH->CR & FLASH_CR_LOCK) return 0U;
    FLASH->CR &= ~(FLASH_CR_PG|FLASH_CR_PER|FLASH_CR_EOPIE);
    FLASH->SR=FLASH_SR_EOP|FLASH_SR_WRPERR|FLASH_SR_OPTVERR;
    FLASH->CR |= FLASH_CR_EOPIE;
    return 1U;
}

static uint32_t complete(void)
{
    return ready() && !(FLASH->SR & (FLASH_SR_WRPERR|FLASH_SR_OPTVERR)) &&
           (FLASH->SR & FLASH_SR_EOP);
}

static void lock(void)
{
    FLASH->SR=FLASH_SR_EOP;
    FLASH->CR &= ~(FLASH_CR_PG|FLASH_CR_PER|FLASH_CR_EOPIE);
    FLASH->CR |= FLASH_CR_LOCK;
}

/* ENTRY is first: no vectors, startup, data initialization, heap or library calls. */
__attribute__((section("ENTRY"),used,noreturn)) void boot_repair_ram(void)
{
    const uint32_t *t=(const uint32_t *)0x1FFF011CUL;
    uint32_t address,i,error=0U;
    __disable_irq();
    CONTROL[2]=0xFFFFFFFFUL; CONTROL[3]=1U;
    if (CONTROL[0]!=REPAIR_MAGIC) { CONTROL[2]=1U; goto stop; }
    CONTROL[0]=0U;
    address=CONTROL[1];
    if (address<0x08000000UL || address>=0x08001800UL || (address&127UL)) {
        CONTROL[2]=2U; goto stop;
    }
    if (__get_IPSR()!=0U) { CONTROL[2]=11U; goto stop; }
    if (FLASH->CR & (FLASH_CR_MER|FLASH_CR_SER)) { CONTROL[2]=10U; goto stop; }
    CONTROL[3]=2U;
    if (!ready()) { error=4U; goto fail; }
    if (((RCC->ICSCR & RCC_ICSCR_HSI_FS)>>RCC_ICSCR_HSI_FS_Pos)!=4U) {
        CONTROL[2]=3U; goto stop;
    }
    CONTROL[3]=3U;
    if (!unlock()) { error=5U; goto fail; }
    CONTROL[3]=4U;
    FLASH->TS0=t[0]&0x1FFU; FLASH->TS1=(t[0]>>18)&0x3FFU;
    FLASH->TS3=(t[0]>>9)&0x1FFU; FLASH->TS2P=t[1]&0x1FFU;
    FLASH->TPS3=(t[1]>>16)&0xFFFU; FLASH->PERTPE=t[2]&0x3FFFFUL;
    FLASH->SMERTPE=t[3]&0x3FFFFUL; FLASH->PRGTPE=t[4]&0xFFFFU;
    FLASH->PRETPE=(t[4]>>16)&0x3FFFU;
    CONTROL[3]=5U;
    FLASH->CR |= FLASH_CR_PER;
    *(__IO uint32_t *)address=0xFFU;
    if (!complete()) { error=6U; goto fail; }
    lock();
    CONTROL[3]=6U;
    for (i=0U;i<32U;++i)
        if (((const uint32_t *)address)[i]!=0xFFFFFFFFUL) { error=7U; goto fail; }
    CONTROL[3]=7U;
    if (!unlock()) { error=5U; goto fail; }
    FLASH->CR |= FLASH_CR_PG;
    for (i=0U;i<32U;++i) {
        if (i==31U) FLASH->CR |= FLASH_CR_PGSTRT;
        ((__IO uint32_t *)address)[i]=DATA[i];
    }
    if (!complete()) { error=8U; goto fail; }
    lock();
    CONTROL[3]=8U;
    for (i=0U;i<32U;++i)
        if (((const uint32_t *)address)[i]!=DATA[i]) { error=9U; goto fail; }
    CONTROL[2]=0U; CONTROL[3]=9U;
    goto stop;
fail:
    CONTROL[2]=error|((FLASH->SR&FLAGS)<<8);
    lock();
stop:
    __DSB(); __ISB(); __BKPT(0);
    for (;;) { }
}
