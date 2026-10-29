#ifndef SENSOR_FLASH_HOST_HW_H
#define SENSOR_FLASH_HOST_HW_H
#include <stdint.h>

#define __IO volatile
#define FLASH_SR_EOP 1UL
#define FLASH_SR_WRPERR (1UL<<4)
#define FLASH_SR_OPTVERR (1UL<<15)
#define FLASH_SR_BSY (1UL<<16)
#define FLASH_CR_PG 1UL
#define FLASH_CR_PER (1UL<<1)
#define FLASH_CR_PGSTRT (1UL<<19)
#define FLASH_CR_EOPIE (1UL<<24)
#define FLASH_CR_LOCK (1UL<<31)
#define FLASH_KEY1 0x45670123UL
#define FLASH_KEY2 0xCDEF89ABUL
#define RCC_ICSCR_HSI_FS_Pos 13U
#define RCC_ICSCR_HSI_FS (7UL<<13)
#define SCB_ICSR_PENDSTCLR_Msk (1UL<<25)
#define SCB_ICSR_PENDSVCLR_Msk (1UL<<27)

uint32_t mock_read(unsigned id);
void mock_write(unsigned id,uint32_t value);
class FlashRegister {
    unsigned id;
public:
    explicit FlashRegister(unsigned which):id(which) {}
    operator uint32_t() const { return mock_read(id); }
    FlashRegister& operator=(uint32_t n) { mock_write(id,n);return *this; }
    FlashRegister& operator|=(uint32_t n) { return operator=(mock_read(id)|n); }
    FlashRegister& operator&=(uint32_t n) { return operator=(mock_read(id)&n); }
};
struct MockFlashRegisters {
    FlashRegister SR,CR,KEYR;
    uint32_t TS0,TS1,TS3,TS2P,TPS3,PERTPE,SMERTPE,PRGTPE,PRETPE;
    MockFlashRegisters():SR(0),CR(1),KEYR(2) {}
};
struct MockRCC { uint32_t ICSCR; };
struct MockSysTick { uint32_t CTRL; };
struct MockUSART { uint32_t CR1; };
struct MockNVIC { uint32_t ICER[1],ICPR[1]; };
struct MockSCB { uint32_t ICSR,VTOR; };
extern MockFlashRegisters mock_flash;
extern MockRCC mock_rcc;
extern MockSysTick mock_systick;
extern MockUSART mock_usart;
extern MockNVIC mock_nvic;
extern MockSCB mock_scb;
extern uint32_t mock_primask;
#define FLASH (&mock_flash)
#define RCC (&mock_rcc)
#define SysTick (&mock_systick)
#define USART1 (&mock_usart)
#define NVIC (&mock_nvic)
#define SCB (&mock_scb)
#define __get_PRIMASK() (mock_primask)
#define __disable_irq() ((void)(mock_primask=1U))
#define __set_PRIMASK(n) ((void)(mock_primask=(n)))
#define __DSB() ((void)0)
#define __ISB() ((void)0)
void sensor_hw_init(void);
uint8_t sensor_hw_uart_get(uint8_t *b);
uint8_t sensor_hw_uart_put(uint8_t b);
uint8_t sensor_hw_uart_drain(void);
#endif
