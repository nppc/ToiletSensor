/******************************************************************************
*
* adpcm_decoder_efm8bb30.c
*
* IMA ADPCM mono decoder for EFM8BB30
* - strict C
* - reads encoded bytes from flash array (synchronous for early testing)
* - decodes into an XRAM PCM buffer (1024 samples)
* - processes 256-byte ADPCM blocks
* - feeds decoded samples to DAC via timer interrupt
*
* USAGE:
*   1. Define your ADPCM data: const uint8_t code g_adpcmFlashData[] = { ... };
*   2. Call ADPCM_Start(totalBytes) to initialize
*   3. Call ADPCM_Task() from main loop to fill PCM buffer
*   4. Timer ISR reads from buffer and writes to DAC
*
******************************************************************************/
#include "main.h"
#include "flash.h"
#include "i2c.h"
#include "adpcm_decoder.h"
static const int16_t code adpcmStepTable[89] =
{
    7,    8,    9,   10,   11,   12,   13,   14,
   16,   17,   19,   21,   23,   25,   28,   31,
   34,   37,   41,   45,   50,   55,   60,   66,
   73,   80,   88,   97,  107,  118,  130,  143,
  157,  173,  190,  209,  230,  253,  279,  307,
  337,  371,  408,  449,  494,  544,  598,  658,
  724,  796,  876,  963, 1060, 1166, 1282, 1411,
 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
 7132, 7845, 8630, 9493,10442,11487,12635,13899,
15289,16818,18500,20350,22385,24623,27086,29794,
32767
};
static const int8_t code adpcmIndexTable[16] =
{
  -1, -1, -1, -1,
   2,  4,  6,  8,
  -1, -1, -1, -1,
   2,  4,  6,  8
};
static uint16_t g_flashOffset;     /* Reserved for external flash positioning if needed. */
static uint16_t g_streamLength;    /* Total ADPCM stream length in bytes. */
static uint16_t g_blockSize;       /* Encoded block size: 256 or 512/1024 depending on source. */
static int16_t  g_predictor;       /* Current ADPCM predictor sample. */
static uint8_t  g_stepIndex;       /* Current ADPCM step-table index. */
static uint8_t  g_blockHeaderSize; /* Number of header bytes at the start of each block. */
static uint8_t  g_blockHeaderByteIndex;
static uint8_t  g_blockHeaderBytes[ADPCM_BLOCK_HEADER_SIZE];
static bit      g_needBlockHeaderLoad;
static uint16_t g_blockBaseOffset;      /* Absolute offset of the current block in flash. */
static uint16_t g_blockDataOffset;      /* First byte of compressed data for the current block. */
static uint16_t g_blockBytesRemaining;  /* Compressed bytes left to fetch in this block. */
static uint16_t g_blockCurrentOffset;   /* Current byte offset within block data (for fast address calc). */
bit      g_nibbleState;          /* 0 = low nibble next, 1 = high nibble next. */
static uint8_t  g_currentByte;         /* Cached encoded byte, split into two samples. */
bit      g_playing;             /* Nonzero while playback is active. */
bit      g_blockDone;           /* Set when the current block has been fully decoded. */
bit      g_bufferValid;         /* Set when buffer has at least one sample ready. */
bit      g_headerSamplePending; /* Set when the block header's verbatim predictor still needs */
uint8_t  g_pcmWr;               /* FIFO write index (0-255, wraps naturally). */
uint8_t g_pcmRd;               /* FIFO read index (0-255, wraps naturally). */
uint16_t g_pendingSample;
bit      g_havePendingSample;

