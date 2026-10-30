#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define KLQ_BOARD_H
#define GPIO_PIN_6 64u
#define GPIO_PIN_7 128u
#define GPIO_MODE_OUT_PP 1u
#define GPIO_SPEED_2MHz 2u
#define RCM_APB2_PERIPH_GPIOB 1u
#define DISPLAY_ICON_DOWNLOAD 0u
#define DISPLAY_ICON_ERROR 2u
#define DISPLAY_ICON_STANDBY_LOOK_RIGHT 7u
typedef struct {unsigned pin,mode,speed;} GPIO_Config_T;
static struct {uint32_t BSC,BC;} regs;
#define GPIOB (&regs)
static unsigned checks,delays,ms,configs;
#define CHECK(x) do{++checks;if(!(x)){printf("TM1640 line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
void RCM_EnableAPB2PeriphClock(unsigned p){CHECK(p==RCM_APB2_PERIPH_GPIOB);}
void GPIO_Config(void *p,GPIO_Config_T *io){CHECK(p==GPIOB&&io->pin==(GPIO_PIN_6|GPIO_PIN_7));++configs;}
void delay_us(uint32_t n){CHECK(n==5);++delays;}
void delay_ms(uint32_t n){ms+=n;}
#include "../../bootloader/tm1640.c"
int main(void)
{
    uint8_t a[13],b[13];unsigned i,n;
    memset(a,0,sizeof(a));display_init();CHECK(configs==1&&ms==5&&delays==340);n=delays;
    display_clear();CHECK(delays==n);for(i=0;i<13;i++)a[i]=(uint8_t)(i+1);display_columns(a);CHECK(delays==n+319);n=delays;
    display_columns(a);CHECK(delays==n);for(i=0;i<13;i++)b[i]=(uint8_t)(a[i]|0x80);display_columns(b);CHECK(delays==n);
    for(i=0;i<13;i++){b[i]^=1;display_columns(b);CHECK(delays==n+319);n=delays;display_columns(b);CHECK(delays==n);}
    display_clear();CHECK(delays==n+319);n=delays;display_clear();CHECK(delays==n);display_columns(a);CHECK(delays==n+319);n=delays;
    display_clear();CHECK(delays==n+319);n=delays;display_init();CHECK(delays==n+340&&configs==2&&ms==10);n=delays;
    display_icon(DISPLAY_ICON_DOWNLOAD);CHECK(delays==n+319);n=delays;display_icon(DISPLAY_ICON_DOWNLOAD);CHECK(delays==n);
    printf("TM1640: %u assertions passed; repeat masked frames zero bus work; init/off/restore verified\n",checks);return 0;
}
