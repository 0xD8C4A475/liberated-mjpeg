/*
 * huffman_fast.h - Optimized Huffman decoding with multi-bit lookup tables
 *
 * Uses an 8-bit first-level lookup table for O(1) decode of most common symbols,
 * falling back to bit-by-bit decode for longer codes.
 */

#ifndef HUFFMAN_FAST_H
#define HUFFMAN_FAST_H

#include "jpeg_parser.h"
#include "huffman.h"
#include <stdint.h>

#define HUFF_LOOKAHEAD 8  /* Bits in first-level lookup table */

/* Fast Huffman decode table with lookup acceleration */
typedef struct {
    /* First-level lookup: 256 entries (2^HUFF_LOOKAHEAD) */
    /* Each entry: upper 8 bits = symbol value, lower 8 bits = code length */
    /* If code length > HUFF_LOOKAHEAD, fall back to slow path */
    uint16_t lookup[1 << HUFF_LOOKAHEAD];

    /* Slow-path decode tables (same as huff_decode_table) */
    huff_decode_table slow;
} huff_fast_table;

/*
 * Build a fast Huffman decode table from parsed DHT data.
 */
int huff_fast_build(const jpeg_huff_table *src, huff_fast_table *dest);

/*
 * Decode one Huffman symbol using the fast lookup table.
 */
int huff_fast_decode(bitstream *bs, const huff_fast_table *table);

/*
 * Fast DC coefficient decode.
 */
int decode_dc_fast(bitstream *bs, const huff_fast_table *dc_table, int prev_dc);

/*
 * Fast AC coefficients decode.
 */
int decode_ac_fast(bitstream *bs, const huff_fast_table *ac_table, int block[64]);

#endif /* HUFFMAN_FAST_H */