/*
* PCM buffer in XRAM.
* 256 samples (U16_U8 is 2 bytes per sample = 512 bytes total).
* Index wraps naturally with uint8_t arithmetic (0-255).
*/
U16_U8 xdata g_pcmBuffer[ADPCM_PCM_BUF_SIZE]; /* FIFO storage for DAC samples. */
/*
* Nonblocking I2C EEPROM read state machine.
* Currently reads from flash array for testing.
* Replace I2C_HwStartEepromRead() and I2C_HwIsEepromReadReady() with real I2C driver.
*/
static uint16_t g_eepromReadAddress;
static bit      g_eepromReadArmed;
//static uint8_t  g_eepromReadByte;
static void I2C_HwStartEepromRead(uint16_t address)
{
   g_eepromReadAddress = address;
   g_eepromReadArmed = 1;
   if(eeprom_read_start(address) != 0)
   {
       g_eepromReadArmed = 0;
   }
}
static bit I2C_HwIsEepromReadReady(uint8_t *value)
{
   if(!g_eepromReadArmed)
       return 0;
   if(!eeprom_read_poll())
       return 0;
   *value = eeprom_read_get_byte();
   g_eepromReadArmed = 0;
   return 1;
}
static void I2C_EepromReadBegin(uint16_t address)
{
   g_eepromReadAddress = address;
   g_eepromReadArmed = 1;
   I2C_HwStartEepromRead(address);
}
static bit I2C_EepromReadPoll(uint16_t address, uint8_t *value)
{
   if(!g_eepromReadArmed || address != g_eepromReadAddress)
       I2C_EepromReadBegin(address);
   if(I2C_HwIsEepromReadReady(value))
   {
       return 1;
   }
   return 0;
}
/*===========================================================================
   SMALL HELPERS
   These helpers are for the background decoder path. The interrupt routine
   should avoid calling any function and should only touch the FIFO directly.
===========================================================================*/
static void ADPCM_ResetPcmFifo(void)
{
   uint16_t i;
  
   /* Prefill buffer with silence (2048 = midpoint). */
   for(i = 0u; i < ADPCM_PCM_BUF_SIZE; i++)
   {
       g_pcmBuffer[i].u16 = ADPCM_DAC_MIDPOINT;
   }
	/* Start write pointer ahead to give decoder room to fill before ISR catches up. */   
   g_pcmWr = ADPCM_PCM_HEADROOM;
   g_pcmRd = 0u;
   g_bufferValid = 1;  /* Buffer is prefilled with silence, so it's valid. */
}
static bit ADPCM_PcmFifoPush(uint16_t sample)
{
   uint8_t nextWr = (uint8_t)(g_pcmWr + 1u);
   uint8_t available = (uint8_t)(g_pcmRd - g_pcmWr - 1u);  /* Space available before catching read pointer. */
  
   /* Check if we have enough space (at least ADPCM_PCM_HEADROOM samples gap). */
   if(available < ADPCM_PCM_HEADROOM)
       return 0;  /* Not enough space; decoder should wait. */
  
   g_pcmBuffer[g_pcmWr].u16 = sample;
   g_pcmWr = nextWr;  /* Wraps naturally at 256. */
   g_bufferValid = 1;  /* Mark buffer as having valid data. */
   return 1;
}
static uint16_t ADPCM_DecodeNibble(uint8_t bytecode, int16_t *predictor, uint8_t *stepIndex)
{
   int16_t step;
   int16_t diff;
   int16_t nextIndex;
   int16_t dacSample;
   uint16_t dacValue;
   /* Clamp step index (rare, but safe). */
   if(*stepIndex > 88u)
       *stepIndex = 88u;
   step = adpcmStepTable[*stepIndex];
   /* Compute delta from 4-bit code using bit extraction. */
   diff = (int16_t)(step >> 3);
   if(bytecode & 1u) diff = (int16_t)(diff + (step >> 2));
   if(bytecode & 2u) diff = (int16_t)(diff + (step >> 1));
   if(bytecode & 4u) diff = (int16_t)(diff + step);
   /* Apply delta to predictor. */
   if(bytecode & 8u)
       *predictor = (int16_t)(*predictor - diff);
   else
       *predictor = (int16_t)(*predictor + diff);
   /* Saturate predictor to 16-bit range. */
   if(*predictor > 32767)
       *predictor = 32767;
   else if(*predictor < -32768)
       *predictor = -32768;
   /* Update step index (code & 0x0F is always 0-15, no bounds check needed). */
   nextIndex = (int16_t)(*stepIndex + adpcmIndexTable[bytecode & 0x0Fu]);
   if(nextIndex < 0)
       nextIndex = 0;
   else if(nextIndex > 88)
       nextIndex = 88;
   *stepIndex = (uint8_t)nextIndex;
   /*
    * Convert predictor to 12-bit DAC value.
    *
    * The predictor is a full-range signed 16-bit sample (+/-32767), but the
    * DAC only has 12 bits of unsigned range (0-4095, i.e. +/-2047 around
    * the midpoint). Simply adding the midpoint without scaling clips almost
    * every sample above roughly +/-2047, badly distorting the waveform.
    * Instead, scale the sample down by the 16-bit -> 12-bit ratio (4 bits,
    * i.e. divide by 16) before centering it, so the full dynamic range maps
    * evenly into the DAC's window. This is a single arithmetic shift, which
    * is cheap even on an 8-bit core.
    */
   dacSample = (int16_t)(*predictor >> 4);
   dacValue  = (uint16_t)(dacSample + ADPCM_DAC_MIDPOINT);
   if(dacValue > 4095u)
       dacValue = 4095u;
   return dacValue;
}
static bit ADPCM_ReadFlashByte(uint16_t address, uint8_t *value)
{
   *value = eeprom_read_byte_simple(address);
   return 1;
}
/*===========================================================================
   BLOCK STATE
===========================================================================*/
static bit ADPCM_LoadBlockHeader(void)
{
   /* Heavy path: block header fetch. Done in background, not in ISR. */
   if(g_blockSize < g_blockHeaderSize)
       return 0;

   while(g_blockHeaderByteIndex < g_blockHeaderSize)
   {
       uint16_t address = (uint16_t)(g_blockBaseOffset + g_blockHeaderByteIndex);
       if(!ADPCM_ReadFlashByte(address, &g_blockHeaderBytes[g_blockHeaderByteIndex]))
           return 0;
       g_blockHeaderByteIndex++;
   }

   g_predictor = (int16_t)((uint16_t)g_blockHeaderBytes[0] | ((uint16_t)g_blockHeaderBytes[1] << 8));
   g_stepIndex = g_blockHeaderBytes[2];
   g_blockDataOffset = (uint16_t)(g_blockBaseOffset + g_blockHeaderSize);
   g_blockBytesRemaining = (uint16_t)(g_blockSize - g_blockHeaderSize);
   g_blockCurrentOffset = 0u;  /* Reset offset tracker for this block. */
   g_nibbleState = 0;
   g_currentByte = 0u;
   g_blockDone = 0;
   g_headerSamplePending = 1;  /* Header predictor is a verbatim sample; emit it before any nibble. */
   g_blockHeaderByteIndex = 0;
   return 1;
}
static bit ADPCM_AdvanceToNextBlock(void)
{
   g_blockBaseOffset = (uint16_t)(g_blockBaseOffset + g_blockSize);
   if(g_blockBaseOffset >= g_streamLength)
   {
       g_playing = 0;
       return 0;
   }
   return ADPCM_LoadBlockHeader();
}
/*===========================================================================
   PUBLIC API
===========================================================================*/
void ADPCM_Start(uint16_t streamLength)
{
   g_streamLength = streamLength;
   g_blockSize = ADPCM_BLOCK_SIZE;
   g_blockHeaderSize = ADPCM_BLOCK_HEADER_SIZE;
   g_flashOffset = 0u;
   g_blockBaseOffset = 0u;
   g_playing = 1;
   g_blockDone = 0;
   g_needBlockHeaderLoad = 1;
   g_blockHeaderByteIndex = 0;
   g_headerSamplePending = 0;  /* Will be set by ADPCM_LoadBlockHeader(). */
   g_havePendingSample = 0;
   ADPCM_ResetPcmFifo();  /* Prefills with silence and resets indices. */
}
void ADPCM_Stop(void)
{
   g_playing = 0;
}
bit ADPCM_IsBusy(void)
{
   return g_playing;
}
/*===========================================================================
   DECODE ONE SAMPLE FROM CURRENT BLOCK
   Heavy path: block boundary handling and ADPCM decode. This stays in the
   background task so the interrupt can remain trivial.
===========================================================================*/
static bit ADPCM_DecodeOneSample(uint16_t *sampleOut)
{
   uint8_t bytecode;
   uint8_t byteValue;
   uint16_t flashAddress;
   if(!g_playing)
       return 0;
   /* Loop to handle block boundaries without recursion. */
   while(1)
   {
       if(g_needBlockHeaderLoad)
       {
           if(!ADPCM_LoadBlockHeader())
               return 0;
           g_needBlockHeaderLoad = 0;
       }
       if(g_headerSamplePending)
       {
           int16_t  dacSample;
           uint16_t dacValue;
           g_headerSamplePending = 0;
           /* Standard IMA ADPCM: the block header's predictor is a verbatim
            * 16-bit PCM sample, not nibble-encoded. It must be emitted as the
            * block's first sample, using the same 16-bit -> 12-bit DAC scaling
            * as ADPCM_DecodeNibble() applies to every other sample. */
           dacSample = (int16_t)(g_predictor >> 4);
           dacValue  = (uint16_t)(dacSample + ADPCM_DAC_MIDPOINT);
           if(dacValue > 4095u)
               dacValue = 4095u;
           *sampleOut = dacValue;
           return 1;
       }
       if(g_blockDone)
       {
           if(!ADPCM_AdvanceToNextBlock())
               return 0;
           continue;  /* Restart loop so the new block's header-pending flag is checked first. */
       }
       if(g_nibbleState == 0)
       {
           if(g_blockBytesRemaining == 0u)
           {
               g_blockDone = 1;
               continue;  /* Loop back to advance to next block. */
           }
           flashAddress = (uint16_t)(g_blockDataOffset + g_blockCurrentOffset);
           if(!ADPCM_ReadFlashByte(flashAddress, &byteValue))
               return 0;
           g_currentByte = byteValue;
           bytecode = (uint8_t)(g_currentByte & 0x0Fu);
           g_nibbleState = 1;
           g_blockBytesRemaining--;
           g_blockCurrentOffset++;
       }
       else
       {
           bytecode = (uint8_t)((g_currentByte >> 4) & 0x0Fu);
           g_nibbleState = 0;
       }
       *sampleOut = ADPCM_DecodeNibble(bytecode, &g_predictor, &g_stepIndex);
       return 1;
   }
}
/*===========================================================================
   BACKGROUND TASK
   Call this from the main loop. It fills the PCM FIFO when space is
   available. The flash read hook is intentionally asynchronous.
===========================================================================*/
void ADPCM_Task(void)
{
    /* Fill the FIFO from flash as long as space is available and playback is active. */
    while(g_playing)
    {
        /* If we have a sample from a previous failed push, try to push it first. */
        if(g_havePendingSample)
        {
            if(!ADPCM_PcmFifoPush(g_pendingSample))
                return;  /* Still full; retry next call. */
            g_havePendingSample = 0;
            continue;
        }

        /* Decode a new sample. */
        if(!ADPCM_DecodeOneSample(&g_pendingSample))
            return;  /* No more data. */

        /* Try to push it. If it fails, flag it as pending and exit. */
        if(!ADPCM_PcmFifoPush(g_pendingSample))
        {
            g_havePendingSample = 1;
            return;
        }
    }
}
/*===========================================================================
   TIMER ISR EXAMPLE
   Minimal ISR: just read from buffer and advance pointer.
   The decoder task maintains the headroom, so underruns should not occur.
  
   When playback ends (g_playing = 0), the ISR will continue reading silence
   from the prefilled buffer until the decoder catches up.
===========================================================================*/
/*
void Timer2_ISR(void) interrupt 5
{
   if(g_bufferValid)
   {
       DAC0L = g_pcmBuffer[g_pcmRd].u8[0];  // Low byte
       DAC0H = g_pcmBuffer[g_pcmRd].u8[1];  // High byte
       g_pcmRd++;                            // Advance and wrap naturally at 256.
      
       // Check if we've caught up to the write pointer (buffer empty).
       if(g_pcmRd == g_pcmWr)
           g_bufferValid = 0;  // Buffer is now empty.
   }
   else
   {
       // Buffer underrun: play silence.
       DAC0L = (uint8_t)(ADPCM_DAC_MIDPOINT & 0xFF);
       DAC0H = (uint8_t)((ADPCM_DAC_MIDPOINT >> 8) & 0xFF);
   }
}
*/
