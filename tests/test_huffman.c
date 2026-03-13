/*
 * test_huffman.c - Unit tests for Huffman decoding
 */

#include "../src/huffman.h"
#include "../src/jpeg_parser.h"
#include "claude_mjpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST(name) static int test_##name(void)
#define RUN_TEST(name) do { \
    printf("  %-40s ", #name); \
    if (test_##name() == 0) { printf("PASS\n"); passed++; } \
    else { printf("FAIL\n"); failed++; } \
    total++; \
} while(0)

/* Standard JPEG luminance DC Huffman table (Table K.3) */
static const jpeg_huff_table std_dc_lum = {
    .bits = {0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
    .huffval = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
    .total_codes = 12,
    .valid = 1
};

/* Standard JPEG luminance AC Huffman table (Table K.5) */
static const jpeg_huff_table std_ac_lum = {
    .bits = {0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D},
    .huffval = {
        0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
        0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
        0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
        0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
        0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16,
        0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
        0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
        0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
        0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
        0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
        0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
        0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
        0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
        0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
        0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
        0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
        0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4,
        0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
        0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
        0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
        0xF9, 0xFA
    },
    .total_codes = 162,
    .valid = 1
};

/* Test: build DC Huffman table */
TEST(build_dc_table)
{
    huff_decode_table table;
    int ret = huff_build_table(&std_dc_lum, &table);
    if (ret != CMJ_OK) {
        fprintf(stderr, "    build_table failed: %d\n", ret);
        return -1;
    }
    if (table.total_codes != 12) {
        fprintf(stderr, "    expected 12 codes, got %d\n", table.total_codes);
        return -1;
    }
    return 0;
}

/* Test: build AC Huffman table */
TEST(build_ac_table)
{
    huff_decode_table table;
    int ret = huff_build_table(&std_ac_lum, &table);
    if (ret != CMJ_OK) {
        fprintf(stderr, "    build_table failed: %d\n", ret);
        return -1;
    }
    if (table.total_codes != 162) {
        fprintf(stderr, "    expected 162 codes, got %d\n", table.total_codes);
        return -1;
    }
    return 0;
}

/* Test: bitstream reader */
TEST(bitstream_basic)
{
    uint8_t data[] = { 0xA5, 0x3C };  /* 10100101 00111100 */
    bitstream bs;
    bitstream_init(&bs, data, 2);

    /* Read 4 bits: should be 1010 = 10 */
    int val = bitstream_read_bits(&bs, 4);
    if (val != 0x0A) {
        fprintf(stderr, "    expected 0x0A, got 0x%02X\n", val);
        return -1;
    }

    /* Read 8 bits: should be 01010011 = 0x53 */
    val = bitstream_read_bits(&bs, 8);
    if (val != 0x53) {
        fprintf(stderr, "    expected 0x53, got 0x%02X\n", val);
        return -1;
    }

    /* Read 4 bits: should be 1100 = 12 */
    val = bitstream_read_bits(&bs, 4);
    if (val != 0x0C) {
        fprintf(stderr, "    expected 0x0C, got 0x%02X\n", val);
        return -1;
    }

    return 0;
}

/* Test: byte stuffing in bitstream */
TEST(bitstream_byte_stuffing)
{
    /* 0xFF 0x00 should read as 0xFF */
    uint8_t data[] = { 0xFF, 0x00, 0x42 };
    bitstream bs;
    bitstream_init(&bs, data, 3);

    int val = bitstream_read_bits(&bs, 8);
    if (val != 0xFF) {
        fprintf(stderr, "    expected 0xFF, got 0x%02X\n", val);
        return -1;
    }

    val = bitstream_read_bits(&bs, 8);
    if (val != 0x42) {
        fprintf(stderr, "    expected 0x42, got 0x%02X\n", val);
        return -1;
    }

    return 0;
}

/* Test: huff_extend function */
TEST(extend_values)
{
    /* Category 1: 0 -> -1, 1 -> 1 */
    if (huff_extend(0, 1) != -1) return -1;
    if (huff_extend(1, 1) != 1) return -1;

    /* Category 2: 0 -> -3, 1 -> -2, 2 -> 2, 3 -> 3 */
    if (huff_extend(0, 2) != -3) return -1;
    if (huff_extend(1, 2) != -2) return -1;
    if (huff_extend(2, 2) != 2) return -1;
    if (huff_extend(3, 2) != 3) return -1;

    /* Category 3: 0-3 -> -(7..4), 4-7 -> 4..7 */
    if (huff_extend(0, 3) != -7) return -1;
    if (huff_extend(3, 3) != -4) return -1;
    if (huff_extend(4, 3) != 4) return -1;
    if (huff_extend(7, 3) != 7) return -1;

    return 0;
}

/* Test: decode DC coefficient from hand-crafted bitstream */
TEST(decode_dc)
{
    huff_decode_table dc_table;
    huff_build_table(&std_dc_lum, &dc_table);

    /*
     * Standard DC lum table: category 0 has code "00" (2 bits)
     * Category 0 means DC difference = 0
     * So data 0x00 = 00000000 should decode category 0 from first 2 bits
     */
    uint8_t data[] = { 0x00, 0x00 };
    bitstream bs;
    bitstream_init(&bs, data, sizeof(data));

    int dc = decode_dc_coefficient(&bs, &dc_table, 0);
    if (dc == INT_MIN) {
        fprintf(stderr, "    decode_dc failed\n");
        return -1;
    }
    /* DC diff=0, prev=0, so DC=0 */
    if (dc != 0) {
        fprintf(stderr, "    expected DC=0, got %d\n", dc);
        return -1;
    }

    return 0;
}

/* Test: decode AC with immediate EOB */
TEST(decode_ac_eob)
{
    huff_decode_table ac_table;
    huff_build_table(&std_ac_lum, &ac_table);

    /*
     * Standard AC lum table: EOB (symbol 0x00) has code "1010" (4 bits)
     * 0xA0 = 10100000
     */
    uint8_t data[] = { 0xA0, 0x00 };
    bitstream bs;
    bitstream_init(&bs, data, sizeof(data));

    int block[64];
    memset(block, 0x55, sizeof(block));  /* Fill with junk */
    block[0] = 42;  /* DC already set */

    int ret = decode_ac_coefficients(&bs, &ac_table, block);
    if (ret != 0) {
        fprintf(stderr, "    decode_ac failed: %d\n", ret);
        return -1;
    }

    /* DC should be unchanged */
    if (block[0] != 42) {
        fprintf(stderr, "    DC was modified: %d\n", block[0]);
        return -1;
    }

    /* All AC coefficients should be 0 */
    for (int i = 1; i < 64; i++) {
        if (block[i] != 0) {
            fprintf(stderr, "    block[%d] = %d, expected 0\n", i, block[i]);
            return -1;
        }
    }

    return 0;
}

int main(void)
{
    int passed = 0, failed = 0, total = 0;

    printf("Huffman Decoder Tests:\n");
    RUN_TEST(build_dc_table);
    RUN_TEST(build_ac_table);
    RUN_TEST(bitstream_basic);
    RUN_TEST(bitstream_byte_stuffing);
    RUN_TEST(extend_values);
    RUN_TEST(decode_dc);
    RUN_TEST(decode_ac_eob);

    printf("\nResults: %d/%d passed", passed, total);
    if (failed > 0)
        printf(", %d FAILED", failed);
    printf("\n");

    return failed > 0 ? 1 : 0;
}
