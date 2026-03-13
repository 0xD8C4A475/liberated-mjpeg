/*
 * huffman.c - Huffman decoding implementation for JPEG
 *
 * Implements bit-by-bit Huffman decoding per ITU-T T.81 Annex F
 */

#include "huffman.h"
#include "claude_mjpeg.h"
#include <string.h>
#include <limits.h>

/*
 * Build decode tables from BITS and HUFFVAL arrays.
 * Per T.81 Annex C, Figure C.1 and C.2
 */
int huff_build_table(const jpeg_huff_table *src, huff_decode_table *dest)
{
    if (!src || !dest || !src->valid)
        return CMJ_ERROR_INVALID_ARG;

    memset(dest, 0, sizeof(*dest));
    memcpy(dest->huffval, src->huffval, sizeof(dest->huffval));
    dest->total_codes = src->total_codes;

    /* Generate code table per Figure C.1 */
    int code = 0;
    int si = 1;

    for (int i = 1; i <= 16; i++) {
        if (src->bits[i] > 0) {
            dest->valptr[i] = code;   /* Not index - will fix below */
            dest->mincode[i] = code;
            code += src->bits[i];
            dest->maxcode[i] = code - 1;
        } else {
            dest->maxcode[i] = -1;    /* No codes of this length */
            dest->mincode[i] = 0;
            dest->valptr[i] = 0;
        }
        code <<= 1;
        si++;
    }
    dest->maxcode[17] = 0xFFFFF;  /* Sentinel */

    /* Fix valptr to be index into huffval array */
    int j = 0;
    for (int i = 1; i <= 16; i++) {
        if (src->bits[i] > 0) {
            dest->valptr[i] = j - dest->mincode[i];
            j += src->bits[i];
        }
    }

    return CMJ_OK;
}

void bitstream_init(bitstream *bs, const uint8_t *data, size_t length)
{
    bs->data = data;
    bs->length = length;
    bs->byte_pos = 0;
    bs->bit_buffer = 0;
    bs->bits_left = 0;
}

/*
 * Read the next byte from the entropy-coded data,
 * handling byte stuffing (0xFF 0x00 -> 0xFF).
 * Returns the byte, or -1 on EOF.
 */
static int bitstream_read_byte(bitstream *bs)
{
    if (bs->byte_pos >= bs->length)
        return -1;

    uint8_t b = bs->data[bs->byte_pos++];

    if (b == 0xFF) {
        if (bs->byte_pos >= bs->length)
            return -1;

        uint8_t next = bs->data[bs->byte_pos];
        if (next == 0x00) {
            /* Byte stuffing: 0xFF 0x00 -> 0xFF */
            bs->byte_pos++;
            return 0xFF;
        } else if (next >= 0xD0 && next <= 0xD7) {
            /* RST marker - skip it and return next data byte */
            bs->byte_pos++;  /* Skip marker byte */
            /* Return the next data byte after RST */
            if (bs->byte_pos >= bs->length)
                return -1;
            return bs->data[bs->byte_pos++];
        } else {
            /* Other marker - end of data */
            return -1;
        }
    }

    return b;
}

/* Ensure at least 'count' bits are in the buffer */
static int bitstream_fill(bitstream *bs, int count)
{
    while (bs->bits_left < count) {
        int byte = bitstream_read_byte(bs);
        if (byte < 0) {
            /* Pad with zeros at end (common in JPEG) */
            bs->bit_buffer <<= 8;
            bs->bits_left += 8;
            if (bs->bits_left >= 32) break;
            continue;
        }
        bs->bit_buffer = (bs->bit_buffer << 8) | (uint32_t)byte;
        bs->bits_left += 8;
    }
    return 0;
}

int bitstream_read_bits(bitstream *bs, int count)
{
    if (count == 0) return 0;
    if (count > 25) return -1;  /* Safety limit */

    bitstream_fill(bs, count);

    bs->bits_left -= count;
    int value = (int)((bs->bit_buffer >> bs->bits_left) & ((1u << count) - 1));
    return value;
}

int bitstream_peek_bits(bitstream *bs, int count)
{
    if (count == 0) return 0;

    bitstream_fill(bs, count);

    int value = (int)((bs->bit_buffer >> (bs->bits_left - count)) & ((1u << count) - 1));
    return value;
}

void bitstream_skip_bits(bitstream *bs, int count)
{
    bitstream_fill(bs, count);
    bs->bits_left -= count;
}

/*
 * Decode one Huffman symbol.
 * Per T.81 Figure F.16 (DECODE procedure)
 */
int huff_decode_symbol(bitstream *bs, const huff_decode_table *table)
{
    int code = 0;

    for (int i = 1; i <= 16; i++) {
        int bit = bitstream_read_bits(bs, 1);
        if (bit < 0) return -1;

        code = (code << 1) | bit;

        if (code <= table->maxcode[i]) {
            int index = table->valptr[i] + code;
            if (index < 0 || index >= 256) return -1;
            return table->huffval[index];
        }
    }

    /* Code not found (shouldn't happen with valid data) */
    return -1;
}

/*
 * EXTEND function per T.81 Table F.1
 * Converts an unsigned value to its signed representation
 */
int huff_extend(int value, int category)
{
    /* If the MSB of value is 0, the value is negative */
    int vt = 1 << (category - 1);
    if (value < vt) {
        /* value = value - (2^category - 1) */
        value = value - (1 << category) + 1;
    }
    return value;
}

int decode_dc_coefficient(bitstream *bs, const huff_decode_table *dc_table, int prev_dc)
{
    /* Decode the DC category (number of additional bits) */
    int category = huff_decode_symbol(bs, dc_table);
    if (category < 0) return INT_MIN;

    if (category == 0) {
        /* DC difference is 0 */
        return prev_dc;
    }

    if (category > 11) return INT_MIN;  /* Invalid category */

    /* Read the additional bits */
    int value = bitstream_read_bits(bs, category);
    if (value < 0) return INT_MIN;

    /* Extend to signed value */
    int diff = huff_extend(value, category);

    return prev_dc + diff;
}

int decode_ac_coefficients(bitstream *bs, const huff_decode_table *ac_table, int block[64])
{
    /* block[0] should already be set (DC coefficient) */
    /* Decode 63 AC coefficients (positions 1-63 in zigzag order) */

    for (int k = 1; k < 64; ) {
        int symbol = huff_decode_symbol(bs, ac_table);
        if (symbol < 0) return -1;

        int run = (symbol >> 4) & 0x0F;    /* Zero run length */
        int category = symbol & 0x0F;       /* Value category */

        if (category == 0) {
            if (run == 0) {
                /* EOB (End of Block) - remaining coefficients are zero */
                while (k < 64)
                    block[k++] = 0;
                return 0;
            } else if (run == 0x0F) {
                /* ZRL (Zero Run Length) - 16 zeros */
                int end = k + 16;
                if (end > 64) return -1;
                while (k < end)
                    block[k++] = 0;
            } else {
                /* Invalid */
                return -1;
            }
        } else {
            /* 'run' zeros followed by a non-zero coefficient */
            int end = k + run;
            if (end >= 64) return -1;
            while (k < end)
                block[k++] = 0;

            /* Read and extend the coefficient value */
            int value = bitstream_read_bits(bs, category);
            if (value < 0) return -1;

            block[k++] = huff_extend(value, category);
        }
    }

    return 0;
}
