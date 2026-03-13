/*
 * huffman.h - Huffman decoding for JPEG
 *
 * Builds lookup tables from DHT data and provides bitstream-level decoding
 * per ITU-T T.81 Annex C and F.2.2
 */

#ifndef HUFFMAN_H
#define HUFFMAN_H

#include "jpeg_parser.h"
#include <stdint.h>
#include <stddef.h>

/* Huffman decode table (built from DHT) */
typedef struct {
    /* Standard decode: code -> symbol lookup */
    int maxcode[18];      /* Max code value for each code length + 1 sentinel */
    int mincode[18];      /* Min code value for each code length */
    int valptr[18];       /* Index into huffval for first code of each length */
    uint8_t huffval[256]; /* Symbol values */
    int total_codes;
} huff_decode_table;

/* Bitstream reader for entropy-coded data */
typedef struct {
    const uint8_t *data;  /* Pointer to ECS data */
    size_t length;        /* Total length of ECS data */
    size_t byte_pos;      /* Current byte position */
    uint32_t bit_buffer;  /* Bit accumulator */
    int bits_left;        /* Number of valid bits in buffer */
} bitstream;

/*
 * Build a Huffman decode table from parsed DHT data.
 *
 * src:  parsed Huffman table (BITS + HUFFVAL from DHT)
 * dest: decode table to fill
 *
 * Returns 0 on success.
 */
int huff_build_table(const jpeg_huff_table *src, huff_decode_table *dest);

/*
 * Initialize a bitstream reader.
 */
void bitstream_init(bitstream *bs, const uint8_t *data, size_t length);

/*
 * Read N bits from the bitstream (MSB first).
 * Returns the bits as an unsigned value, or -1 on error.
 */
int bitstream_read_bits(bitstream *bs, int count);

/*
 * Peek at the next N bits without consuming them.
 */
int bitstream_peek_bits(bitstream *bs, int count);

/*
 * Skip N bits.
 */
void bitstream_skip_bits(bitstream *bs, int count);

/*
 * Decode one Huffman symbol from the bitstream.
 *
 * bs:    bitstream reader
 * table: Huffman decode table
 *
 * Returns the decoded symbol (0-255), or -1 on error.
 */
int huff_decode_symbol(bitstream *bs, const huff_decode_table *table);

/*
 * Extend a value to its signed representation.
 * Per T.81 section F.2.2.1, Table F.1
 *
 * value:    raw bits read from stream
 * category: number of bits (1-11)
 *
 * Returns the signed coefficient value.
 */
int huff_extend(int value, int category);

/*
 * Decode the DC coefficient for one 8x8 block.
 *
 * bs:      bitstream reader
 * dc_table: DC Huffman decode table
 * prev_dc: previous DC value (for differential coding)
 *
 * Returns the DC coefficient value, or INT_MIN on error.
 */
int decode_dc_coefficient(bitstream *bs, const huff_decode_table *dc_table, int prev_dc);

/*
 * Decode the 63 AC coefficients for one 8x8 block.
 *
 * bs:       bitstream reader
 * ac_table: AC Huffman decode table
 * block:    output array of 64 coefficients (block[0] is set by caller for DC)
 *
 * Returns 0 on success, -1 on error.
 */
int decode_ac_coefficients(bitstream *bs, const huff_decode_table *ac_table, int block[64]);

#endif /* HUFFMAN_H */
