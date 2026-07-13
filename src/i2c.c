#include "main.h"
#include "i2c.h"

// Example software to demonstrate the C8051F33x SMBus interface in
// Master mode.
// - Interrupt-driven SMBus implementation
// - Only master states defined (no slave or arbitration)
// - 1-byte SMBus data holders used for each transmit and receive
// - Timer1 used as SMBus clock source
// - Timer3 used by SMBus for SCL low timeout detection
// - SCL frequency defined by <SMB_FREQUENCY> constant
// - ARBLOST support included
// - Pinout:
// P0.1 -> SDA (SMBus)
// P0.2 -> SCL (SMBus)
//
// P1.3 -> LED

unsigned char SMB_DATA_IN; // Global holder for SMBus data
 // All receive data is written here
unsigned char SMB_DATA_OUT; // Global holder for SMBus data.
 // All transmit data is read from here
unsigned char TARGET; // Target SMBus slave address
bit SMB_BUSY; // Software flag to indicate when the
 // SMB_Read() or SMB_Write() functions
 // have claimed the SMBus
bit SMB_RW; // Software flag to indicate the
 // direction of the current transfer
unsigned long NUM_ERRORS; // Counter for the number of errors.


//-----------------------------------------------------------------------------
// MAIN Routine
//-----------------------------------------------------------------------------
//
// Main routine performs all configuration tasks, then loops forever sending
// and receiving SMBus data to the slave <SLAVE_ADDR>.
//
void test_i2c (void)
{
 unsigned char i; // Dummy variable counters

 // If slave is holding SDA low because of an improper SMBus reset or error
 while(!SDA)
 {
   // Provide clock pulses to allow the slave to advance out
   // of its current state. This will allow it to release SDA.
   SCL = 0; // Drive the clock low
   for(i = 0; i < 255; i++); // Hold the clock low
   SCL = 1; // Release the clock
   while(!SCL); // Wait for open-drain
   // clock output to rise
   for(i = 0; i < 10; i++); // Hold the clock high
 }

 // low timeout detect
 // TEST CODE-------------------------------------------------------------------
  NUM_ERRORS = 0; // Error counter
    // SMBus Write Sequence
    TARGET = SLAVE_ADDR; // Target the F3xx/Si8250 Slave for next
    // SMBus transfer
    SMB_DATA_OUT = 0x00; // Register Address
    SMB_Write(); // Initiate SMBus write
    SMB_DATA_OUT = 0x9C; // 7V
    SMB_Write(); // Initiate SMBus write
//    // SMBus Read Sequence
//    TARGET = SLAVE_ADDR; // Target the F3xx/Si8250 Slave for next
//    // SMBus transfer
//    SMB_Read();
//    // Check transfer data
//    if(SMB_DATA_IN != SMB_DATA_OUT) { // Received data match transmit data?
//        NUM_ERRORS++; // Increment error counter if no match
//    }
    // Run to here to view the SMB_DATA_IN and SMB_DATA_OUT variables
    //waitNms (1000); // Wait 1 ms until the next cycle
 // END TEST CODE---------------------------------------------------------------
 }


//-----------------------------------------------------------------------------
// Support Functions
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// SMB_Write
//-----------------------------------------------------------------------------
//
// Return Value : None
// Parameters : None
//
// Writes a single byte to the slave with address specified by the <TARGET>
// variable.
// Calling sequence:
// 1) Write target slave address to the <TARGET> variable
// 2) Write outgoing data to the <SMB_DATA_OUT> variable
// 3) Call SMB_Write()
//
void SMB_Write (void)
{
 while (SMB_BUSY); // Wait for SMBus to be free.
 SMB_BUSY = 1; // Claim SMBus (set to busy)
 SMB_RW = 0; // Mark this transfer as a WRITE
 SMB0CN0_STA = 1; // Start transfer
}
//-----------------------------------------------------------------------------
// SMB_Read
//-----------------------------------------------------------------------------
//
// Return Value : None
// Parameters : None
//
// Reads a single byte from the slave with address specified by the <TARGET>
// variable.
// Calling sequence:
// 1) Write target slave address to the <TARGET> variable
// 2) Call SMB_Write()
// 3) Read input data from <SMB_DATA_IN> variable
//
void SMB_Read (void)
{
   while (SMB_BUSY); // Wait for bus to be free.
   SMB_BUSY = 1; // Claim SMBus (set to busy)
   SMB_RW = 1; // Mark this transfer as a READ
   SMB0CN0_STA = 1; // Start transfer
   while (SMB_BUSY); // Wait for transfer to complete
}
  //-----------------------------------------------------------------------------
 // End Of File
 //-----------------------------------------------------------------------------
