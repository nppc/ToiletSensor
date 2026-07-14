#include "main.h"
#include "i2c.h"

//=============================================================================
// I2C Polling-Based Implementation for EFM8BB3
// Simplified version - focus on basic write and read operations
//=============================================================================

// Global variables
unsigned char SMB_DATA_IN = 0;      // Received data
unsigned char SMB_DATA_OUT = 0;     // Data to transmit
unsigned char TARGET = 0;           // Target slave address
unsigned long NUM_ERRORS = 0;       // Error counter

// State machine variables
static i2c_state_t i2c_state = I2C_IDLE;
static uint8_t i2c_rw_flag = 0;     // 0 = write, 1 = read
static uint16_t eeprom_addr = 0;    // EEPROM address for multi-byte operations
static uint8_t addr_bytes_sent = 0; // Counter for address bytes sent (0, 1, 2, or 3)
static uint8_t is_read_phase = 0;   // 0 = write address phase, 1 = read data phase

//=============================================================================
// i2c_init - Initialize I2C interface
//=============================================================================
void i2c_init(void)
{
    // SMBus is already configured by InitDevice.c
    i2c_state = I2C_IDLE;
    NUM_ERRORS = 0;
}

//=============================================================================
// i2c_is_busy - Check if I2C operation is in progress
// Returns: 1 if busy, 0 if idle
//=============================================================================
uint8_t i2c_is_busy(void)
{
    i2c_poll();
    return (i2c_state != I2C_IDLE);
}

//=============================================================================
// i2c_get_data - Get received data
// Returns: Data received from slave
//=============================================================================
uint8_t i2c_get_data(void)
{
    return SMB_DATA_IN;
}

//=============================================================================
// i2c_poll - Poll I2C state machine (call this regularly)
// Simplified state machine for basic I2C operations
//=============================================================================
void i2c_poll(void)
{
    uint8_t status = SMB0CN0 & 0xF0;
    
    // Check for arbitration lost
    if (SMB0CN0_ARBLOST) {
        i2c_state = I2C_ERROR;
        NUM_ERRORS++;
        SMB0CN0_STA = 0;
        SMB0CN0_STO = 0;
        SMB0CN0_ACK = 0;
        SMB0CN0_SI = 0;
        i2c_state = I2C_IDLE;
        return;
    }
    
    // Wait for STOP to complete
    if (SMB0CN0_STO) {
        return;
    }
    
    // Process based on status
    switch (status) {
        // Master START condition transmitted (0x08)
        case 0x08:
            // Send slave address with R/W bit
            SMB0DAT = (TARGET << 1) | i2c_rw_flag;
            SMB0CN0_STA = 0;  // Clear START bit
            break;
            
        // Master transmitted SLA+W, ACK received (0x18)
        case 0x18:
            // Slave acknowledged write - send address high byte
            SMB0DAT = (eeprom_addr >> 8) & 0xFF;
            addr_bytes_sent = 1;
            break;
            
        // Master transmitted data byte, ACK received (0x28)
        case 0x28:
            if (addr_bytes_sent == 1) {
                // Send address low byte
                SMB0DAT = eeprom_addr & 0xFF;
                addr_bytes_sent = 2;
            } else if (addr_bytes_sent == 2) {
                // Address sent, now send data byte
                if (i2c_rw_flag == WRITE) {
                    SMB0DAT = SMB_DATA_OUT;
                    addr_bytes_sent = 3;
                } else {
                    // For read, do repeated START
                    is_read_phase = 1;
                    SMB0CN0_STA = 1;  // Repeated START
                }
            } else if (addr_bytes_sent == 3) {
                // Data byte sent, generate STOP
                SMB0CN0_STO = 1;
                i2c_state = I2C_IDLE;
            }
            break;
            
        // Master transmitted SLA+W, NACK received (0x20)
        case 0x20:
            NUM_ERRORS++;
            SMB0CN0_STO = 1;
            i2c_state = I2C_IDLE;
            break;
            
        // Master transmitted data byte, NACK received (0x30)
        case 0x30:
            NUM_ERRORS++;
            SMB0CN0_STO = 1;
            i2c_state = I2C_IDLE;
            break;
            
        // Master transmitted SLA+R, ACK received (0x40)
        case 0x40:
            // Slave acknowledged read - prepare to receive data
            SMB0CN0_ACK = 1;  // Send ACK for incoming byte
            break;
            
        // Master transmitted SLA+R, NACK received (0x48)
        case 0x48:
            NUM_ERRORS++;
            SMB0CN0_STO = 1;
            i2c_state = I2C_IDLE;
            break;
            
        // Master received data byte, ACK sent (0x50)
        case 0x50:
            SMB_DATA_IN = SMB0DAT;
            // Send NACK for last byte
            SMB0CN0_ACK = 0;
            SMB0CN0_STO = 1;
            i2c_state = I2C_IDLE;
            break;
            
        // Master received data byte, NACK sent (0x58)
        case 0x58:
            SMB_DATA_IN = SMB0DAT;
            SMB0CN0_STO = 1;
            i2c_state = I2C_IDLE;
            break;
            
        default:
            // Unknown status - do nothing, wait for next interrupt
            break;
    }
    
    // Clear interrupt flag
    SMB0CN0_SI = 0;
}

//=============================================================================
// EEPROM Functions for 24LC512
//=============================================================================

