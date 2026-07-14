#include "main.h"
#include "i2c.h"

//=============================================================================
// Bit-bang I2C Implementation for Testing
// Simple software-based I2C to verify EEPROM connectivity
//=============================================================================

// Bit-bang I2C timing delays (adjust based on clock speed)
#define I2C_DELAY() { unsigned char i; for(i=0; i<10; i++); }

//=============================================================================
// i2c_bb_start - Generate START condition
//=============================================================================
void i2c_bb_start(void)
{
    SDA = 1;  // Release SDA
    I2C_DELAY();
    SCL = 1;  // Release SCL
    I2C_DELAY();
    SDA = 0;  // Pull SDA low (START condition)
    I2C_DELAY();
    SCL = 0;  // Pull SCL low
    I2C_DELAY();
}

//=============================================================================
// i2c_bb_stop - Generate STOP condition
//=============================================================================
void i2c_bb_stop(void)
{
    SDA = 0;  // Pull SDA low
    I2C_DELAY();
    SCL = 1;  // Release SCL
    I2C_DELAY();
    SDA = 1;  // Release SDA (STOP condition)
    I2C_DELAY();
}

//=============================================================================
// i2c_bb_send_bit - Send a single bit
//=============================================================================
void i2c_bb_send_bit(uint8_t onebit)
{
    if (onebit) {
        SDA = 1;  // Release SDA for 1
    } else {
        SDA = 0;  // Pull SDA low for 0
    }
    I2C_DELAY();
    SCL = 1;  // Release SCL (clock pulse)
    I2C_DELAY();
    I2C_DELAY();
    SCL = 0;  // Pull SCL low
    I2C_DELAY();
}

//=============================================================================
// i2c_bb_read_bit - Read a single bit
//=============================================================================
uint8_t i2c_bb_read_bit(void)
{
    uint8_t onebit;
    SDA = 1;  // Release SDA (let slave pull if needed)
    I2C_DELAY();
    SCL = 1;  // Release SCL (clock pulse)
    I2C_DELAY();
    onebit = SDA;  // Read SDA
    I2C_DELAY();
    SCL = 0;  // Pull SCL low
    I2C_DELAY();
    return onebit;
}

//=============================================================================
// i2c_bb_send_byte - Send a byte and wait for ACK
// Returns: 1 if ACK received, 0 if NACK
//=============================================================================
uint8_t i2c_bb_send_byte(uint8_t byte)
{
    uint8_t i;
    uint8_t ack;
    
    // Send 8 bits
    for (i = 0; i < 8; i++) {
        i2c_bb_send_bit((byte >> (7 - i)) & 0x01);
    }
    
    // Read ACK bit
    ack = i2c_bb_read_bit();
    
    return (ack == 0) ? 1 : 0;  // ACK is low (0)
}

//=============================================================================
// i2c_bb_read_byte - Read a byte and send ACK/NACK
// Parameters: send_ack (1 = send ACK, 0 = send NACK)
// Returns: Byte read
//=============================================================================
uint8_t i2c_bb_read_byte(uint8_t send_ack)
{
    uint8_t i;
    uint8_t byte = 0;
    
    // Read 8 bits
    for (i = 0; i < 8; i++) {
        byte = (byte << 1) | i2c_bb_read_bit();
    }
    
    // Send ACK or NACK
    if (send_ack) {
        i2c_bb_send_bit(0);  // ACK (pull SDA low)
    } else {
        i2c_bb_send_bit(1);  // NACK (release SDA)
    }
    
    return byte;
}

//=============================================================================
// i2c_bb_write_byte - Write a byte to EEPROM at address
// Returns: 1 if successful, 0 if failed
//=============================================================================
uint8_t i2c_bb_write_byte(uint16_t address, uint8_t bytedata)
{
    uint8_t ack;
    
    // START condition
    i2c_bb_start();
    
    // Send slave address with WRITE bit (0xA0 = 0x50 << 1)
    ack = i2c_bb_send_byte(0xA0);
    if (!ack) {
        i2c_bb_stop();
        return 0;  // No ACK from slave
    }
    
    // Send address high byte
    ack = i2c_bb_send_byte((address >> 8) & 0xFF);
    if (!ack) {
        i2c_bb_stop();
        return 0;
    }
    
    // Send address low byte
    ack = i2c_bb_send_byte(address & 0xFF);
    if (!ack) {
        i2c_bb_stop();
        return 0;
    }
    
    // Send data byte
    ack = i2c_bb_send_byte(bytedata);
    if (!ack) {
        i2c_bb_stop();
        return 0;
    }
    
    // STOP condition
    i2c_bb_stop();
    
    // Wait for EEPROM write cycle (5ms typical)
    waitNms(10);
    
    return 1;  // Success
}

//=============================================================================
// i2c_bb_read_byte_eeprom - Read a byte from EEPROM at address
// Returns: Byte read (or 0xFF if failed)
//=============================================================================
uint8_t i2c_bb_read_byte_eeprom(uint16_t address)
{
    uint8_t ack;
    uint8_t bytedata;
    
    // START condition
    i2c_bb_start();
    
    // Send slave address with WRITE bit (to send address)
    ack = i2c_bb_send_byte(0xA0);
    if (!ack) {
        i2c_bb_stop();
        return 0xFF;
    }
    
    // Send address high byte
    ack = i2c_bb_send_byte((address >> 8) & 0xFF);
    if (!ack) {
        i2c_bb_stop();
        return 0xFF;
    }
    
    // Send address low byte
    ack = i2c_bb_send_byte(address & 0xFF);
    if (!ack) {
        i2c_bb_stop();
        return 0xFF;
    }
    
    // Repeated START
    i2c_bb_start();
    
    // Send slave address with READ bit (0xA1 = (0x50 << 1) | 1)
    ack = i2c_bb_send_byte(0xA1);
    if (!ack) {
        i2c_bb_stop();
        return 0xFF;
    }
    
    // Read data byte (send NACK to indicate last byte)
    bytedata = i2c_bb_read_byte(0);
    
    // STOP condition
    i2c_bb_stop();
    
    return bytedata;
}

//=============================================================================
// test_i2c_bitbang - Test EEPROM with bit-bang I2C
// Writes bytes 1-10 to addresses 0-9, then reads them back
//=============================================================================
void test_i2c_bitbang(void)
{
    unsigned char i;
    unsigned char read_value;
    unsigned char errors = 0;
    
    // Initialize I2C pins (should already be configured)
    SDA = 1;
    SCL = 1;
    
    // Small delay to let bus settle
    waitNms(10);
    
    // Write test: Write bytes 1-10 to addresses 0-9
    for (i = 0; i < 10; i++) {
        if (!i2c_bb_write_byte(i, i + 1)) {
            errors++;
        }
    }
    
    // Read test: Read bytes from addresses 0-9
    for (i = 0; i < 10; i++) {
        read_value = i2c_bb_read_byte_eeprom(i);
        
        // Check if read value matches written value
        if (read_value != (i + 1)) {
            errors++;
        }
    }
    
    // Store result in NUM_ERRORS
    NUM_ERRORS = errors;
}
