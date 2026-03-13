/*
 * idct_simd.h - SIMD-accelerated IDCT and color conversion
 */

#ifndef IDCT_SIMD_H
#define IDCT_SIMD_H

#include <stdint.h>

/*
 * SSE2 IDCT + dequantize for an 8x8 block.
 * Processes 8 values at once using 128-bit SSE2 registers.
 */
void idct_sse2_dequant_block(const int coefs[64], const uint16_t quant[64], uint8_t output[64]);

/*
 * SSE2 YCbCr to RGB conversion for a row of pixels.
 * Processes 8 pixels at a time.
 *
 * y_row:   Y component values
 * cb_row:  Cb component values (may be subsampled, caller handles mapping)
 * cr_row:  Cr component values
 * rgb_out: Output RGB24 data (3 bytes per pixel)
 * width:   Number of pixels to convert
 */
void ycbcr_to_rgb_sse2(const uint8_t *y_row, const uint8_t *cb_row,
                        const uint8_t *cr_row, uint8_t *rgb_out, int width);

/*
 * SSE2 dequantization (8 values at a time).
 */
void dequant_sse2(const int coefs[64], const uint16_t quant[64], int output[64]);

#endif /* IDCT_SIMD_H */
