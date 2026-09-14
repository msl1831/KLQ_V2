#include "sc7a20.h"
#include "board.h"
#include "apm32e10x_gpio.h"
#include "apm32e10x_rcm.h"
#include <math.h>

#define SC7_PORT GPIOB
#define SC7_SCL GPIO_PIN_8
#define SC7_SDA GPIO_PIN_9
#define SC7_WHO_AM_I 0x0Fu
#define SC7_CTRL_REG0 0x1Fu
#define SC7_CTRL_REG1 0x20u
#define SC7_CTRL_REG4 0x23u
#define SC7_STATUS_REG 0x27u
#define SC7_OUT_X_L 0x28u
#define SC7_VERSION_REG 0x70u
#define SC7_ID 0x11u
#define SC7_DELAY_US 5u
#define SC7_CLOCK_TIMEOUT_US 1000u

static uint8_t device_address;
static uint8_t device_identity;
static uint8_t device_version;

static void line_high(uint16_t pin) { GPIO_SetBit(SC7_PORT, pin); }
static void line_low(uint16_t pin) { GPIO_ResetBit(SC7_PORT, pin); }
static bool line_is_high(uint16_t pin) { return GPIO_ReadInputBit(SC7_PORT, pin) != 0; }

static bool clock_high(void)
{
    uint32_t waited = 0;
    line_high(SC7_SCL);
    while (!line_is_high(SC7_SCL)) {
        if (++waited >= SC7_CLOCK_TIMEOUT_US) return false;
        delay_us(1);
    }
    delay_us(SC7_DELAY_US);
    return true;
}

static void clock_low(void)
{
    line_low(SC7_SCL);
    delay_us(SC7_DELAY_US);
}

static bool i2c_start(void)
{
    line_high(SC7_SDA);
    if (!clock_high()) return false;
    if (!line_is_high(SC7_SDA)) return false;
    line_low(SC7_SDA);
    delay_us(SC7_DELAY_US);
    clock_low();
    return true;
}

static void i2c_stop(void)
{
    line_low(SC7_SDA);
    delay_us(SC7_DELAY_US);
    if (!clock_high()) return;
    line_high(SC7_SDA);
    delay_us(SC7_DELAY_US);
}

static bool i2c_write_byte(uint8_t value)
{
    unsigned bit;
    bool acknowledged;

    for (bit = 0; bit < 8; ++bit) {
        if (value & 0x80u) line_high(SC7_SDA); else line_low(SC7_SDA);
        delay_us(SC7_DELAY_US);
        if (!clock_high()) return false;
        clock_low();
        value <<= 1;
    }
    line_high(SC7_SDA);
    delay_us(SC7_DELAY_US);
    if (!clock_high()) return false;
    acknowledged = !line_is_high(SC7_SDA);
    clock_low();
    return acknowledged;
}

static bool i2c_read_byte(uint8_t *value, bool acknowledge)
{
    unsigned bit;
    uint8_t data = 0;

    line_high(SC7_SDA);
    for (bit = 0; bit < 8; ++bit) {
        data <<= 1;
        if (!clock_high()) return false;
        if (line_is_high(SC7_SDA)) data |= 1u;
        clock_low();
    }
    if (acknowledge) line_low(SC7_SDA); else line_high(SC7_SDA);
    delay_us(SC7_DELAY_US);
    if (!clock_high()) return false;
    clock_low();
    line_high(SC7_SDA);
    *value = data;
    return true;
}

static void i2c_recover(void)
{
    unsigned pulse;

    line_high(SC7_SDA);
    for (pulse = 0; pulse < 9 && !line_is_high(SC7_SDA); ++pulse) {
        clock_low();
        clock_high();
    }
    i2c_stop();
}

static bool write_register(uint8_t address, uint8_t reg, uint8_t value)
{
    bool ok = i2c_start();
    if (ok) ok = i2c_write_byte((uint8_t)(address << 1));
    if (ok) ok = i2c_write_byte(reg);
    if (ok) ok = i2c_write_byte(value);
    i2c_stop();
    return ok;
}

static bool read_registers(uint8_t address, uint8_t reg, uint8_t *data, uint32_t length)
{
    uint32_t i;
    bool ok = length != 0 && i2c_start();

    if (ok) ok = i2c_write_byte((uint8_t)(address << 1));
    if (ok) ok = i2c_write_byte(reg);
    if (ok) ok = i2c_start();
    if (ok) ok = i2c_write_byte((uint8_t)((address << 1) | 1u));
    for (i = 0; ok && i < length; ++i)
        ok = i2c_read_byte(&data[i], i + 1u < length);
    i2c_stop();
    return ok;
}

static bool read_register(uint8_t address, uint8_t reg, uint8_t *value)
{
    return read_registers(address, reg, value, 1);
}

bool sc7a20_init(void)
{
    GPIO_Config_T io;
    const uint8_t addresses[] = {0x19u, 0x18u};
    unsigned i;

    device_address = 0;
    device_identity = 0;
    device_version = 0;
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_GPIOB);
    line_high(SC7_SCL | SC7_SDA);
    io.pin = SC7_SCL | SC7_SDA;
    io.mode = GPIO_MODE_OUT_OD;
    io.speed = GPIO_SPEED_50MHz;
    GPIO_Config(SC7_PORT, &io);
    line_high(SC7_SCL | SC7_SDA);
    delay_ms(5);
    i2c_recover();

    for (i = 0; i < sizeof(addresses); ++i) {
        uint8_t identity = 0;
        uint8_t version = 0;
        if (read_register(addresses[i], SC7_WHO_AM_I, &identity) &&
            identity == SC7_ID &&
            read_register(addresses[i], SC7_VERSION_REG, &version)) {
            device_address = addresses[i];
            device_identity = identity;
            device_version = version;
            break;
        }
        i2c_recover();
    }
    if (!device_address) return false;

    /* High resolution, block update, +-2 g, 100 Hz, X/Y/Z enabled. */
    if (!write_register(device_address, SC7_CTRL_REG0, 0x01u) ||
        !write_register(device_address, SC7_CTRL_REG4, 0x80u) ||
        !write_register(device_address, SC7_CTRL_REG1, 0x57u)) {
        device_address = 0;
        return false;
    }
    delay_ms(20);
    return true;
}

bool sc7a20_read(sc7a20_sample_t *sample)
{
    uint8_t status;
    uint8_t raw[6];
    int16_t x, y, z;
    float xf, yf, zf;

    if (!sample || !device_address) return false;
    if (!read_register(device_address, SC7_STATUS_REG, &status) || !(status & 0x08u))
        return false;
    if (!read_registers(device_address, SC7_OUT_X_L | 0x80u, raw, sizeof(raw)))
        return false;

    x = (int16_t)((uint16_t)raw[1] << 8 | raw[0]);
    y = (int16_t)((uint16_t)raw[3] << 8 | raw[2]);
    z = (int16_t)((uint16_t)raw[5] << 8 | raw[4]);
    x >>= 4;
    y >>= 4;
    z >>= 4;
    sample->x_mg = x;
    sample->y_mg = y;
    sample->z_mg = z;

    xf = (float)x;
    yf = (float)y;
    zf = (float)z;
    sample->roll_cdeg = (int32_t)(atan2f(yf, zf) * 5729.57795f);
    sample->pitch_cdeg = (int32_t)(atan2f(-xf, sqrtf(yf * yf + zf * zf)) * 5729.57795f);
    return true;
}

uint8_t sc7a20_address(void) { return device_address; }
uint8_t sc7a20_identity(void) { return device_identity; }
uint8_t sc7a20_version(void) { return device_version; }
