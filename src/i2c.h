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
#define EEPROM_ADDR 0x50 // A2=0, A1=0, A0=0

// I2C State machine states
typedef enum {
    I2C_IDLE = 0,
    I2C_START_SENT,
    I2C_ADDR_SENT,
    I2C_DATA_SENT,
    I2C_DATA_RECEIVED,
    I2C_STOP_SENT,
    I2C_ERROR
} i2c_state_t;

//-----------------------------------------------------------------------------
// Global VARIABLES
//-----------------------------------------------------------------------------
extern unsigned char SMB_DATA_IN;    // Received data
extern unsigned char SMB_DATA_OUT;   // Data to transmit
extern unsigned char TARGET;         // Target slave address
extern unsigned long NUM_ERRORS;     // Error counter

SI_SBIT(SDA, SFR_P0, 2);
SI_SBIT(SCL, SFR_P0, 3);

//-----------------------------------------------------------------------------
// Function PROTOTYPES
//-----------------------------------------------------------------------------

// Low-level I2C functions (polling-based, non-blocking)
void i2c_init(void);
void i2c_start_write(uint8_t slave_addr, uint8_t bytedata);
void i2c_start_read(uint8_t slave_addr);
void i2c_poll(void);
uint8_t i2c_is_busy(void);
uint8_t i2c_get_data(void);

// EEPROM-specific functions
void eeprom_write_byte(uint16_t address, uint8_t bytedata);
uint8_t eeprom_read_byte(uint16_t address);
void eeprom_start_read(uint16_t address);
uint8_t eeprom_read_is_ready(void);

// Test routine
void test_i2c(void);

#endif /* SRC_I2C_H_ */
