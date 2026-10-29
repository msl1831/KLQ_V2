#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))
#define RDP (*(volatile uint16_t *)0x1FFFF800UL)
#define STATUS ((volatile uint32_t *)0x20000700UL)
#define FLASH 0x40022000UL
#define SR (FLASH + 0x0CUL)
#define CR (FLASH + 0x10UL)
#define AR (FLASH + 0x14UL)
#define KEYR (FLASH + 0x04UL)
#define OPTKEYR (FLASH + 0x08UL)
#define BUSY 1UL
#define ERROR 0x10UL
#define EOP 0x20UL
#define OPTPG 0x10UL
#define OPTER 0x20UL
#define START 0x40UL
#define LOCK 0x80UL
#define OPTWRE 0x200UL

static uint32_t ready(void)
{
    uint32_t n = 4000000UL;
    while ((REG(SR) & BUSY) && n--) { }
    return !(REG(SR) & BUSY) && !(REG(SR) & ERROR);
}

__attribute__((noreturn)) void unlock_entry(void)
{
    uint32_t i;
    __asm { cpsid i }
    STATUS[0] = 0xA001UL;
    STATUS[1] = STATUS[2] = STATUS[3] = 0UL;
    if (!ready()) { STATUS[0] = 0xE001UL; goto done; }
    if (REG(CR) & LOCK) {
        REG(KEYR) = 0x45670123UL;
        REG(KEYR) = 0xCDEF89ABUL;
    }
    if (REG(CR) & LOCK) { STATUS[0] = 0xE002UL; goto done; }
    STATUS[0] = 0xA002UL;
    if (!(REG(CR) & OPTWRE)) {
        REG(OPTKEYR) = 0x45670123UL;
        REG(OPTKEYR) = 0xCDEF89ABUL;
    }
    if (!(REG(CR) & OPTWRE)) { STATUS[0] = 0xE003UL; goto done; }
    STATUS[0] = 0xA003UL;
    for (i = 0UL; i < 2UL; ++i) {
        REG(SR) = EOP | ERROR;
        REG(CR) |= OPTER;
        REG(AR) = 0x1FFFF800UL + i;
        REG(CR) |= START;
        if (!ready()) { STATUS[0] = 0xE004UL + i; goto done; }
        STATUS[1] = REG(SR);
        STATUS[2] = RDP;
        STATUS[3] = REG(CR);
        if (!(REG(SR) & EOP)) { STATUS[0] = 0xE006UL + i; goto done; }
        STATUS[0] = 0xA004UL + i;
    }
    REG(CR) &= ~OPTER;
    REG(SR) = EOP | ERROR;
    REG(CR) |= OPTPG;
    RDP = 0xAAU;
    if (!ready()) { STATUS[0] = 0xE008UL; goto done; }
    STATUS[1] = REG(SR);
    STATUS[2] = RDP;
    STATUS[3] = REG(CR);
    if (!(REG(SR) & EOP)) { STATUS[0] = 0xE009UL; goto done; }
    REG(CR) &= ~OPTPG;
    STATUS[0] = 0xA006UL;
done:
    for (;;) { }
}
