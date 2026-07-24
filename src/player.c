#include "main.h"
#include "player.h"
#include "adpcm_decoder.h"

/*
* PCM buffer in XRAM.
* 256 samples (U16_U8 is 2 bytes per sample = 512 bytes total).
* Index wraps naturally with uint8_t arithmetic (0-255).
*/
U16_U8 xdata g_pcmBuffer[PLAYER_BUF_SIZE]; /* FIFO storage for DAC samples. */
bit      g_bufferValid;         /* Set when buffer has at least one sample ready. */
uint8_t  g_pcmWr;               /* FIFO write index (0-255, wraps naturally). */
uint8_t g_pcmRd;               /* FIFO read index (0-255, wraps naturally). */


uint16_t g_streamLength;    /* Total ADPCM stream length in bytes. */
bit g_playing;             /* Nonzero while playback is active. */

int16_t g_pendingSample;
bit      g_havePendingSample;

/* Interpolation state: keep previous decoded raw sample to average with current. */
int16_t g_prevDecodedSample;
bit      g_havePrevSample;      /* Set when g_prevDecodedSample is valid. */
bit      g_interpolateNext;     /* Set when we need to push average before pushing real sample. */

void player_Start(void){
  g_playing = 1;


  g_havePendingSample = 0;

  g_havePrevSample = 0;        /* No previous sample yet for interpolation. */
  g_interpolateNext = 0;       /* Will be set once we have a pair. */


  player_ResetFifoBuffer();  /* Prefills with silence and resets indices. */
}

bit player_IsBusy(void)
{
   return g_playing;
}

uint16_t player_ScaleToDacSample(int16_t sampleByte)
{
   int16_t dacSample;
   uint16_t dacValue;

   /*
    * The sample byte is a full-range signed 16-bit sample (+/-32767), but the
    * DAC only has 12 bits of unsigned range (0-4095, i.e. +/-2047 around
    * the midpoint). Simply adding the midpoint without scaling clips almost
    * every sample above roughly +/-2047, badly distorting the waveform.
    * Instead, scale the sample down by the 16-bit -> 12-bit ratio (4 bits,
    * i.e. divide by 16) before centering it, so the full dynamic range maps
    * evenly into the DAC's window. This is a single arithmetic shift, which
    * is cheap even on an 8-bit core.
    */
   dacSample = (int16_t)(sampleByte >> 4);
   dacValue  = (uint16_t)(dacSample + DAC_MIDPOINT);
   if(dacValue > 4095u)
       dacValue = 4095u;
   return dacValue;
}

/*===========================================================================
   SMALL HELPERS
===========================================================================*/
void player_ResetFifoBuffer(void)
{
   uint16_t i;

   /* Prefill buffer with silence (2048 = midpoint).
    * Only need to prefil the HEADROOM area */
   for(i = 0u; i < PLAYER_HEADROOM; i++)
   {
       g_pcmBuffer[i].u16 = DAC_MIDPOINT;
   }
  /* Start write pointer ahead to give decoder room to fill before ISR catches up. */
   g_pcmWr = PLAYER_HEADROOM;
   g_pcmRd = 0u;
   g_bufferValid = 1;  /* Buffer is prefilled with silence, so it's valid. */
}

bit player_FifoBufferPush(uint16_t sample)
{
   uint8_t nextWr = (uint8_t)(g_pcmWr + 1u);
   uint8_t available = (uint8_t)(g_pcmRd - g_pcmWr - 1u);  /* Space available before catching read pointer. */

   /* Check if we have enough space (at least PLAYER_HEADROOM samples gap). */
   if(available < PLAYER_HEADROOM)
       return 0;  /* Not enough space; decoder should wait. */

   g_pcmBuffer[g_pcmWr].u16 = sample;
   g_pcmWr = nextWr;  /* Wraps naturally at 256. */
   g_bufferValid = 1;  /* Mark buffer as having valid data. */
   return 1;
}

void player_play_sample(uint16_t start_address, uint16_t data_len){
  player_Start();
  ADPCM_Start(start_address, data_len);
  while(player_IsBusy()){
      ADPCM_Task_interpolated();
  }
}
