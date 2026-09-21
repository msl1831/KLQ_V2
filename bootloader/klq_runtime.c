#include "klq_runtime.h"
#include "board.h"
#include "external_ports.h"
#include "robot_ui.h"
#include "apm32e10x_gpio.h"
#include "apm32e10x_rcm.h"

#define TILT_CDEG 1500

static sc7a20_sample_t imu;
static uint32_t imu_at, button_at;
static uint8_t imu_valid, motor_power=2, move_power=2, button_raw, button_stable;

void klq_runtime_init(void)
{
    GPIO_Config_T io;
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_GPIOC);
    io.pin=GPIO_PIN_3;
    io.mode=GPIO_MODE_IN_PU;
    io.speed=GPIO_SPEED_2MHz;
    GPIO_Config(GPIOC,&io);
    imu_valid=0;
    button_raw=button_stable=(uint8_t)(GPIO_ReadInputBit(GPIOC,GPIO_PIN_3)==0);
    button_at=board_ms;
}

void klq_runtime_poll(void)
{
    uint8_t raw=(uint8_t)(GPIO_ReadInputBit(GPIOC,GPIO_PIN_3)==0);
    if (raw!=button_raw) { button_raw=raw; button_at=board_ms; }
    else if (raw!=button_stable && board_ms-button_at>=20u) button_stable=raw;
}

void klq_runtime_update_imu(const sc7a20_sample_t *s, bool valid)
{
    if (valid && s) { imu=*s; imu_valid=1; imu_at=board_ms; }
    else imu_valid=0;
}

static bool cmp(int32_t value, int compare, int32_t limit)
{
    return compare<0 ? value<limit : value>limit;
}

bool klq_runtime_distance(unsigned port, int compare, int32_t cm)
{
    const klq_port_status_t *s;
    if (port<1u || port>KLQ_EXTERNAL_PORT_COUNT || (compare!=-1 && compare!=1) || cm<0) return false;
    s=external_port_status(port-1u);
    return s && s->online && s->device_class==1u && s->sensor_type==1u && !s->sample_status &&
           board_ms-s->sample_host_ms<=80u &&
           cmp(s->distance_mm,compare,cm*10);
}

bool klq_runtime_tilt(unsigned direction)
{
    if (!imu_valid || board_ms-imu_at>100u) return false;
    switch (direction) {
    case 0: return imu.pitch_cdeg<=-TILT_CDEG;
    case 1: return imu.pitch_cdeg>= TILT_CDEG;
    case 2: return imu.roll_cdeg <=-TILT_CDEG;
    case 3: return imu.roll_cdeg >= TILT_CDEG;
    default:return false;
    }
}

bool klq_runtime_button(bool pressed)
{
    return (button_stable!=0)==pressed;
}

int klq_runtime_motor(unsigned port, int direction, bool run)
{
    if (port<1u || port>KLQ_EXTERNAL_PORT_COUNT || (direction!=-1 && direction!=1)) return KLQ_RT_RANGE;
    if (!run) { external_port_motor(port-1u,0,false,motor_power); return KLQ_RT_OK; }
    return external_port_motor(port-1u,direction,run,motor_power) ? KLQ_RT_OK : KLQ_RT_UNSUPPORTED;
}

int klq_runtime_move(unsigned direction, bool run)
{
    int a=0,b=0;
    if (direction>3u) return KLQ_RT_RANGE;
    if (run) {
        if (direction==0u) a=b=1;
        else if (direction==1u) a=b=-1;
        else if (direction==2u) { a=-1; b=1; }
        else { a=1; b=-1; }
    }
    if (!run) { external_port_motor(0,0,false,move_power); external_port_motor(1,0,false,move_power); return KLQ_RT_OK; }
    if (!external_port_motor(0,a,true,move_power)) return KLQ_RT_UNSUPPORTED;
    if (!external_port_motor(1,b,true,move_power)) { external_port_motor(0,0,false,move_power); return KLQ_RT_UNSUPPORTED; }
    return KLQ_RT_OK;
}

void klq_runtime_stop_all(void) { external_ports_motor_stop_all(); }
void klq_runtime_set_motor_power(unsigned level) { if (level>=1u && level<=3u) motor_power=(uint8_t)level; }
void klq_runtime_set_move_power(unsigned level) { if (level>=1u && level<=3u) move_power=(uint8_t)level; }

static const uint8_t faces[10][13] = {
    {0,0,0x10,0x26,0x46,0x40,0x40,0x40,0x46,0x26,0x10,0,0},
    {0,0x10,0x26,0x46,0x40,0x48,0x50,0x48,0x40,0x46,0x26,0x10,0},
    {0,0x20,0x12,0x44,0x40,0x40,0x40,0x40,0x40,0x44,0x12,0x20,0},
    {0,0x10,0x26,0x46,0x40,0x50,0x48,0x50,0x40,0x46,0x26,0x10,0},
    {0,0x10,0x26,0x46,0x40,0x5c,0x54,0x5c,0x40,0x46,0x26,0x10,0},
    {0,0x10,0x20,0x46,0x40,0x48,0x50,0x48,0x40,0x40,0x20,0x10,0},
    {0,0x0c,0x12,0x24,0x48,0x10,0x20,0x10,0x48,0x24,0x12,0x0c,0},
    {0,0x10,0x20,0x41,0x42,0x44,0x48,0x44,0x42,0x41,0x20,0x10,0},
    {0,0x10,0x26,0x46,0x40,0x50,0x40,0x48,0x40,0x46,0x26,0x10,0},
    {0,0x10,0x26,0x46,0x40,0x44,0x48,0x44,0x40,0x46,0x26,0x10,0}
};

bool klq_runtime_pattern(const uint8_t columns[13]) { return robot_ui_set_user_pattern(columns); }
bool klq_runtime_face(unsigned face) { return face<10u && klq_runtime_pattern(faces[face]); }

bool klq_runtime_number(unsigned value)
{
    static const uint8_t d[10][3]={{31,17,31},{0,31,0},{29,21,23},{21,21,31},{7,4,31},
        {23,21,29},{31,21,29},{1,1,31},{31,21,31},{23,21,31}};
    uint8_t out[13]={0}, digit[3], n=0, width, x, i, j;
    if (value>100u) return false;
    if (value==100u) { digit[0]=1; digit[1]=0; digit[2]=0; n=3; }
    else if (value>=10u) { digit[0]=(uint8_t)(value/10u); digit[1]=(uint8_t)(value%10u); n=2; }
    else { digit[0]=(uint8_t)value; n=1; }
    width=(uint8_t)(n*3u+n-1u); x=(uint8_t)((13u-width)/2u);
    for (i=0;i<n;i++) for (j=0;j<3u;j++) out[x+i*4u+j]=(uint8_t)(d[digit[i]][j]<<1);
    return klq_runtime_pattern(out);
}

bool klq_runtime_display_off(void)
{
    static const uint8_t blank[13]={0};
    return klq_runtime_pattern(blank);
}
