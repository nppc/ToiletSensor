/*
 * i2c.h
 *
 *  Created on: 23 Oct 2022
 *      Author: Pavel
 */

#ifndef SRC_I2C_H_
#define SRC_I2C_H_

#include <stdint.h>

//-----------------------------------------------------------------------------
// Global CONSTANTS
//-----------------------------------------------------------------------------
#define WRITE 0x00 // I2C WRITE command
#define READ 0x01 // I2C READ command

// 24LC512 EEPROM address (7-bit address)
#define EEPROM_ADDR 0xA0 // A2=0, A1=0, A0=0

// Status vector - top 4 bits only
#define  SMB_MTSTA      0xE0           // (MT) start transmitted
#define  SMB_MTDB       0xC0           // (MT) data byte transmitted
#define  SMB_MRDB       0x80           // (MR) data byte received
// End status vector definition

//-----------------------------------------------------------------------------
// Global VARIABLES
//-----------------------------------------------------------------------------
extern unsigned char SMB_DATA_OUT;   // Data to transmit
extern unsigned long NUM_ERRORS;     // Error counter

SI_SBIT(SDA, SFR_P0, 2);
SI_SBIT(SCL, SFR_P0, 3);

//-----------------------------------------------------------------------------
// Function PROTOTYPES
//-----------------------------------------------------------------------------

// Test routine
void test_i2c(void);
void test_write_flash_data(void);
uint8_t test_verify_flash_data(void);

#endif /* SRC_I2C_H_ */
