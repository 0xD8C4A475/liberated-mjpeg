/*
 * huffman_fast.c - Optimized Huffman decoding with 8-bit lookup tables
 *
 * For codes <= 8 bits long (vast majority of JPEG symbols), we can
 * decode in a single table lookup instead of bit-by-bit scanning.
 */

#include "huffman_fast.h"
#include "claude_mjpeg.h"
#include <string.h>
#include <limits.h>

int huff_fast_build(const jpeg_huff_table *src, huff_fast_table *dest)
{
    if (!src || !dest || !src->valid)
        return CMJ_ERROR_INVALID_ARG;

    memset(dest, 0, sizeof(*dest));

    /* Build the slow-path table first */
    int ret = huff_build_table(src, &dest->slow);
    if (ret != CMJ_OK)
        return ret;

    /* Build the fast lookup table */
    /* For each code of length <= HUFF_LOOKAHEAD, fill all lookup entries
     * that start with that code */
    int code = 0;
    int huffval_idx = 0;

    for (int len = 1; len <= 16; len++) {
        for (int i = 0; i < src->bits[len]; i++) {
            uint8_t symbol = src->huffval[huffval_idx++];

            if (len <= HUFF_LOOKAHEAD) {
                /* Fill all entries where the top 'len' bits match this code */
                int pad_bits = HUFF_LOOKAHEAD - len;
                int base = code << pad_bits;
                int count = 1 << pad_bits;

                for (int j = 0; j < count; j++) {
                    dest->lookup[base + j] = (uint16_t)((symbol << 8) | len);
                }
            }

            code++;
        }
        code <<= 1;
    }

    return CMJ_OK;
}

int huff_fast_decode(bitstream *bs, const huff_fast_table *table)
{
    /* Peek at HUFF_LOOKAHEAD bits */
    int peek = bitstream_peek_bits(bs, HUFF_LOOKAHEAD);
    if (peek < 0) return -1;

    uint16_t entry = table->lookup[peek];
    int len = entry & 0xFF;

    if (len > 0 && len <= HUFF_LOOKAHEAD) {
        /* Fast path: found in lookup table */
        bitstream_skip_bits(bs, len);
        return (entry >> 8) & 0xFF;
    }

    /* Slow path: code is longer than HUFF_LOOKAHEAD bits */
    /* Consume the peeked bits and continue bit by bit */
    bitstream_skip_bits(bs, HUFF_LOOKAHEAD);
    int code = peek;

    for (int i = HUFF_LOOKAHEAD + 1; i <= 16; i++) {
        int bit = bitstream_read_bits(bs, 1);
        if (bit < 0) return -1;
        code = (code << 1) | bit;

        if (code <= table->slow.maxcode[i]) {
            int index = table->slow.valptr[i] + code;
            if (index < 0 || index >= 256) return -1;
            return table->slow.huffval[index];
        }
    }

    return -1;
}

int decode_dc_fast(bitstream *bs, const huff_fast_table *dc_table, int prev_dc)
{
    int category = huff_fast_decode(bs, dc_table);
    if (category < 0) return INT_MIN;
    if (category == 0) return prev_dc;
    if (category > 11) return INT_MIN;

    int value = bitstream_read_bits(bs, category);
    if (value < 0) return INT_MIN;

    int diff = huff_extend(value, category);
    return prev_dc + diff;
}

int decode_ac_fast(bitstream *bs, const huff_fast_table *ac_table, int block[64])
{
    for (int k = 1; k < 64; ) {
        int symbol = huff_fast_decode(bs, ac_table);
        if (symbol < 0) return -1;

        int run = (symbol >> 4) & 0x0F;
        int category = symbol & 0x0F;

        if (category == 0) {
            if (run == 0) {
                /* EOB */
                while (k < 64)
                    block[k++] = 0;
                return 0;
            } else if (run == 0x0F) {
                /* ZRL */
                int end = k + 16;
                if (end > 64) return -1;
                while (k < end)
                    block[k++] = 0;
            } else {
                return -1;
            }
        } else {
            int end = k + run;
            if (end >= 64) return -1;
            while (k < end)
                block[k++] = 0;

            int value = bitstream_read_bits(bs, category);
            if (value < 0) return -1;
            block[k++] = huff_extend(value, category);
        }
    }

    return 0;
}
