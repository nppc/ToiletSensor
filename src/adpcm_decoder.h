#ifndef SRC_ADPCM_DECODER_H_
#define SRC_ADPCM_DECODER_H_

#define ADPCM_BLOCK_SIZE       256u // block size
#define ADPCM_PCM_BUF_SIZE     256u // decoded values (512 bytes, uint8_t wraps naturally)
#define ADPCM_PCM_HEADROOM     40u  // Minimum samples to keep ahead of ISR.
#define ADPCM_DAC_MIDPOINT     2048u
#define ADPCM_BLOCK_HEADER_SIZE 4u


extern U16_U8 xdata g_pcmBuffer[]; // FIFO storage for DAC samples (256 samples).
extern uint8_t g_pcmRd;            // FIFO read index (wraps automatically).
extern uint8_t g_pcmWr;            // FIFO write index (wraps automatically).
extern bit g_bufferValid;

extern void ADPCM_Start(uint16_t streamLength);
extern bit ADPCM_IsBusy(void);
extern void ADPCM_Task(void);
extern void ADPCM_Task_interpolated(void);

#endif /* SRC_ADPCM_DECODER_H_ */
