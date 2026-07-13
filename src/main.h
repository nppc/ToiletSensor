//#pragma REGISTERBANK(0)
#include <SI_EFM8BB3_Register_Enums.h>                  // SFR declarations
#include <SI_EFM8BB3_Defs.h>
#include <stdint.h>

#ifndef SRC_MAIN_H_
#define SRC_MAIN_H_

// Structure for accessing 16bit number by 2 8 bit (back and forth)
// u16 and u8[] sharing the same memory space
// Usage:
// U16_U8 ptr;
// ptr.u8[0] = high;
// ptr.u8[1] = low;
// ptr.u16 = 16bit;
typedef union
   {
   unsigned short u16;
   unsigned char u8[2];
   } U16_U8;

//SI_SBIT(PIN_PWM, SFR_P1, 7);

void waitNms(uint16_t ms);

#endif /* SRC_MAIN_H_ */
