#include "main.h"
#include "i2c.h"
#include "flash.h"

//=============================================================================
// I2C Polling-Based Implementation for EFM8BB3
// Simplified version - focus on basic write and read operations
//=============================================================================

// Global variables
unsigned long NUM_ERRORS = 0;       // Error counter

// Non-blocking read state
//static uint8_t nb_read_state = 0;   // 0=idle, 1=waiting for byte
static uint8_t nb_read_byte = 0xFF;
static uint8_t nb_read_ready = 0;

// Non-blocking continuous read state
// remaining == 0 means the transaction is idle; non-zero means it is active.
static uint16_t nb_read_continuous_remaining = 0;
static uint8_t nb_read_continuous_byte_ready = 0;  // Byte latched by poll(), consumed by get_byte()

//=============================================================================
// i2c_init - Initialize I2C interface
//=============================================================================
void i2c_init(void)
{
    uint8_t i;
    NUM_ERRORS = 0;
    //nb_read_state = 0;
    nb_read_byte = 0xFF;
    nb_read_ready = 0;
    nb_read_continuous_remaining = 0;
    nb_read_continuous_byte_ready = 0;

    // Check if slave is holding SDA low
    while (!SDA) {
        // Provide clock pulses to allow slave to advance
        SCL = 0;
        for (i = 0; i < 255; i++);
        SCL = 1;
        while (!SCL);
        for (i = 0; i < 10; i++);
    }
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
        return 0xFF;
    }

    // --- START
    SMB0CN0_STA = 1;
    wait_count = 50000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto error;

    // --- SLA+W
    SMB0DAT = EEPROM_ADDR; // Slave address (0xA0)
    SMB0CN0_STA = 0;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MTDB) goto error;

    // --- address high byte
    SMB0DAT = (address >> 8) & 0xFF;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MTDB) goto error;

    // --- address low byte
    SMB0DAT = address & 0xFF;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MTDB) goto error;

    // --- REPEATED START (terminates write, starts read)
    SMB0CN0_STA = 1;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto error;

    //smb_status = SMB0CN0 & 0xF0;
    //if (smb_status != SMB_MTSTA) goto error;

    // --- SLA+R
    SMB0DAT = EEPROM_ADDR + 1; // SLA+R (0xA1)
    SMB0CN0_STA = 0;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MTDB) goto error;

    // --- READ 1 BYTE (NACK)
    SMB0CN0_ACK = 0;     // NACK (last byte)
    SMB0CN0_SI = 0;

    wait_count = 50000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MRDB) goto error;

    read_byte = SMB0DAT;

    // --- STOP
    SMB0CN0_STO = 1;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_STO == 1) && wait_count--);

    return read_byte;

error:
    NUM_ERRORS++;

    SMB0CN0_STO = 1;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_STO == 1) && wait_count--);

    return 0xFF;
}

//=============================================================================
// Non-blocking continuous EEPROM read helpers
//=============================================================================
// Typical usage:
//   if (eeprom_read_continuous_start(address, length) == 0) {
//       for loop{
//           if (eeprom_read_continuous_poll()) {
//               byte = eeprom_read_continuous_get_byte();
//           }
//       }
//   }
//
//=============================================================================
bit eeprom_read_continuous_start(uint16_t address, uint16_t length)
{
    uint16_t wait_count;
    uint8_t smb_status;

    if (length == 0) {
        return 1;
    }

    if (nb_read_continuous_remaining != 0) {
        return 1;  // Busy
    }

    // --- Bus free check
    wait_count = 10000;
    while (((SDA == 0) || (SCL == 0)) && wait_count--);
    if (wait_count == 0) {
        NUM_ERRORS++;
        return 1;
    }

    // --- START
    SMB0CN0_STA = 1;
    wait_count = 50000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto cont_error;

    // --- SLA+W
    SMB0DAT = EEPROM_ADDR;
    SMB0CN0_STA = 0;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto cont_error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MTDB) goto cont_error;

    // --- address high byte
    SMB0DAT = (address >> 8) & 0xFF;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto cont_error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MTDB) goto cont_error;

    // --- address low byte
    SMB0DAT = address & 0xFF;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto cont_error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MTDB) goto cont_error;

    // --- REPEATED START (terminates write, starts read)
    SMB0CN0_STA = 1;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto cont_error;

    // --- SLA+R
    SMB0DAT = EEPROM_ADDR + 1;
    SMB0CN0_STA = 0;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto cont_error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MTDB) goto cont_error;

    // Prepare for first byte
    SMB0CN0_ACK = (length > 1) ? 1 : 0;
    SMB0CN0_SI = 0;

    nb_read_continuous_remaining = length;
    nb_read_byte = 0xFF;
    nb_read_ready = 0;
    nb_read_continuous_byte_ready = 0;

    return 0;

