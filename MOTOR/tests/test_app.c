#include <assert.h>
#include <string.h>
#define main motor_application_main
#include "../firmware/app/main.c"
#undef main

motor_test_scb_t motor_test_scb;
uint32_t motor_test_uid[3]={0x12345678UL,0xABCDEF01UL,0x43218765UL};
uint32_t SystemCoreClock=24000000UL;
static uint8_t wire[148],tx_ok,tc_ok,drained;
static uint16_t wire_len,fail_after,in1,in2;
static uint32_t resets;

void motor_hw_init(void) { }
void motor_hw_pwm_init(void) { }
void SysTick_Config(uint32_t reload) { (void)reload; }
uint8_t motor_hw_uart_get(uint8_t *b) { (void)b; return 0U; }
void motor_hw_output(uint16_t a,uint16_t b)
{
    assert(!a || !b); in1=a; in2=b;
}
uint8_t motor_hw_uart_put(uint8_t b)
{
    if (rd16(rx+8U)==0x01F0U) assert(!motor.active && !in1 && !in2);
    if (!tx_ok || wire_len==fail_after) return 0U;
    assert(wire_len<sizeof(wire)); wire[wire_len++]=b; return 1U;
}
uint8_t motor_hw_uart_drain(void)
{
    assert(wire_len==20U && !motor.active && !in1 && !in2);
    drained=1U; return tc_ok;
}
void NVIC_SystemReset(void)
{
    assert(drained && tc_ok && wire_len==20U && !motor.active && !in1 && !in2);
    assert(rd16(wire+8U)==0x81F0U && rd32(wire+16U)==0U);
    ++resets;
}
static void prepare(void)
{
    used=expected=0U; wire_len=0U; drained=0U;
    tx_ok=tc_ok=1U; fail_after=0xFFFFU; ms=100U;
    motor_safety_init(&motor,motor_hw_output);
    assert(motor_safety_set(&motor,600,ms)); requested_pwm=600;
}
static void command(uint16_t cmd)
{
    uint8_t request[16];
    uint16_t i;
    memcpy(request,"KLQ1",4U); wr32(request+4U,17U);
    wr16(request+8U,cmd); wr16(request+10U,0U);
    wr32(request+12U,frame_crc(request,16U));
    for(i=0U;i<16U;++i) receive(request[i]);
}
int main(void)
{
    uint16_t i;
    prepare(); command(0x0001U);
    assert(wire_len==56U && rd32(wire+16U)==0U);
    assert(rd16(wire+20U)==1U && wire[22]==1U && wire[23]==2U);
    assert(rd16(wire+24U)==2U && rd16(wire+26U)==2U && rd16(wire+28U)==2U);
    assert(rd32(wire+32U)==0x00020000UL && rd32(wire+36U)==9U);
    assert(rd16(wire+42U)==18304U && rd32(wire+52U)==motor_test_uid[2]);
    assert(motor.active && in1==600U && !in2 && !resets);
    prepare(); command(0x01F0U);
    assert(resets==1U && !requested_pwm && !motor.active);
    for(i=0U;i<20U;++i) {
        prepare(); fail_after=i; command(0x01F0U);
        assert(wire_len==i && resets==1U && !drained && !requested_pwm && !motor.active);
    }
    prepare(); tc_ok=0U; command(0x01F0U);
    assert(wire_len==20U && drained && resets==1U && !motor.active && !requested_pwm);
    prepare(); command(0x0302U);
    assert(!requested_pwm && !motor.active && !in1 && !in2 && resets==1U);
    return 0;
}
