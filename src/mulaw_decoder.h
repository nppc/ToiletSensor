#ifndef MULAW_DECODER_H
#define MULAW_DECODER_H

#include "main.h"

/*
 * Mu-Law (G.711) decoder for EFM8BB3 (8051-family).
 *
 * Unlike ADPCM, Mu-Law is memoryless: each input byte decodes to a sample
 * completely independently of any other byte. There is no predictor state,
 * no step index, no nibble packing, and no block headers. This makes the
 * decoder trivial and very fast - it's just a 256-entry table lookup.
 *
 * Usage:
 *   1. Call MULAW_Init() once before playback starts.
 *   2. Call MULAW_DecodeOneSample() once per output sample. It pulls one
 *      byte via MULAW_ReadByte() (implement this to read from your I2C
 *      EEPROM, flash, or wherever the compressed stream lives) and writes
 *      the decoded 16-bit signed PCM sample to *sampleOut.
 *   3. Stop playback by clearing g_mulawPlaying (or via MULAW_Stop()).
 *
 * MULAW_ReadByte() must be supplied by the application/integration layer,
 * exactly like ADPCM_ReadFlashByte() in the ADPCM decoder. It should
 * return 1 and populate *byteOut on success, or return 0 if no byte is
 * currently available (e.g. non-blocking I2C read still in flight) or if
 * the end of the stream has been reached.
 */

#define G_KVASHIMUSLUGAM_MULAW_ULAW_LENGTH 15975UL

/* Resets decoder state (there isn't much of it - no predictor to reset). */
extern void MULAW_Start(uint16_t startAddress, uint16_t streamLength);

extern void MULAW_Task_interpolated(void);


#endif /* MULAW_DECODER_H */
