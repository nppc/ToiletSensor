/*
 * i2c.h
 *
 *  Created on: 23 Oct 2022
 *      Author: Pavel
 */

#ifndef SRC_I2C_H_
#define SRC_I2C_H_

//-----------------------------------------------------------------------------
// Global CONSTANTS
//-----------------------------------------------------------------------------
#define WRITE 0x00 // SMBus WRITE command
#define READ 0x01 // SMBus READ command
// Device addresses (7 bits, LSB is a don’t care)
#define SLAVE_ADDR 0x8 // Device address for slave target
// Status vector - top 4 bits only
#define SMB_MTSTA (SMB0CN0_MASTER__BMASK | SMB0CN0_TXMODE__BMASK | SMB0CN0_STA__BMASK) // (MT) start transmitted
#define SMB_MTDB (SMB0CN0_MASTER__BMASK | SMB0CN0_TXMODE__BMASK) // (MT) data byte transmitted
#define SMB_MRDB (SMB0CN0_MASTER__BMASK) // (MR) data byte received
// End status vector definition
//-----------------------------------------------------------------------------
// Global VARIABLES
//-----------------------------------------------------------------------------
extern unsigned char SMB_DATA_IN; // Global holder for SMBus data
 // All receive data is written here
extern unsigned char SMB_DATA_OUT; // Global holder for SMBus data.
 // All transmit data is read from here
extern unsigned char TARGET; // Target SMBus slave address
extern bit SMB_BUSY; // Software flag to indicate when the
 // SMB_Read() or SMB_Write() functions
 // have claimed the SMBus
extern bit SMB_RW; // Software flag to indicate the
 // direction of the current transfer
extern unsigned long NUM_ERRORS; // Counter for the number of errors.

SI_SBIT(SDA, SFR_P0, 1);
SI_SBIT(SCL, SFR_P0, 2);

//-----------------------------------------------------------------------------
// Function PROTOTYPES
//-----------------------------------------------------------------------------
void SMB_Write (void);
void SMB_Read (void);
void test_i2c (void);



#endif /* SRC_I2C_H_ */
