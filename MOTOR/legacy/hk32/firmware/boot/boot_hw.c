#include "boot_hw.h"
#include "hk32f030m.h"
#include "system_hk32f030m.h"

#define MOTOR_META_MAGIC 0x314D4C4BUL /* KLM1 */
#define FLASH_WAIT_LIMIT 4000000UL

/* Required by the vendor SystemCoreClockUpdate implementation. */
const uint16_t AHBPrescTable[16] = {1, 6, 1, 1, 1, 1, 1, 1,
                                    2, 4, 8, 16, 64, 128, 256, 512};

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t image_crc(uint16_t length)
{
    const uint8_t *p = (const uint8_t *)MOTOR_APP_BASE;
    uint32_t crc = 0xFFFFFFFFUL;
    uint16_t i;
    uint8_t bit;
    for (i = 0U; i < length; ++i) {
        crc ^= p[i];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((0UL - (crc & 1UL)) & 0xEDB88320UL);
    }
    return ~crc;
}

static uint8_t flash_wait(void)
{
    uint32_t n = FLASH_WAIT_LIMIT;
    while ((FLASH->SR & FLASH_SR_BSY) && n--) { }
    return !(FLASH->SR & (FLASH_SR_BSY | FLASH_SR_WRPRTERR));
}

static uint8_t flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = FLASH_FKEY1;
        FLASH->KEYR = FLASH_FKEY2;
    }
    return !(FLASH->CR & FLASH_CR_LOCK);
}

static uint8_t erase_page(uint32_t addr)
{
    uint8_t ok;
    if (addr < MOTOR_APP_BASE || addr > MOTOR_META_BASE ||
        (addr & (MOTOR_FLASH_PAGE_SIZE - 1U))) return 0U;
    if (!flash_wait() || !flash_unlock()) return 0U;
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_WRPRTERR;
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = addr;
    FLASH->CR |= FLASH_CR_STRT;
    ok = flash_wait() && (FLASH->SR & FLASH_SR_EOP);
    FLASH->CR &= ~FLASH_CR_PER;
    FLASH->CR |= FLASH_CR_LOCK;
    return ok;
}

static uint8_t program_half(uint32_t addr, uint16_t value)
{
    uint8_t ok;
    if ((addr & 1U) || addr < MOTOR_APP_BASE ||
        addr + 1U >= MOTOR_META_BASE + MOTOR_FLASH_PAGE_SIZE)
        return 0U;
    if (!flash_wait() || !flash_unlock()) return 0U;
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_WRPRTERR;
    FLASH->CR |= FLASH_CR_PG;
    *(__IO uint16_t *)addr = value;
    ok = flash_wait() && (FLASH->SR & FLASH_SR_EOP);
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;
    return ok && *(__IO uint16_t *)addr == value;
}

static int flash_read(uint32_t addr, uint8_t *data, uint16_t n)
{
    uint16_t i;
    const uint8_t *p;
    if (addr < MOTOR_APP_BASE || addr + n > MOTOR_META_BASE + MOTOR_FLASH_PAGE_SIZE)
        return 0;
    p = (const uint8_t *)addr;
    for (i = 0U; i < n; ++i) data[i] = p[i];
    return 1;
}

static int flash_invalidate(void)
{
    const uint8_t *p = (const uint8_t *)MOTOR_META_BASE;
    if (!erase_page(MOTOR_META_BASE)) return 0;
    return rd32(p + 12U) != MOTOR_META_MAGIC;
}

static int flash_erase_app(uint32_t length)
{
    uint32_t a, end;
    if (length > MOTOR_APP_CAPACITY) return 0;
    end = MOTOR_APP_BASE + length;
    for (a = MOTOR_APP_BASE; a < end; a += MOTOR_FLASH_PAGE_SIZE)
        if (!erase_page(a)) return 0;
    return 1;
}

static int flash_write_app(uint32_t addr, const uint8_t *p, uint16_t n)
{
    uint16_t i;
    if ((n & 1U) || addr < MOTOR_APP_BASE || addr + n > MOTOR_META_BASE)
        return 0;
    for (i = 0U; i < n; i += 2U)
        if (!program_half(addr + i, rd16(p + i))) return 0;
    return 1;
}

