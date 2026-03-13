/*
 * idct_fast.h - Optimized IDCT using AAN (Arai, Agui, Nakajima) algorithm
 *
 * Separated row/column 1D passes with fixed-point integer arithmetic.
 * Replaces the naive O(N^4) 2D IDCT with O(N^2 * N) = O(N^3).
 */

#ifndef IDCT_FAST_H
#define IDCT_FAST_H

#include <stdint.h>

/*
 * Fast IDCT + dequantize for an 8x8 block.
 *
 * coefs:  64 DCT coefficients in zigzag order
 * quant:  Quantization table values (64 entries, zigzag order)
 * output: 64 pixel values (0-255), row-major order
 */
void idct_fast_dequant_block(const int coefs[64], const uint16_t quant[64], uint8_t output[64]);

/*
 * Integer YCbCr to RGB conversion (no floating point).
 * Uses 16-bit fixed point scaling.
 */
void ycbcr_to_rgb_fast(int y, int cb, int cr, uint8_t *r, uint8_t *g, uint8_t *b);

#endif /* IDCT_FAST_H */
