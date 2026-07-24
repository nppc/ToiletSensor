#include "main.h"
#include "mulaw_decoder.h"
#include "player.h"
#include "flash.h"

/*
 * Standard ITU-T G.711 Mu-Law decode table.
 *
 * Placed in `code` space (flash) via SDCC/Keil `code` keyword so it costs
 * zero bytes of XDATA/IDATA - it's read-only and never changes at runtime.
 * 256 entries * 2 bytes = 512 bytes of flash.
 *
 * Table generated directly from the reference G.711 decode formula:
 *   u_val   = ~raw_byte
 *   sign    = u_val & 0x80
 *   exp     = (u_val >> 4) & 0x07
 *   mantissa= u_val & 0x0F
 *   magnitude = ((mantissa << 3) + 0x84) << exp) - 0x84
 *   sample  = sign ? -magnitude : magnitude
 */
static const int16_t code g_mulawTable[256] =
{
    -32124, -31100, -30076, -29052, -28028, -27004, -25980, -24956,
    -23932, -22908, -21884, -20860, -19836, -18812, -17788, -16764,
    -15996, -15484, -14972, -14460, -13948, -13436, -12924, -12412,
    -11900, -11388, -10876, -10364,  -9852,  -9340,  -8828,  -8316,
     -7932,  -7676,  -7420,  -7164,  -6908,  -6652,  -6396,  -6140,
     -5884,  -5628,  -5372,  -5116,  -4860,  -4604,  -4348,  -4092,
     -3900,  -3772,  -3644,  -3516,  -3388,  -3260,  -3132,  -3004,
     -2876,  -2748,  -2620,  -2492,  -2364,  -2236,  -2108,  -1980,
     -1884,  -1820,  -1756,  -1692,  -1628,  -1564,  -1500,  -1436,
     -1372,  -1308,  -1244,  -1180,  -1116,  -1052,   -988,   -924,
      -876,   -844,   -812,   -780,   -748,   -716,   -684,   -652,
      -620,   -588,   -556,   -524,   -492,   -460,   -428,   -396,
      -372,   -356,   -340,   -324,   -308,   -292,   -276,   -260,
      -244,   -228,   -212,   -196,   -180,   -164,   -148,   -132,
      -120,   -112,   -104,    -96,    -88,    -80,    -72,    -64,
       -56,    -48,    -40,    -32,    -24,    -16,     -8,      0,
     32124,  31100,  30076,  29052,  28028,  27004,  25980,  24956,
     23932,  22908,  21884,  20860,  19836,  18812,  17788,  16764,
     15996,  15484,  14972,  14460,  13948,  13436,  12924,  12412,
     11900,  11388,  10876,  10364,   9852,   9340,   8828,   8316,
      7932,   7676,   7420,   7164,   6908,   6652,   6396,   6140,
      5884,   5628,   5372,   5116,   4860,   4604,   4348,   4092,
      3900,   3772,   3644,   3516,   3388,   3260,   3132,   3004,
      2876,   2748,   2620,   2492,   2364,   2236,   2108,   1980,
      1884,   1820,   1756,   1692,   1628,   1564,   1500,   1436,
      1372,   1308,   1244,   1180,   1116,   1052,    988,    924,
       876,    844,    812,    780,    748,    716,    684,    652,
       620,    588,    556,    524,    492,    460,    428,    396,
       372,    356,    340,    324,    308,    292,    276,    260,
       244,    228,    212,    196,    180,    164,    148,    132,
       120,    112,    104,     96,     88,     80,     72,     64,
        56,     48,     40,     32,     24,     16,      8,      0
};

uint16_t g_mulawDataCounter;

void MULAW_Start(uint16_t startAddress, uint16_t streamLength)
{

   g_mulawDataCounter = startAddress;
   g_streamLength = streamLength;

   g_playing = 1;


   g_havePendingSample = 0;

   g_havePrevSample = 0;        /* No previous sample yet for interpolation. */
   g_interpolateNext = 0;       /* Will be set once we have a pair. */


   player_ResetFifoBuffer();  /* Prefills with silence and resets indices. */

}


/*
 * Decodes exactly one sample.
 * Returns 1 and writes *sampleOut on success.
 * Returns 0 if playback isn't active, or MULAW_ReadByte() had no byte
 * ready (e.g. waiting on a non-blocking I2C transfer) - in that case
 * *sampleOut is left unmodified and the caller should try again later.
 */
bit MULAW_DecodeOneSample(int16_t *sampleOut)
{
    uint8_t rawByte;

    if(!g_playing)
        return 0;

    /* Single byte in, single sample out - no loop, no block state,
     * no recursion needed. This is the entire decoder. */
    if (g_mulawDataCounter >= g_streamLength) {
        g_playing = 0;
        return 0;  /* No byte available. */
    }
// TODO: uncomment for use
//    rawByte = g_kvashimuslugam_mulaw_ulaw[g_mulawDataCounter];
    *sampleOut = g_mulawTable[rawByte];
    g_mulawDataCounter++;
    return 1;
}


/*===========================================================================
   BACKGROUND TASK
   Call this from the main loop. It fills the PCM FIFO when space is
   available. The flash read hook is intentionally asynchronous.
===========================================================================*/
void MULAW_Task_interpolated(void)
{
    int16_t average;
    uint16_t dacAverage;
    uint16_t dacSample;

    while(g_playing)
    {
        /* Make sure we have a decoded raw PCM sample first. */
        if(!g_havePendingSample)
        {
            if(!MULAW_DecodeOneSample(&g_pendingSample))
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