cont_error:
    NUM_ERRORS++;
    SMB0CN0_STO = 1;
    SMB0CN0_SI = 0;
    return 1;
}

bit eeprom_read_continuous_poll(void)
{
    uint8_t smb_status;

    if (nb_read_continuous_remaining == 0) {
        return 0;
    }

    if (nb_read_continuous_byte_ready) {
        return 1;
    }

    if (!SMB0CN0_SI) {
        return 0;
    }

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MRDB) {
        NUM_ERRORS++;
        nb_read_continuous_remaining = 0;
        nb_read_continuous_byte_ready = 0;
        return 0;
    }

    nb_read_byte = SMB0DAT;
    nb_read_continuous_byte_ready = 1;

    nb_read_continuous_remaining--;
    if (nb_read_continuous_remaining == 0) {
        SMB0CN0_ACK = 0;
        SMB0CN0_STO = 1;
        SMB0CN0_SI = 0;
    } else {
        SMB0CN0_ACK = 1; //(nb_read_continuous_remaining == 1) ? 0 : 1;
        SMB0CN0_SI = 0;
    }

    return 1;
}

uint8_t eeprom_read_continuous_get_byte(void)
{
    if (!nb_read_continuous_byte_ready) {
        return 0xFF;
    }

    nb_read_continuous_byte_ready = 0;
    return nb_read_byte;
}

