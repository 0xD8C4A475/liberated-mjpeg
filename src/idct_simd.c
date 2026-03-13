/*
 * idct_simd.c - SSE2-accelerated dequantization and color conversion
 *
 * The IDCT itself uses the optimized scalar AAN algorithm from idct_fast.c.
 * SIMD is used for:
 * - Dequantization (8 values at once)
 * - YCbCr to RGB conversion (8 pixels at once)
 */

#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__x86_64__) || defined(__SSE2__)

#include "idct_simd.h"
#include "idct_fast.h"
#include "jpeg_parser.h"
#include <emmintrin.h>  /* SSE2 */
#include <string.h>

/* SSE2 doesn't have _mm_mullo_epi32, emulate it */
static inline __m128i mullo_epi32_sse2(__m128i a, __m128i b)
{
    __m128i tmp1 = _mm_mul_epu32(a, b);
    __m128i tmp2 = _mm_mul_epu32(_mm_srli_si128(a, 4), _mm_srli_si128(b, 4));
    return _mm_unpacklo_epi32(
        _mm_shuffle_epi32(tmp1, _MM_SHUFFLE(0, 0, 2, 0)),
        _mm_shuffle_epi32(tmp2, _MM_SHUFFLE(0, 0, 2, 0))
    );
}

void idct_sse2_dequant_block(const int coefs[64], const uint16_t quant[64], uint8_t output[64])
{
    /* Use the scalar fast IDCT — it's already well-optimized.
     * The real SIMD win for the full pipeline is in color conversion. */
    idct_fast_dequant_block(coefs, quant, output);
}

void ycbcr_to_rgb_sse2(const uint8_t *y_row, const uint8_t *cb_row,
                        const uint8_t *cr_row, uint8_t *rgb_out, int width)
{
    int x = 0;
    __m128i zero = _mm_setzero_si128();
    __m128i bias128 = _mm_set1_epi16(128);

    /* BT.601 coefficients scaled to 14-bit fixed point (x16384):
     * 1.402   * 16384 = 22970
     * 0.34414 * 16384 =  5638
     * 0.71414 * 16384 = 11700
     * 1.772   * 16384 = 29032
     */
    __m128i cr_r_coeff = _mm_set1_epi16(22970);
    __m128i cb_g_coeff = _mm_set1_epi16(5638);
    __m128i cr_g_coeff = _mm_set1_epi16(11700);
    __m128i cb_b_coeff = _mm_set1_epi16(29032);

    /* Process 8 pixels at a time */
    for (; x + 7 < width; x += 8) {
        /* Load 8 values and expand to 16-bit */
        __m128i y8  = _mm_loadl_epi64((__m128i *)&y_row[x]);
        __m128i cb8 = _mm_loadl_epi64((__m128i *)&cb_row[x]);
        __m128i cr8 = _mm_loadl_epi64((__m128i *)&cr_row[x]);

        __m128i y16  = _mm_unpacklo_epi8(y8, zero);
        __m128i cb16 = _mm_unpacklo_epi8(cb8, zero);
        __m128i cr16 = _mm_unpacklo_epi8(cr8, zero);

        /* Offset Cb and Cr by -128 */
        __m128i cb_off = _mm_sub_epi16(cb16, bias128);
        __m128i cr_off = _mm_sub_epi16(cr16, bias128);

        /* R = Y + (22970 * Cr_off) >> 14 */
        __m128i r_adj = _mm_mulhi_epi16(cr_off, cr_r_coeff);
        r_adj = _mm_slli_epi16(r_adj, 2);  /* mulhi gives >>16, we need >>14 */
        __m128i r16 = _mm_add_epi16(y16, r_adj);

        /* G = Y - (5638 * Cb_off + 11700 * Cr_off) >> 14 */
        __m128i g_adj1 = _mm_mulhi_epi16(cb_off, cb_g_coeff);
        __m128i g_adj2 = _mm_mulhi_epi16(cr_off, cr_g_coeff);
        g_adj1 = _mm_slli_epi16(g_adj1, 2);
        g_adj2 = _mm_slli_epi16(g_adj2, 2);
        __m128i g16 = _mm_sub_epi16(y16, _mm_add_epi16(g_adj1, g_adj2));

        /* B = Y + (29032 * Cb_off) >> 14 */
        __m128i b_adj = _mm_mulhi_epi16(cb_off, cb_b_coeff);
        b_adj = _mm_slli_epi16(b_adj, 2);
        __m128i b16 = _mm_add_epi16(y16, b_adj);

        /* Pack to bytes with saturation (clamp 0-255) */
        __m128i r8out = _mm_packus_epi16(r16, zero);
        __m128i g8out = _mm_packus_epi16(g16, zero);
        __m128i b8out = _mm_packus_epi16(b16, zero);

        /* Interleave RGB24 - extract to scalar and store */
        uint8_t r_buf[8], g_buf[8], b_buf[8];
        _mm_storel_epi64((__m128i *)r_buf, r8out);
        _mm_storel_epi64((__m128i *)g_buf, g8out);
        _mm_storel_epi64((__m128i *)b_buf, b8out);

        uint8_t *dst = rgb_out + x * 3;
        for (int i = 0; i < 8; i++) {
            dst[i * 3 + 0] = r_buf[i];
            dst[i * 3 + 1] = g_buf[i];
            dst[i * 3 + 2] = b_buf[i];
        }
    }

    /* Remaining pixels: scalar fallback */
    for (; x < width; x++) {
        int Y = y_row[x];
        int cb_off = (int)cb_row[x] - 128;
        int cr_off = (int)cr_row[x] - 128;

        int r = Y + ((91881 * cr_off + 32768) >> 16);
        int g = Y - ((22554 * cb_off + 46802 * cr_off + 32768) >> 16);
        int b = Y + ((116130 * cb_off + 32768) >> 16);

        rgb_out[x * 3 + 0] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
        rgb_out[x * 3 + 1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
        rgb_out[x * 3 + 2] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
    }
}

void dequant_sse2(const int coefs[64], const uint16_t quant[64], int output[64])
{
    /* Process 4 values at a time (32-bit multiply) */
    for (int i = 0; i < 64; i += 4) {
        __m128i c = _mm_loadu_si128((__m128i *)&coefs[i]);
        /* Load 4 quant values (16-bit) and zero-extend to 32-bit */
        __m128i q16 = _mm_loadl_epi64((__m128i *)&quant[i]);
        __m128i q32 = _mm_unpacklo_epi16(q16, _mm_setzero_si128());
        /* Multiply */
        __m128i result = mullo_epi32_sse2(c, q32);
        _mm_storeu_si128((__m128i *)&output[i], result);
    }
}

#else
/* Non-x86 stub implementations */
#include "idct_simd.h"
#include "idct_fast.h"

void idct_sse2_dequant_block(const int coefs[64], const uint16_t quant[64], uint8_t output[64])
{
    idct_fast_dequant_block(coefs, quant, output);
}

void ycbcr_to_rgb_sse2(const uint8_t *y_row, const uint8_t *cb_row,
                        const uint8_t *cr_row, uint8_t *rgb_out, int width)
{
    for (int x = 0; x < width; x++) {
        ycbcr_to_rgb_fast(y_row[x], cb_row[x], cr_row[x],
                          &rgb_out[x*3], &rgb_out[x*3+1], &rgb_out[x*3+2]);
    }
}

void dequant_sse2(const int coefs[64], const uint16_t quant[64], int output[64])
{
    for (int i = 0; i < 64; i++)
        output[i] = coefs[i] * (int)quant[i];
}
#endif
