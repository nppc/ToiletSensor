#ifndef SRC_ADPCM_DECODER_H_
#define SRC_ADPCM_DECODER_H_

#define ADPCM_BLOCK_SIZE       256u // block size
#define ADPCM_PCM_BUF_SIZE     512u // decoded values per block
#define ADPCM_PCM_THRESHOLD    768u   /* Stop decoding when buffer reaches this level (75% full). */
#define ADPCM_DAC_MIDPOINT     2048u
#define ADPCM_BLOCK_HEADER_SIZE 4u


extern uint16_t g_fifoSamplesPending;  /* Decoded samples waiting in FIFO for the DAC. */
extern U16_U8 xdata g_pcmBuffer[]; /* FIFO storage for DAC samples. */
extern uint16_t g_pcmRd;               /* FIFO read index. */
extern uint16_t g_pcmCount;            /* Number of samples currently buffered. */

extern void ADPCM_Start(uint16_t streamLength);
extern void ADPCM_Task(void);
extern bit ADPCM_IsBusy(void);

#endif /* SRC_ADPCM_DECODER_H_ */
