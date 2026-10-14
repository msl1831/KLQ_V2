#ifndef KLQ_SIM_STDINT_H
#define KLQ_SIM_STDINT_H
/* VC98 lacks C99 stdint; its Win32 integer widths match the MCU's types. */
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
#endif
