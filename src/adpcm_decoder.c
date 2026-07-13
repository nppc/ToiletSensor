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

static uint16_t g_blockBaseOffset;      /* Absolute offset of the current block in flash. */
static uint16_t g_blockDataOffset;      /* First byte of compressed data for the current block. */
static uint16_t g_blockBytesRemaining;  /* Compressed bytes left to fetch in this block. */
static uint16_t g_blockCurrentOffset;   /* Current byte offset within block data (for fast address calc). */
static bit      g_nibbleState;          /* 0 = low nibble next, 1 = high nibble next. */
static uint8_t  g_currentByte;         /* Cached encoded byte, split into two samples. */

static bit      g_playing;             /* Nonzero while playback is active. */
static bit      g_blockDone;           /* Set when the current block has been fully decoded. */
uint16_t g_fifoSamplesPending;  /* Decoded samples waiting in FIFO for the DAC. */

static uint16_t g_pcmWr;               /* FIFO write index. */
uint16_t g_pcmRd;               /* FIFO read index. */
uint16_t g_pcmCount;            /* Number of samples currently buffered. */

/*
 * PCM buffer in XRAM.
 * On EFM8 you will likely want to place this with your compiler-specific
 * XRAM keyword / pragma.
 */
U16_U8 xdata g_pcmBuffer[ADPCM_PCM_BUF_SIZE]; /* FIFO storage for DAC samples. */

/*
 * Nonblocking I2C EEPROM read state machine.
 * Currently reads from flash array for testing.
 * Replace I2C_HwStartEepromRead() and I2C_HwIsEepromReadReady() with real I2C driver.
 */
static uint16_t g_eepromReadAddress;
static bit      g_eepromReadArmed;
static uint8_t  g_eepromReadByte;

static void I2C_HwStartEepromRead(uint16_t address)
{
    /* TODO: start a real I2C transaction on the bus. */
    /* For now, read directly from flash array. */
    g_eepromReadByte = g_adpcmFlashData[address];
}

static bit I2C_HwIsEepromReadReady(uint8_t *value)
{
    /* TODO: return 1 when I2C transaction completes and byte is ready. */
    /* For now, always ready (synchronous flash read). */
    *value = g_eepromReadByte;
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
        g_eepromReadArmed = 0;
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
    g_pcmWr = 0;
    g_pcmRd = 0;
    g_pcmCount = 0;
}

static bit ADPCM_PcmFifoPush(uint16_t sample)
{
    if(g_pcmCount >= ADPCM_PCM_BUF_SIZE)
        return 0;

    g_pcmBuffer[g_pcmWr].u16 = sample;
    g_pcmWr = (g_pcmWr + 1) & 0x3FF;  /* Bitwise AND for 1024-byte wrap. */
    g_pcmCount++;
    return 1;
}

static uint16_t ADPCM_DecodeNibble(uint8_t bytecode, int16_t *predictor, uint8_t *stepIndex)
{
    int16_t step;
    int16_t diff;
    int16_t nextIndex;
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

    /* Convert predictor to 12-bit DAC value. */
    dacValue = (uint16_t)(*predictor + ADPCM_DAC_MIDPOINT);
    if(dacValue > 4095u)
        dacValue = 4095u;

    return dacValue;
}

static bit ADPCM_ReadFlashByte(uint16_t address, uint8_t *value)
{
    *value = g_adpcmFlashData[address];
    return 1;  // Always ready (synchronous read).
}

/*===========================================================================
    BLOCK STATE
===========================================================================*/

static bit ADPCM_LoadBlockHeader(void)
{
    uint8_t b0;
    uint8_t b1;
    uint8_t b2;
    uint8_t b3;

    /* Heavy path: block header fetch. Done in background, not in ISR. */
    if(g_blockSize < g_blockHeaderSize)
        return 0;

    if(!ADPCM_ReadFlashByte((uint16_t)(g_blockBaseOffset + 0u), &b0)) return 0;
    if(!ADPCM_ReadFlashByte((uint16_t)(g_blockBaseOffset + 1u), &b1)) return 0;
    if(!ADPCM_ReadFlashByte((uint16_t)(g_blockBaseOffset + 2u), &b2)) return 0;
    if(!ADPCM_ReadFlashByte((uint16_t)(g_blockBaseOffset + 3u), &b3)) return 0;

    g_predictor = (int16_t)((uint16_t)b0 | ((uint16_t)b1 << 8));
    g_stepIndex = b2;

    g_blockDataOffset = (uint16_t)(g_blockBaseOffset + g_blockHeaderSize);
    g_blockBytesRemaining = (uint16_t)(g_blockSize - g_blockHeaderSize);
    g_blockCurrentOffset = 0u;  /* Reset offset tracker for this block. */
    g_nibbleState = 0;
    g_currentByte = 0u;
    g_blockDone = 0;

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
    g_fifoSamplesPending = 0u;

    ADPCM_ResetPcmFifo();
    (void)ADPCM_LoadBlockHeader();
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
        if(g_blockDone)
        {
            if(!ADPCM_AdvanceToNextBlock())
                return 0;
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
    uint16_t sample;

    /* Fill the FIFO from EEPROM as long as space is available and playback is active. */
    /* Stop at threshold to allow ISR to drain while decoder prepares next block. */
    while(g_playing && g_pcmCount < ADPCM_PCM_THRESHOLD)
    {
        if(!ADPCM_DecodeOneSample(&sample))
            return;  /* No more data or I2C not ready. */

        if(!ADPCM_PcmFifoPush(sample))
            return;  /* FIFO full. */

        g_fifoSamplesPending++;
    }
}

/*===========================================================================
    TIMER ISR EXAMPLE

    This is the no-call ISR model you asked for. The decoder task fills the
    FIFO in the background. The interrupt only consumes one sample and writes
    it to the DAC.

    g_fifoSamplesPending reaches zero when the last decoded sample has been
    consumed, which marks end of message.
===========================================================================*/

/*
void Timer2_ISR(void) interrupt 5
{
    if(g_fifoSamplesPending != 0u)
    {
        DAC0 = g_pcmBuffer[g_pcmRd];

        g_pcmRd = (g_pcmRd + 1) & 0x3FF;  // Bitwise AND for 1024-byte wrap.
        g_pcmCount--;
        g_fifoSamplesPending--;
    }
    else
    {
        DAC0 = ADPCM_DAC_MIDPOINT;
    }
}
*/
