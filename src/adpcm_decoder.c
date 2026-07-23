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
#include "adpcm_decoder.h"
#include "flash.h"
#include "i2c.h"
#include "player.h"
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
static int16_t  g_predictor;       /* Current ADPCM predictor sample. */
static uint8_t  g_stepIndex;       /* Current ADPCM step-table index. */
static uint8_t  g_blockHeaderByteIndex;
static uint8_t idata g_blockHeaderBytes[ADPCM_BLOCK_HEADER_SIZE];
static bit      g_needBlockHeaderLoad;
static uint16_t g_blockBaseOffset;      /* Absolute offset of the current block in flash. */
static uint16_t g_blockBytesRemaining;  /* Compressed bytes left to fetch in this block. */
bit      g_nibbleState;          /* 0 = low nibble next, 1 = high nibble next. */
static uint8_t  g_currentByte;         /* Cached encoded byte, split into two samples. */
bit      g_blockDone;           /* Set when the current block has been fully decoded. */
bit      g_headerSamplePending; /* Set when the block header's verbatim predictor still needs */


static int16_t ADPCM_DecodeNibble(uint8_t bytecode, int16_t *predictor, uint8_t *stepIndex)
{
   int16_t step;
   int16_t diff;
   int16_t nextIndex;
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
   return *predictor;
}


/* Pulls the next sequential byte in continuous I2C
 * transfer. No address is passed here because
 * the EEPROM's own address pointer auto-increments,
 */
static bit ADPCM_ReadFlashByte(uint8_t *value)
{
   if(!eeprom_read_continuous_poll()){
        return 0;  /* Not ready yet; caller will retry on its next invocation. */
   }

    *value = eeprom_read_continuous_get_byte();

    g_flashOffset++;

    return 1;
}

/*===========================================================================
   BLOCK STATE
===========================================================================*/
static bit ADPCM_LoadBlockHeader(void)
{
    while(g_blockHeaderByteIndex < ADPCM_BLOCK_HEADER_SIZE)
    {
        if(!ADPCM_ReadFlashByte(&g_blockHeaderBytes[g_blockHeaderByteIndex]))
        {
            return 0;
        }

        g_blockHeaderByteIndex++;
    }

    g_predictor = (int16_t)((uint16_t)g_blockHeaderBytes[0] | ((uint16_t)g_blockHeaderBytes[1] << 8));

    g_stepIndex = g_blockHeaderBytes[2];

    if(g_stepIndex > 88u)
    {
        g_stepIndex = 88u;
    }

    g_blockBytesRemaining = ADPCM_BLOCK_SIZE - ADPCM_BLOCK_HEADER_SIZE;

    g_nibbleState = 0;
    g_currentByte = 0u;

    g_blockDone = 0;
    g_headerSamplePending = 1;

    g_blockHeaderByteIndex = 0;

    return 1;
}

static bit ADPCM_AdvanceToNextBlock(void)
{
    g_blockBaseOffset = (uint16_t)(g_blockBaseOffset + ADPCM_BLOCK_SIZE);

    if(g_blockBaseOffset >= g_streamLength)
    {
        g_playing = 0;
        return 0;
    }

    g_needBlockHeaderLoad = 1;

    return 1;
}

/*===========================================================================
   PUBLIC API
===========================================================================*/
void ADPCM_Start(uint16_t startAddress, uint16_t streamLength)
{

   g_blockBaseOffset = 0u;
   g_blockDone = 0;
   g_needBlockHeaderLoad = 1;
   g_blockHeaderByteIndex = 0;
   g_headerSamplePending = 0;  /* Will be set by ADPCM_LoadBlockHeader(). */
   g_flashOffset = 0u;
   g_streamLength = streamLength;

   /* Start ONE continuous EEPROM read of the whole stream */
   if(eeprom_read_continuous_start(startAddress, streamLength) != 0)
    {
        g_playing = 0;
        return;
    }
}


/*===========================================================================
   DECODE ONE SAMPLE FROM CURRENT BLOCK
   Heavy path: block boundary handling and ADPCM decode. This stays in the
   background task so the interrupt can remain trivial.
===========================================================================*/
static bit ADPCM_DecodeOneSample(int16_t *sampleOut)
{
   uint8_t bytecode;
   uint8_t byteValue;
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
           g_headerSamplePending = 0;
           /* Standard IMA ADPCM: the block header's predictor is a verbatim
            * 16-bit PCM sample, not nibble-encoded. Return the raw PCM value
            * here; a separate helper scales it to the DAC range later. */
           *sampleOut = g_predictor;
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
           if(!ADPCM_ReadFlashByte(&byteValue))
               return 0;
           g_currentByte = byteValue;
           bytecode = (uint8_t)(g_currentByte & 0x0Fu);
           g_nibbleState = 1;
           g_blockBytesRemaining--;
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
void ADPCM_Task_interpolated(void)
{
    int16_t average;
    uint16_t dacAverage;
    uint16_t dacSample;

    while(g_playing)
    {
        /* Make sure we have a decoded raw PCM sample first. */
        if(!g_havePendingSample)
        {
            if(!ADPCM_DecodeOneSample(&g_pendingSample))
                return;  /* No more data. */

            g_havePendingSample = 1;
        }

        /* First output the interpolated sample, but only if we already
           have a previous real sample. */
        if(g_havePrevSample && g_interpolateNext)
        {
            average = (int16_t)(((int32_t)g_prevDecodedSample +
                                 (int32_t)g_pendingSample + 1l) >> 1);
            dacAverage = player_ScaleToDacSample(average);

            if(!player_FifoBufferPush(dacAverage))
                return;  /* FIFO full; retry average next call. */

            g_interpolateNext = 0;
        }

        /* Then scale and output the real decoded sample. */
        dacSample = player_ScaleToDacSample(g_pendingSample);
        if(!player_FifoBufferPush(dacSample))
            return;  /* FIFO full; retry this sample next call. */

        /* Save the raw sample as previous sample. */
        g_prevDecodedSample = g_pendingSample;
        g_havePrevSample = 1;

        /* Current sample is consumed. */
        g_havePendingSample = 0;

        /* Next decoded sample should have an interpolated value inserted
           before it. */
        g_interpolateNext = 1;
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
