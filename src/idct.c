/*
 * idct.c - Inverse DCT and color conversion for JPEG decoding
 *
 * Implements the standard Type-II DCT inverse per T.81 section A.3.3
 * and YCbCr to RGB conversion using BT.601 coefficients.
 */

#include "idct.h"
#include "jpeg_parser.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Clamp value to 0-255 */
static inline uint8_t clamp_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/*
 * Naive 2D IDCT using the direct formula from T.81 A.3.3
 * This is O(N^4) and will be optimized later.
 *
 * S(u,v) = input DCT coefficients (after dequantization)
 * s(x,y) = output spatial values
 *
 * s(x,y) = 1/4 * sum_u=0..7 sum_v=0..7 C(u)*C(v)*S(u,v)
 *            * cos((2x+1)*u*pi/16) * cos((2y+1)*v*pi/16)
 *
 * where C(0) = 1/sqrt(2), C(k) = 1 for k > 0
 */
void idct_dequant_block(const int coefs[64], const uint16_t quant[64], uint8_t output[64])
{
    double dequant[64];
    double result[64];

    /* Step 1: Dequantize (coefficients are in zigzag order, output in natural order) */
    for (int i = 0; i < 64; i++) {
        int natural_pos = jpeg_zigzag_order[i];
        dequant[natural_pos] = (double)coefs[i] * (double)quant[i];
    }

    /* Step 2: 2D IDCT */
    double c[8];
    c[0] = 1.0 / sqrt(2.0);
    for (int i = 1; i < 8; i++)
        c[i] = 1.0;

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            double sum = 0.0;

            for (int v = 0; v < 8; v++) {
                for (int u = 0; u < 8; u++) {
                    double s = dequant[v * 8 + u];
                    double cu = c[u];
                    double cv = c[v];
                    double cos_x = cos((2.0 * x + 1.0) * u * M_PI / 16.0);
                    double cos_y = cos((2.0 * y + 1.0) * v * M_PI / 16.0);
                    sum += cu * cv * s * cos_x * cos_y;
                }
            }

            result[y * 8 + x] = sum / 4.0;
        }
    }

    /* Step 3: Level shift (add 128) and clamp */
    for (int i = 0; i < 64; i++) {
        int val = (int)(result[i] + 128.5);  /* Round and level shift */
        output[i] = clamp_u8(val);
    }
}

/*
 * YCbCr to RGB conversion using BT.601 / JFIF standard:
 *
 * R = Y                    + 1.402   * (Cr - 128)
 * G = Y - 0.344136 * (Cb - 128) - 0.714136 * (Cr - 128)
 * B = Y + 1.772    * (Cb - 128)
 */
void ycbcr_to_rgb(int y, int cb, int cr, uint8_t *r, uint8_t *g, uint8_t *b)
{
    double cb_off = (double)(cb - 128);
    double cr_off = (double)(cr - 128);

    int ri = (int)(y + 1.402 * cr_off + 0.5);
    int gi = (int)(y - 0.344136 * cb_off - 0.714136 * cr_off + 0.5);
    int bi = (int)(y + 1.772 * cb_off + 0.5);

    *r = clamp_u8(ri);
    *g = clamp_u8(gi);
    *b = clamp_u8(bi);
}
