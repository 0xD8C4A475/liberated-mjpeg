/*
 * idct_fast.c - Optimized IDCT using AAN algorithm with fixed-point arithmetic
 *
 * Based on the AAN (Arai, Agui, Nakajima) fast IDCT algorithm.
 * Uses separated 1D row + column passes.
 * All computation uses 32-bit fixed-point integer arithmetic.
 *
 * Reference: Y. Arai, T. Agui, M. Nakajima,
 *   "A fast DCT-SQ scheme for images"
 *   Trans. IEICE, vol. E71, no. 11, pp. 1095-1097, 1988.
 */

#include "idct_fast.h"
#include "jpeg_parser.h"
#include <string.h>

/* Fixed-point precision: 12 bits fractional */
#define FP_BITS 12
#define FP_ONE  (1 << FP_BITS)
#define FP_HALF (1 << (FP_BITS - 1))

/* AAN constants scaled to fixed-point (cos(k*pi/16) * sqrt(2)) */
/* These are pre-scaled for the AAN algorithm */
#define FIX_0_298631336  ((int)(0.298631336 * FP_ONE + 0.5))  /* sqrt(2)*cos(7pi/16) */
#define FIX_0_390180644  ((int)(0.390180644 * FP_ONE + 0.5))  /* sqrt(2)*cos(6pi/16) */
#define FIX_0_541196100  ((int)(0.541196100 * FP_ONE + 0.5))  /* sqrt(2)*cos(6pi/16) */
#define FIX_0_765366865  ((int)(0.765366865 * FP_ONE + 0.5))  /* sqrt(2)*cos(2pi/16)-sqrt(2)*cos(6pi/16) */
#define FIX_0_899976223  ((int)(0.899976223 * FP_ONE + 0.5))  /* sqrt(2)*cos(3pi/16)-sqrt(2)*cos(5pi/16) */
#define FIX_1_175875602  ((int)(1.175875602 * FP_ONE + 0.5))  /* sqrt(2)*cos(pi/4) */
#define FIX_1_501321110  ((int)(1.501321110 * FP_ONE + 0.5))  /* sqrt(2)*cos(pi/16)-sqrt(2)*cos(5pi/16) */
#define FIX_1_847759065  ((int)(1.847759065 * FP_ONE + 0.5))  /* sqrt(2)*cos(2pi/16) */
#define FIX_1_961570560  ((int)(1.961570560 * FP_ONE + 0.5))  /* sqrt(2)*cos(3pi/16)+sqrt(2)*cos(5pi/16) */
#define FIX_2_053119869  ((int)(2.053119869 * FP_ONE + 0.5))  /* sqrt(2)*cos(pi/16)+sqrt(2)*cos(3pi/16) */
#define FIX_2_562915447  ((int)(2.562915447 * FP_ONE + 0.5))  /* sqrt(2)*cos(pi/16)+sqrt(2)*cos(5pi/16) */
#define FIX_3_072711026  ((int)(3.072711026 * FP_ONE + 0.5))  /* sqrt(2)*cos(pi/16) */

/* Fixed-point multiply */
#define FPMUL(a, b) (((a) * (b)) >> FP_BITS)

/* Clamp to 0-255 */
static inline uint8_t clamp_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/*
 * 1D IDCT on 8 elements using the AAN/Loeffler algorithm.
 * Input and output are in fixed-point.
 *
 * This is the classic "fast" IDCT used by libjpeg.
 * Based on Loeffler, Ligtenberg, Moschytz (1989).
 */
static void idct_1d(int *data)
{
    int tmp0, tmp1, tmp2, tmp3;
    int tmp10, tmp11, tmp12, tmp13;
    int z1, z2, z3, z4, z5;

    /* Even part */
    tmp0 = data[0];
    tmp1 = data[2];
    tmp2 = data[4];
    tmp3 = data[6];

    tmp10 = tmp0 + tmp2;   /* phase 3 */
    tmp11 = tmp0 - tmp2;

    tmp13 = tmp1 + tmp3;
    tmp12 = FPMUL(tmp1 - tmp3, FIX_1_847759065) - tmp13;  /* 2*cos(2*pi/8) */

    tmp0 = tmp10 + tmp13;  /* phase 2 */
    tmp3 = tmp10 - tmp13;
    tmp1 = tmp11 + tmp12;
    tmp2 = tmp11 - tmp12;

    /* Odd part */
    tmp10 = data[1];
    tmp11 = data[3];
    tmp12 = data[5];
    tmp13 = data[7];

    z5 = FPMUL(tmp10 - tmp13 + tmp12 - tmp11, FIX_1_175875602);
    z2 = FPMUL(tmp10 + tmp12, -FIX_0_390180644) + z5;   /* was: tmp4 + tmp6 */
    /* Actually let me use a cleaner decomposition */

    z1 = tmp10 + tmp13;
    z2 = tmp11 + tmp12;
    z3 = tmp10 + tmp12;
    z4 = tmp11 + tmp13;
    z5 = FPMUL(z3 + z4, FIX_1_175875602);

    tmp10 = FPMUL(tmp10, FIX_0_298631336);
    tmp11 = FPMUL(tmp11, FIX_2_053119869);
    tmp12 = FPMUL(tmp12, FIX_3_072711026);
    tmp13 = FPMUL(tmp13, FIX_1_501321110);
    z1 = FPMUL(z1, -FIX_0_899976223);
    z2 = FPMUL(z2, -FIX_2_562915447);
    z3 = FPMUL(z3, -FIX_1_961570560);
    z4 = FPMUL(z4, -FIX_0_390180644);

    z3 += z5;
    z4 += z5;

    tmp10 += z1 + z3;
    tmp11 += z2 + z4;
    tmp12 += z2 + z3;
    tmp13 += z1 + z4;

    data[0] = tmp0 + tmp13;
    data[7] = tmp0 - tmp13;
    data[1] = tmp1 + tmp12;
    data[6] = tmp1 - tmp12;
    data[2] = tmp2 + tmp11;
    data[5] = tmp2 - tmp11;
    data[3] = tmp3 + tmp10;
    data[4] = tmp3 - tmp10;
}

