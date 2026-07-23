/*
 * player.h
 *
 *  Created on: Jul 23, 2026
 *      Author: Pavel
 */

#ifndef SRC_PLAYER_H_
#define SRC_PLAYER_H_

#define PLAYER_BUF_SIZE     256u // decoded values (512 bytes, uint8_t wraps naturally)
#define PLAYER_HEADROOM     40u  // Minimum samples to keep ahead of ISR.

#define DAC_MIDPOINT     2048u

extern U16_U8 xdata g_pcmBuffer[]; // FIFO storage for DAC samples (256 samples).
extern uint8_t g_pcmRd;            // FIFO read index (wraps automatically).
extern uint8_t g_pcmWr;            // FIFO write index (wraps automatically).
extern bit     g_bufferValid;

extern uint16_t g_streamLength;    /* Total ADPCM stream length in bytes. */
extern bit     g_playing;             /* Nonzero while playback is active. */

extern int16_t g_pendingSample;
extern bit     g_havePendingSample;

/* Interpolation state: keep previous decoded raw sample to average with current. */
extern int16_t g_prevDecodedSample;
extern bit     g_havePrevSample;      /* Set when g_prevDecodedSample is valid. */
extern bit     g_interpolateNext;     /* Set when we need to push average before pushing real sample. */

extern void player_Start(void);
extern uint16_t player_ScaleToDacSample(int16_t sampleByte);
extern bit player_FifoBufferPush(uint16_t sample);
extern void player_ResetFifoBuffer(void);
extern bit player_IsBusy(void);

#endif /* SRC_PLAYER_H_ */
