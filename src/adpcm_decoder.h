#ifndef SRC_ADPCM_DECODER_H_
#define SRC_ADPCM_DECODER_H_

#define ADPCM_BLOCK_SIZE       256u // block size
#define ADPCM_BLOCK_HEADER_SIZE 4u

extern void ADPCM_Start(uint16_t startAddress, uint16_t streamLength);
extern bit ADPCM_IsBusy(void);
extern void ADPCM_Task(void);
extern void ADPCM_Task_interpolated(void);

#endif /* SRC_ADPCM_DECODER_H_ */
