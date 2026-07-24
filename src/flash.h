/*
 * flash.h
 *
 *  Created on: Jul 13, 2026
 *      Author: Pavel
 */

#ifndef SRC_FLASH_H_
#define SRC_FLASH_H_

/*===========================================================================
    PLATFORM HOOKS

    External flash array containing ADPCM data for testing.
    For production, replace with real I2C EEPROM driver.
===========================================================================*/

/* External flash array: declare in your main code or linker script. */
extern const uint8_t code g_adpcmFlashData_bak[];
extern const uint8_t code g_adpcmFlashData_zapolneno[];
extern const uint8_t code g_adpcmFlashData_30[];
extern const uint8_t code g_adpcmFlashData_50[];
extern const uint8_t code g_adpcmFlashData_75[];
extern const uint8_t code g_adpcmFlashData_procentov[];
extern const uint8_t code g_adpcmFlashData_pochti[];
extern const uint8_t code g_adpcmFlashData_polny[];
extern const uint8_t code g_adpcmFlashData_pustoj[];
extern const uint8_t code g_adpcmFlashData_oblegchenije[];
extern const uint8_t code g_adpcmFlashData_kvashimuslugam[];

//extern const uint8_t code g_kvashimuslugam_mulaw_ulaw[];

#endif /* SRC_FLASH_H_ */
