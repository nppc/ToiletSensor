#ifndef I2C_BITBANG_H_
#define I2C_BITBANG_H_

#include <stdint.h>

//=============================================================================
// Bit-bang I2C Functions for EEPROM Testing
//=============================================================================

// Low-level bit-bang functions
void i2c_bb_start(void);
void i2c_bb_stop(void);
void i2c_bb_send_bit(uint8_t onebit);
uint8_t i2c_bb_read_bit(void);

// Byte-level functions
uint8_t i2c_bb_send_byte(uint8_t byte);
uint8_t i2c_bb_read_byte(uint8_t send_ack);

// EEPROM functions
uint8_t i2c_bb_write_byte(uint16_t address, uint8_t bytedata);
uint8_t i2c_bb_read_byte_eeprom(uint16_t address);

// Test function
void test_i2c_bitbang(void);

#endif /* I2C_BITBANG_H_ */