static int flash_commit(uint16_t board, uint16_t hw, uint16_t length,
                        uint32_t crc)
{
    const uint8_t *p = (const uint8_t *)MOTOR_META_BASE;
    if (board != MOTOR_BOARD_ID || hw != MOTOR_HW_COMPAT ||
        length < 8U || length > MOTOR_APP_CAPACITY) return 0;
    if (!program_half(MOTOR_META_BASE, board) ||
        !program_half(MOTOR_META_BASE + 2U, hw) ||
        !program_half(MOTOR_META_BASE + 4U, length) ||
        !program_half(MOTOR_META_BASE + 6U, 0U) ||
        !program_half(MOTOR_META_BASE + 8U, (uint16_t)crc) ||
        !program_half(MOTOR_META_BASE + 10U, (uint16_t)(crc >> 16)) ||
        !program_half(MOTOR_META_BASE + 12U, (uint16_t)MOTOR_META_MAGIC) ||
        !program_half(MOTOR_META_BASE + 14U, (uint16_t)(MOTOR_META_MAGIC >> 16)))
        return 0;
    return rd32(p + 12U) == MOTOR_META_MAGIC;
}

const motor_flash_ops_t boot_flash_ops = {
    flash_read, flash_invalidate, flash_erase_app,
    flash_write_app, flash_commit
};

uint8_t boot_image_valid(void)
{
    const uint8_t *m = (const uint8_t *)MOTOR_META_BASE;
    const uint8_t *a = (const uint8_t *)MOTOR_APP_BASE;
    uint16_t len = rd16(m + 4U);
    uint32_t sp = rd32(a), pc = rd32(a + 4U);
    if (rd32(m + 12U) != MOTOR_META_MAGIC ||
        rd16(m) != MOTOR_BOARD_ID || rd16(m + 2U) != MOTOR_HW_COMPAT ||
        len < 8U || len > MOTOR_APP_CAPACITY || (len & 1U) ||
        (sp & 3U) || sp <= 0x20000000UL || sp > 0x20000800UL ||
        !(pc & 1U) || (pc & ~1UL) < MOTOR_APP_BASE ||
        (pc & ~1UL) >= MOTOR_APP_BASE + len)
        return 0U;
    return image_crc(len) == rd32(m + 8U);
}

void boot_hw_init(void)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN |
                   RCC_AHBENR_GPIODEN;
    GPIOD->BRR = (1U << 3) | (1U << 4);
    GPIOD->BSRR = 1U << 6;
    GPIOD->MODER = (GPIOD->MODER & ~((3UL << 6) | (3UL << 8) | (3UL << 12))) |
                   (1UL << 6) | (1UL << 8) | (1UL << 12);
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(15UL << 12)) | (1UL << 12);
    GPIOB->AFR[0] = (GPIOB->AFR[0] & ~(15UL << 16)) | (1UL << 16);
    GPIOA->MODER = (GPIOA->MODER & ~(3UL << 6)) | (2UL << 6);
    GPIOB->MODER = (GPIOB->MODER & ~(3UL << 8)) | (2UL << 8);
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    USART1->CR1 = 0U;
    USART1->BRR = (SystemCoreClock + 57600UL) / 115200UL;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

uint8_t boot_uart_get(uint8_t *byte)
{
    if (USART1->ISR & USART_ISR_ORE) USART1->ICR = USART_ICR_ORECF;
    if (!(USART1->ISR & USART_ISR_RXNE)) return 0U;
    *byte = (uint8_t)USART1->RDR;
    return 1U;
}

uint8_t boot_uart_put(uint8_t byte)
{
    uint32_t n = 100000UL;
    while (!(USART1->ISR & USART_ISR_TXE) && n--) { }
    if (!(USART1->ISR & USART_ISR_TXE)) return 0U;
    USART1->TDR = byte;
    return 1U;
}

void boot_uart_drain(void)
{
    uint32_t n = 100000UL;
    while (!(USART1->ISR & USART_ISR_TC) && n--) { }
}

void boot_jump_app(void)
{
    uint32_t sp = *(const uint32_t *)MOTOR_APP_BASE;
    uint32_t pc = *(const uint32_t *)(MOTOR_APP_BASE + 4U);
    __disable_irq();
    SysTick->CTRL = 0U;
    USART1->CR1 = 0U;
    NVIC->ICER[0] = 0xFFFFFFFFUL;
    NVIC->ICPR[0] = 0xFFFFFFFFUL;
    FLASH->INT_VEC_OFFSET = MOTOR_APP_BASE - MOTOR_FLASH_BASE;
    __set_MSP(sp);
    ((void (*)(void))pc)();
    for (;;) { }
}