/*
//=============================================================================
// eeprom_page_write - Write a page (up to 128 bytes) to EEPROM (blocking)
// Parameters: address (16-bit starting address), data (pointer to byte array), length (number of bytes)
// Returns: 0 if success, 1 if error
//=============================================================================
uint8_t eeprom_page_write(uint16_t address, uint8_t *databytes, uint16_t length)
{
    uint16_t i;
    uint16_t wait_count;
    uint8_t smb_status;

    if (length == 0 || length > 128) {
        return 1;  // Invalid length
    }

    // --- Bus free check
    wait_count = 10000;
    while (((SDA == 0) || (SCL == 0)) && wait_count--);
    if (wait_count == 0) {
        NUM_ERRORS++;
        return 1;
    }

    // --- START
    SMB0CN0_STA = 1;
    wait_count = 50000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto page_write_error;

    // --- SLA+W
    SMB0DAT = EEPROM_ADDR;
    SMB0CN0_STA = 0;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto page_write_error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MTDB) goto page_write_error;

    // --- address high byte
    SMB0DAT = (address >> 8) & 0xFF;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto page_write_error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MTDB) goto page_write_error;

    // --- address low byte
    SMB0DAT = address & 0xFF;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto page_write_error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MTDB) goto page_write_error;

    // --- Write all data bytes
    for (i = 0; i < length; i++) {
        SMB0DAT = databytes[i];
        SMB0CN0_SI = 0;

        wait_count = 10000;
        while ((SMB0CN0_SI == 0) && wait_count--);
        if (wait_count == 0) goto page_write_error;

        smb_status = SMB0CN0 & 0xF0;
        if (smb_status != SMB_MTDB) goto page_write_error;
    }

    // --- STOP
    SMB0CN0_STO = 1;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_STO == 1) && wait_count--);

    // Wait for EEPROM write cycle to complete (typical 5ms)
    waitNms(10);

    return 0;

page_write_error:
    NUM_ERRORS++;
    SMB0CN0_STO = 1;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_STO == 1) && wait_count--);

    return 1;
}

//=============================================================================
// eeprom_read_continuous - Read multiple bytes from EEPROM in one transaction
// Parameters: address (16-bit starting address), buffer (destination), length (number of bytes)
// Returns: 0 if success, 1 if error
//=============================================================================
uint8_t eeprom_read_continuous(uint16_t address, uint8_t *buffer, uint16_t length)
{
    uint16_t i;
    uint16_t wait_count;
    uint8_t smb_status;

    if (length == 0 || buffer == NULL) {
        return 1;
    }

    // --- Bus free check
    wait_count = 10000;
    while (((SDA == 0) || (SCL == 0)) && wait_count--);
    if (wait_count == 0) {
        NUM_ERRORS++;
        return 1;
    }

    // --- START
    SMB0CN0_STA = 1;
    wait_count = 50000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto continuous_error;

    // --- SLA+W
    SMB0DAT = EEPROM_ADDR;
    SMB0CN0_STA = 0;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto continuous_error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MTDB) goto continuous_error;

    // --- address high byte
    SMB0DAT = (address >> 8) & 0xFF;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto continuous_error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MTDB) goto continuous_error;

    // --- address low byte
    SMB0DAT = address & 0xFF;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto continuous_error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MTDB) goto continuous_error;

    // --- REPEATED START (terminates write, starts read)
    SMB0CN0_STA = 1;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto continuous_error;

    // --- SLA+R
    SMB0DAT = EEPROM_ADDR + 1;
    SMB0CN0_STA = 0;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_SI == 0) && wait_count--);
    if (wait_count == 0) goto continuous_error;

    smb_status = SMB0CN0 & 0xF0;
    if (smb_status != SMB_MTDB) goto continuous_error;

    // --- Read bytes
    for (i = 0; i < length; i++) {
        SMB0CN0_ACK = (i < (length - 1)) ? 1 : 0;
        SMB0CN0_SI = 0;

        wait_count = 10000;
        while ((SMB0CN0_SI == 0) && wait_count--);
        if (wait_count == 0) goto continuous_error;

        smb_status = SMB0CN0 & 0xF0;
        if (smb_status != SMB_MRDB) goto continuous_error;

        buffer[i] = SMB0DAT;
    }

    // --- STOP
    SMB0CN0_STO = 1;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_STO == 1) && wait_count--);

    return 0;

continuous_error:
    NUM_ERRORS++;
    SMB0CN0_STO = 1;
    SMB0CN0_SI = 0;

    wait_count = 10000;
    while ((SMB0CN0_STO == 1) && wait_count--);

    return 1;
}

//=============================================================================
// test_write_flash_data - Write g_adpcmFlashData array to EEPROM starting at address 0
// This writes the entire 11264 byte array in 128-byte pages
//=============================================================================
void test_write_flash_data(void)
{
    extern const uint8_t code g_adpcmFlashData[];
    uint16_t address = 0;
    uint16_t bytes_written = 0;
    uint16_t total_bytes = 11264;
    uint8_t page_size = 128;
    uint8_t result;
    
    i2c_init();
    
    NUM_ERRORS = 0;
    
    // Write data in 128-byte pages
    while (bytes_written < total_bytes) {
        uint16_t bytes_to_write = total_bytes - bytes_written;
        if (bytes_to_write > page_size) {
            bytes_to_write = page_size;
        }
        
        // Write one page
        result = eeprom_page_write(address, (uint8_t *)&g_adpcmFlashData[bytes_written], bytes_to_write);
        
        if (result != 0) {
            NUM_ERRORS++;
            break;  // Stop on error
        }
        
        address += bytes_to_write;
        bytes_written += bytes_to_write;
    }
    
    // If all pages written successfully, NUM_ERRORS should be 0
}

//=============================================================================
// test_verify_flash_data - Verify g_adpcmFlashData was written correctly to EEPROM
// Reads back the data and compares with original
// Returns: 0 if all data matches, 1 if mismatch found
//=============================================================================
bit test_verify_flash_data(void)
{
    extern const uint8_t code g_adpcmFlashData[];
    uint16_t address = 0;
    uint16_t bytes_verified = 0;
    uint16_t total_bytes = 11264;
    uint8_t read_byte;
    uint8_t expected_byte;
    
    i2c_init();
    
    NUM_ERRORS = 0;
    
    // Read and verify all bytes
    while (bytes_verified < total_bytes) {
        read_byte = eeprom_read_byte_simple(address);
        expected_byte = g_adpcmFlashData[address];
        
        if (read_byte != expected_byte) {
            NUM_ERRORS++;
            // Continue checking to find all mismatches
        }
        
        address++;
        bytes_verified++;
    }
    
    // Return 0 if all bytes match, 1 if any mismatch found
    return (NUM_ERRORS == 0) ? 0 : 1;
}

*/
