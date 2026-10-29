#ifndef BUSINESS_GPIO_H
#define BUSINESS_GPIO_H
typedef struct { unsigned pin,mode,speed; } GPIO_Config_T;
#define GPIOC ((void *)0)
#define GPIO_PIN_3 8u
#define GPIO_PIN_4 16u
#define GPIO_MODE_IN_PU 1u
#define GPIO_SPEED_2MHz 2u
void GPIO_Config(void *port,GPIO_Config_T *io);
unsigned GPIO_ReadInputBit(void *port,unsigned pin);
#endif
