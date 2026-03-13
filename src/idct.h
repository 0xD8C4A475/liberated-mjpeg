/*
 * idct.h - IDCT and pixel reconstruction for JPEG decoding
 */

#ifndef IDCT_H
#define IDCT_H

#include <stdint.h>

/*
 * Dequantize and perform 2D IDCT on an 8x8 block of DCT coefficients.
 *
 * coefs:      Input: 64 DCT coefficients in zigzag order
 * quant:      Quantization table values (64 entries, zigzag order)
 * output:     Output: 64 pixel values (0-255), row-major order
 */
void idct_dequant_block(const int coefs[64], const uint16_t quant[64], uint8_t output[64]);

/*
 * Convert YCbCr to RGB using BT.601 coefficients.
 *
 * y, cb, cr:  Input component values (0-255)
 * r, g, b:    Output RGB values (0-255, clamped)
 */
void ycbcr_to_rgb(int y, int cb, int cr, uint8_t *r, uint8_t *g, uint8_t *b);

#endif /* IDCT_H */