void idct_fast_dequant_block(const int coefs[64], const uint16_t quant[64], uint8_t output[64])
{
    int workspace[64];  /* Intermediate buffer */

    /* Step 1: Dequantize and de-zigzag into workspace (natural order) */
    /* Also scale up to fixed-point */
    for (int i = 0; i < 64; i++) {
        int natural_pos = jpeg_zigzag_order[i];
        workspace[natural_pos] = coefs[i] * (int)quant[i] * FP_ONE / 8;
        /* The /8 compensates for the 1/4 * 1/2 normalization that the
         * IDCT formula includes (done after both passes) */
    }

    /* Step 2: 1D IDCT on each row */
    for (int row = 0; row < 8; row++) {
        /* Quick check: if all AC terms are zero, shortcut */
        if (workspace[row*8+1] == 0 && workspace[row*8+2] == 0 &&
            workspace[row*8+3] == 0 && workspace[row*8+4] == 0 &&
            workspace[row*8+5] == 0 && workspace[row*8+6] == 0 &&
            workspace[row*8+7] == 0) {
            int dc = workspace[row*8+0];
            for (int i = 0; i < 8; i++)
                workspace[row*8+i] = dc;
            continue;
        }
        idct_1d(&workspace[row*8]);
    }

    /* Step 3: Transpose so column IDCT becomes row IDCT */
    int transposed[64];
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            transposed[c*8+r] = workspace[r*8+c];

    /* Step 4: 1D IDCT on each column (now rows after transpose) */
    for (int col = 0; col < 8; col++) {
        if (transposed[col*8+1] == 0 && transposed[col*8+2] == 0 &&
            transposed[col*8+3] == 0 && transposed[col*8+4] == 0 &&
            transposed[col*8+5] == 0 && transposed[col*8+6] == 0 &&
            transposed[col*8+7] == 0) {
            int dc = transposed[col*8+0];
            for (int i = 0; i < 8; i++)
                transposed[col*8+i] = dc;
            continue;
        }
        idct_1d(&transposed[col*8]);
    }

    /* Step 5: Transpose back, level shift (+128), and clamp */
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            /* Descale from fixed-point (two passes of IDCT, each with FP_BITS shift) */
            int val = transposed[c*8+r];
            /* Divide by FP_ONE with rounding, then add 128 */
            val = ((val + FP_HALF) >> FP_BITS) + 128;
            output[r*8+c] = clamp_u8(val);
        }
    }
}

/*
 * Integer YCbCr -> RGB conversion.
 * Uses 16-bit fixed-point arithmetic to avoid float entirely.
 *
 * BT.601 coefficients scaled by 2^16:
 *   R = Y + 1.402*(Cr-128)   -> Y + (91881*(Cr-128)) >> 16
 *   G = Y - 0.34414*(Cb-128) - 0.71414*(Cr-128) -> Y - (22554*(Cb-128) + 46802*(Cr-128)) >> 16
 *   B = Y + 1.772*(Cb-128)   -> Y + (116130*(Cb-128)) >> 16
 */
void ycbcr_to_rgb_fast(int y, int cb, int cr, uint8_t *r, uint8_t *g, uint8_t *b)
{
    int cb_off = cb - 128;
    int cr_off = cr - 128;

    int ri = y + ((91881 * cr_off + 32768) >> 16);
    int gi = y - ((22554 * cb_off + 46802 * cr_off + 32768) >> 16);
    int bi = y + ((116130 * cb_off + 32768) >> 16);

    *r = clamp_u8(ri);
    *g = clamp_u8(gi);
    *b = clamp_u8(bi);
}
