#include <stdint.h>
#include "motor_hw.h"
#include "motor_safety.h"
#include "../boot/update.h"

#define FRAME_SIZE 148U
#define RESPONSE_BIT 0x8000U

static volatile uint32_t ms;
static uint8_t rx[FRAME_SIZE], tx[FRAME_SIZE];
static uint16_t used, expected;
static uint32_t last_byte_ms;
static int16_t requested_pwm;
static motor_safety_t motor;

void SysTick_Handler(void) { ++ms; }

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t crc_step(uint32_t c, uint8_t v)
{
    uint8_t i;
    c ^= v;
    for (i = 0U; i < 8U; ++i)
        c = (c >> 1) ^ ((0UL - (c & 1UL)) & 0xEDB88320UL);
    return c;
}

static uint32_t frame_crc(const uint8_t *p, uint16_t n)
{
    uint32_t c = 0xFFFFFFFFUL;
    uint16_t i;
    for (i = 0U; i < 12U; ++i) c = crc_step(c, p[i]);
    for (i = 16U; i < n; ++i) c = crc_step(c, p[i]);
    return ~c;
}

static uint8_t reply(uint32_t seq, uint16_t cmd, uint32_t status,
                  const uint8_t *data, uint16_t n)
{
    uint16_t i, total = (uint16_t)(20U + n);
    if (n > 128U) return 0U;
    tx[0]='K'; tx[1]='L'; tx[2]='Q'; tx[3]='1';
    wr32(tx + 4U, seq); wr16(tx + 8U, (uint16_t)(cmd | RESPONSE_BIT));
    wr16(tx + 10U, (uint16_t)(n + 4U)); wr32(tx + 16U, status);
    for (i=0U; i<n; ++i) tx[20U+i]=data[i];
    wr32(tx + 12U, frame_crc(tx, total));
    for (i=0U; i<total; ++i) if (!motor_hw_uart_put(tx[i])) return 0U;
    return 1U;
}

static void process_frame(void)
{
    uint32_t seq=rd32(rx+4U), status=1U;
    uint16_t cmd=rd16(rx+8U), n=rd16(rx+10U);
    const uint8_t *p=rx+16U;
    uint8_t data[36];
    uint16_t len=0U;
    int16_t duty;
    if (cmd & RESPONSE_BIT) return;
    switch (cmd) {
    case 0x0001U: /* INFO */
        if (n) { status=2U; break; }
        wr16(data,1U); data[2]=1U; data[3]=2U;
        wr16(data+4U,MOTOR_BOARD_ID); wr16(data+6U,MOTOR_HW_COMPAT);
        wr16(data+8U,2U); wr16(data+10U,0U);
        wr32(data+12U,MOTOR_FIRMWARE_VERSION); wr32(data+16U,1U
#ifdef KLQ_BOOT_APP
                | 8U
#endif
                );
        wr16(data+20U,132U); wr16(data+22U,MOTOR_APP_CAPACITY);
        wr32(data+24U,*(const uint32_t *)UID_BASE);
        wr32(data+28U,*(const uint32_t *)(UID_BASE+4U));
        wr32(data+32U,*(const uint32_t *)(UID_BASE+8U));
        status=0U; len=36U; break;
    case 0x0300U: /* SET_PWM: signed -1000..1000; refresh every <250 ms. */
        if (n!=2U) { status=2U; break; }
        duty=(int16_t)rd16(p);
        if (!motor_safety_set(&motor,duty,ms)) {
            requested_pwm=0;
            status=2U;
        } else {
            requested_pwm=duty;
            status=0U;
        }
        wr16(data,(uint16_t)requested_pwm); len=2U; break;
    case 0x0301U: /* GET_STATUS: uptime, duty, reserved=0 (no encoder), active. */
        if (n) { status=2U; break; }
        wr32(data,ms); wr16(data+4U,(uint16_t)requested_pwm);
        data[6]=0U;
        data[7]=motor.active;
        len=8U; status=0U; break;
    case 0x0302U: /* STOP */
        if (n) { status=2U; break; }
        requested_pwm=0; motor_safety_stop(&motor); status=0U; break;
    case 0x01F0U: /* ENTER_BOOTLOADER */
        if (n) { status=2U; break; }
        requested_pwm=0; motor_safety_stop(&motor);
        if (reply(seq,cmd,0U,0,0U) && motor_hw_uart_drain())
            NVIC_SystemReset();
        return;
    default: break;
    }
    reply(seq,cmd,status,data,len);
}

static void receive(uint8_t b)
{
    static const uint8_t magic[4]={'K','L','Q','1'};
    uint16_t n;
    last_byte_ms=ms;
    if (used<4U) {
        if (b!=magic[used]) {
            used=b=='K'?1U:0U;
            if (used) rx[0]='K';
            return;
        }
        rx[used++]=b;
        return;
    }
    if (used>=FRAME_SIZE) { used=expected=0U; return; }
    rx[used++]=b;
    if (used==16U) {
        n=rd16(rx+10U);
        if (n>132U) { used=expected=0U; return; }
        expected=(uint16_t)(16U+n);
    }
    if (expected && used==expected) {
        if (rd32(rx+12U)==frame_crc(rx,expected)) process_frame();
        used=expected=0U;
    }
}

int main(void)
{
    uint8_t b,state;
    __disable_irq();
    SCB->VTOR=MOTOR_APP_BASE; __DSB(); __ISB();
    motor_hw_init();
    motor_hw_pwm_init();
    motor_safety_init(&motor,motor_hw_output);
    SysTick_Config(SystemCoreClock/1000UL);
    __enable_irq();
    for (;;) {
        state=motor_hw_uart_get(&b);
        if (state==1U) receive(b);
        else if (state==2U) {
            used=expected=0U;
            requested_pwm=0; motor_safety_stop(&motor);
        }
        if (used && (uint32_t)(ms-last_byte_ms)>20U)
            used=expected=0U;
        if (motor.active && (uint32_t)(ms-motor.last_command_ms)>=MOTOR_LINK_TIMEOUT_MS)
            requested_pwm=0;
        motor_safety_poll(&motor,ms);
    }
}
