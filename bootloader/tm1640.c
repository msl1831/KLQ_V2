#include "board.h"

/* TM1640 is a write-only, LSB-first two-wire interface (not I2C, no ACK). */
#define CLK(v) do { if (v) GPIOB->BSC = 1u << 6; else GPIOB->BC = 1u << 6; } while (0)
#define DAT(v) do { if (v) GPIOB->BSC = 1u << 7; else GPIOB->BC = 1u << 7; } while (0)
static void start(void) { DAT(1); CLK(1); delay_us(5); DAT(0); delay_us(5); CLK(0); }
static void stop(void) { CLK(0); DAT(0); delay_us(5); CLK(1); delay_us(5); DAT(1); delay_us(5); }
static void byte_out(uint8_t b)
{
    unsigned i;
    for (i = 0; i < 8; ++i) {
        CLK(0); DAT(b & 1); delay_us(5); CLK(1); delay_us(5); b >>= 1;
    }
    CLK(0);
}
static void command(uint8_t value) { start(); byte_out(value); stop(); }

void display_columns(const uint8_t columns[13])
{
    unsigned x;
    command(0x40); /* Normal mode, auto-increment. */
    start(); byte_out(0xc0);
    for (x = 0; x < 16; ++x) byte_out(x < 13 ? columns[x] & 0x7f : 0);
    stop();
    command(0x8a); /* Display on, moderate brightness. */
}

void display_icon(unsigned icon)
{
    /* rows top->bottom SEG1..7, chars left->right GRID1..13. */
    static const char * const rows[][7] = {
        {"......#......", "......#......", "....#.#.#....", ".....###.....",
         "......#......", "..#.......#..", "..#########.."},
        {".............", ".........#...", "........#....", "...#...#.....",
         "....#.#......", ".....#.......", "............."},
        {"...#.....#...", "....#...#....", ".....#.#.....", "......#......",
         ".....#.#.....", "....#...#....", "...#.....#..."}
#ifdef KLQ_DEMO_APP
        ,
        {".............", "...##...##...", "...##...##...", ".............",
         "..#.......#..", "...#.....#...", "....#####...."},
        {".....#.......", ".....###.....", ".....#####...", ".....#######.",
         ".....#####...", ".....###.....", ".....#......."}
#endif
    };
    uint8_t col[13] = {0};
    unsigned x,y;
#ifdef KLQ_DEMO_APP
    if (icon > DISPLAY_ICON_RUNNING) icon = DISPLAY_ICON_DOWNLOAD;
#else
    if (icon > DISPLAY_ICON_ERROR) icon = DISPLAY_ICON_DOWNLOAD;
#endif
    for (x = 0; x < 13; ++x) for (y = 0; y < 7; ++y)
        if (rows[icon][y][x] == '#') col[x] |= 1u << y;
    display_columns(col);
}

void display_clear(void)
{
    static const uint8_t blank[13] = {0};
    display_columns(blank);
}

void display_init(void)
{
    GPIO_Config_T io;
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_GPIOB);
    GPIOB->BSC = (1u << 6) | (1u << 7);
    io.pin = GPIO_PIN_6 | GPIO_PIN_7;
    io.mode = GPIO_MODE_OUT_PP;
    io.speed = GPIO_SPEED_2MHz;
    GPIO_Config(GPIOB, &io);
    delay_ms(5);
    command(0x80);
    display_clear();
}