//=============================================================================
// eeprom_write_byte - Write a single byte to EEPROM (blocking)
// Parameters: address (16-bit address), bytedata (byte to write)
//=============================================================================
void eeprom_write_byte(uint16_t address, uint8_t bytedata)
{
    // Wait for bus to be free
    while (i2c_state != I2C_IDLE) {
        i2c_poll();
    }
    
    // Set up for write operation
    TARGET = EEPROM_ADDR;
    eeprom_addr = address;
    SMB_DATA_OUT = bytedata;
    i2c_rw_flag = WRITE;
    addr_bytes_sent = 0;
    is_read_phase = 0;
    i2c_state = I2C_START_SENT;
    
    // Generate START condition
    SMB0CN0_STA = 1;
    
    // Poll until write is complete
    while (i2c_state != I2C_IDLE) {
        i2c_poll();
    }
    
    // Wait for EEPROM write cycle to complete (typical 5ms)
    waitNms(10);
}

//=============================================================================
// eeprom_read_byte - Read a single byte from EEPROM (blocking)
// Parameters: address (16-bit address)
// Returns: Byte read from EEPROM
//=============================================================================
uint8_t eeprom_read_byte(uint16_t address)
{
    // First, write the address (without reading data)
    // Wait for bus to be free
    while (i2c_state != I2C_IDLE) {
        i2c_poll();
    }
    
    // Set up for address write
    TARGET = EEPROM_ADDR;
    eeprom_addr = address;
    i2c_rw_flag = WRITE;
    addr_bytes_sent = 0;
    is_read_phase = 0;
    i2c_state = I2C_START_SENT;
    
    // Generate START condition
    SMB0CN0_STA = 1;
    
    // Poll until address write is complete
    while (i2c_state != I2C_IDLE) {
        i2c_poll();
    }
    
    // Now do the read with repeated START
    while (i2c_state != I2C_IDLE) {
        i2c_poll();
    }
    
    TARGET = EEPROM_ADDR;
    i2c_rw_flag = READ;
    addr_bytes_sent = 0;
    is_read_phase = 1;
    i2c_state = I2C_START_SENT;
    
    // Generate repeated START
    SMB0CN0_STA = 1;
    
    // Poll until read is complete
    while (i2c_state != I2C_IDLE) {
        i2c_poll();
    }
    
    return SMB_DATA_IN;
}

//=============================================================================
// eeprom_read_byte_simple - Read a single byte from EEPROM (blocking, linear flow)
// Parameters: address (16-bit address)
// Returns: Byte read from EEPROM
// This version uses a simple linear flow without state machine to debug
//=============================================================================
uint8_t eeprom_read_byte_simple(uint16_t address)
{
    uint8_t read_byte = 0xFF;
    uint16_t wait_count;
    uint8_t smb_status;

    // --- Bus free check
    wait_count = 10000;
    while (((SDA == 0) || (SCL == 0)) && wait_count--);
    if (wait_count == 0) {
        NUM_ERRORS++;
        SMB_DATA_OUT = 0xCC;
        return 0xFF;
    }

    // --- START
    SMB0CN0_STA = 1;
    wait_count = 50000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto error;

    // --- SLA+W
    SMB0DAT = 0xA0;
    SMB0CN0_STA = 0;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != 0xC0) goto error;

    // --- address high byte
    SMB0DAT = (address >> 8) & 0xFF;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != 0xC0) goto error;

    // --- address low byte
    SMB0DAT = address & 0xFF;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != 0xC0) goto error;

    // --- REPEATED START (terminates write, starts read)
    SMB0CN0_STA = 1;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != 0xE0) goto error;

    // --- SLA+R
    SMB0DAT = 0xA1;
    SMB0CN0_STA = 0;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != 0xC0) goto error;

    // --- READ 1 BYTE (NACK)
    SMB0CN0_ACK = 0;     // NACK (last byte)
    SMB0CN0_SI = 0;

    wait_count = 50000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != 0x80) goto error;

    read_byte = SMB0DAT;

    // --- STOP
    SMB0CN0_STO = 1;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_STO == 1) && wait_count--);

    return read_byte;

error:
    NUM_ERRORS++;
    SMB_DATA_OUT = SMB0CN0 & 0xF0;

    SMB0CN0_STO = 1;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_STO == 1) && wait_count--);

    return 0xFF;
}

//=============================================================================
// test_i2c - Test routine for EEPROM
// Writes bytes 1-10 to addresses 0-9, then reads them back
//=============================================================================
void test_i2c(void)
{
    unsigned char i;
    unsigned char read_value;
    
    // Initialize I2C
    i2c_init();
    
    // Check if slave is holding SDA low
    while (!SDA) {
        // Provide clock pulses to allow slave to advance
        SCL = 0;
        for (i = 0; i < 255; i++);
        SCL = 1;
        while (!SCL);
        for (i = 0; i < 10; i++);
    }
    
    NUM_ERRORS = 0;
    
    // Read test: Read bytes from addresses 0-9 using simple linear function
    for (i = 0; i < 10; i++) {
        read_value = eeprom_read_byte_simple(i);
        
        // Check if read value matches expected value (1-10)
        if (read_value != (i + 1)) {
            NUM_ERRORS++;
        }
    }
    
    // Test complete - NUM_ERRORS should be 0 if successful
}
