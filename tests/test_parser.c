/*
 * test_parser.c - Unit tests for JPEG marker parsing
 */

#include "../src/jpeg_parser.h"
#include "claude_mjpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TEST(name) static int test_##name(void)
#define RUN_TEST(name) do { \
    printf("  %-40s ", #name); \
    if (test_##name() == 0) { printf("PASS\n"); passed++; } \
    else { printf("FAIL\n"); failed++; } \
    total++; \
} while(0)

/*
 * Minimal valid JPEG (1x1 white pixel, baseline, 4:4:4)
 * Hand-crafted to be the smallest valid JPEG for testing.
 */
static const uint8_t minimal_jpeg[] = {
    /* SOI */
    0xFF, 0xD8,

    /* DQT - Quantization table 0 (all 1s for simplicity) */
    0xFF, 0xDB,
    0x00, 0x43,  /* Length = 67 */
    0x00,        /* Pq=0 (8-bit), Tq=0 */
    /* 64 quantization values (all 1) */
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,

    /* SOF0 - Baseline DCT, 1x1, 1 component (grayscale) */
    0xFF, 0xC0,
    0x00, 0x0B,  /* Length = 11 */
    0x08,        /* Precision = 8 bits */
    0x00, 0x01,  /* Height = 1 */
    0x00, 0x01,  /* Width = 1 */
    0x01,        /* Number of components = 1 */
    0x01,        /* Component ID = 1 */
    0x11,        /* H=1, V=1 */
    0x00,        /* Quant table 0 */

    /* DHT - DC Huffman table 0 */
    0xFF, 0xC4,
    0x00, 0x1F,  /* Length = 31 */
    0x00,        /* Class=0 (DC), ID=0 */
    /* BITS: 0 codes of length 1, 1 code of length 2, ... */
    0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* HUFFVAL: */
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B,

    /* DHT - AC Huffman table 0 */
    0xFF, 0xC4,
    0x00, 0xB5,  /* Length = 181 */
    0x10,        /* Class=1 (AC), ID=0 */
    /* BITS */
    0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03,
    0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D,
    /* HUFFVAL (162 values) */
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
    0xF9, 0xFA,

    /* SOS */
    0xFF, 0xDA,
    0x00, 0x08,  /* Length = 8 */
    0x01,        /* Number of components = 1 */
    0x01,        /* Component ID = 1 */
    0x00,        /* DC table 0, AC table 0 */
    0x00, 0x3F,  /* Ss=0, Se=63 */
    0x00,        /* Ah=0, Al=0 */

    /* Entropy coded data: a single DC=128 (white pixel), AC all zero (EOB) */
    /* DC: category 0 (value 0 relative to 128 level shift, meaning pixel=128) */
    /* Actually for a white (255) pixel with level shift: 255-128=127, cat 7 */
    /* Simple: just encode a zero DC diff + EOB for AC */
    0x00,        /* Simplified ECS data */

    /* EOI */
    0xFF, 0xD9
};

/* Test: parse a valid minimal JPEG */
TEST(parse_minimal_jpeg)
{
    jpeg_scan_info info;
    int ret = jpeg_parse(minimal_jpeg, sizeof(minimal_jpeg), &info);
    if (ret != CMJ_OK) {
        fprintf(stderr, "    parse returned %d\n", ret);
        return -1;
    }

    /* Check dimensions */
    if (info.width != 1 || info.height != 1) {
        fprintf(stderr, "    wrong dimensions: %dx%d\n", info.width, info.height);
        return -1;
    }

    /* Check component count */
    if (info.num_components != 1) {
        fprintf(stderr, "    wrong component count: %d\n", info.num_components);
        return -1;
    }

    /* Check sampling factors */
    if (info.components[0].h_sampling != 1 || info.components[0].v_sampling != 1) {
        fprintf(stderr, "    wrong sampling: %dx%d\n",
                info.components[0].h_sampling, info.components[0].v_sampling);
        return -1;
    }

    /* Check quant table */
    if (!info.quant_tables[0].valid) {
        fprintf(stderr, "    quant table 0 not found\n");
        return -1;
    }

    /* Check Huffman tables */
    if (!info.dc_huff_tables[0].valid) {
        fprintf(stderr, "    DC Huffman table 0 not found\n");
        return -1;
    }
    if (!info.ac_huff_tables[0].valid) {
        fprintf(stderr, "    AC Huffman table 0 not found\n");
        return -1;
    }

    return 0;
}

/* Test: reject invalid data (no SOI) */
TEST(reject_no_soi)
{
    uint8_t bad[] = { 0x00, 0x00, 0x00 };
    jpeg_scan_info info;
    int ret = jpeg_parse(bad, sizeof(bad), &info);
    return (ret != CMJ_OK) ? 0 : -1;
}

/* Test: reject null pointer */
TEST(reject_null)
{
    jpeg_scan_info info;
    int ret = jpeg_parse(NULL, 0, &info);
    return (ret == CMJ_ERROR_INVALID_ARG) ? 0 : -1;
}

/* Test: parse a real JPEG file if provided */
TEST(parse_real_jpeg)
{
    const char *path = "test_image.jpg";
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("(skipped: no %s) ", path);
        return 0;  /* Skip if no test image */
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = (uint8_t *)malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    jpeg_scan_info info;
    int ret = jpeg_parse(data, size, &info);
    free(data);

    if (ret != CMJ_OK) {
        fprintf(stderr, "    parse failed: %d\n", ret);
        return -1;
    }

    printf("(%dx%d, %d comp) ", info.width, info.height, info.num_components);
    return 0;
}

/* Test: DRI parsing */
TEST(parse_dri)
{
    /* Build a JPEG with DRI marker */
    uint8_t jpeg_with_dri[sizeof(minimal_jpeg) + 6];
    /* SOI */
    jpeg_with_dri[0] = 0xFF;
    jpeg_with_dri[1] = 0xD8;
    /* DRI */
    jpeg_with_dri[2] = 0xFF;
    jpeg_with_dri[3] = 0xDD;
    jpeg_with_dri[4] = 0x00;
    jpeg_with_dri[5] = 0x04;  /* Length = 4 */
    jpeg_with_dri[6] = 0x00;
    jpeg_with_dri[7] = 0x0A;  /* Restart interval = 10 */
    /* Copy rest of minimal_jpeg after SOI */
    memcpy(jpeg_with_dri + 8, minimal_jpeg + 2, sizeof(minimal_jpeg) - 2);

    jpeg_scan_info info;
    int ret = jpeg_parse(jpeg_with_dri, sizeof(jpeg_with_dri), &info);
    if (ret != CMJ_OK) {
        fprintf(stderr, "    parse returned %d\n", ret);
        return -1;
    }

    if (info.restart_interval != 10) {
        fprintf(stderr, "    wrong restart interval: %d\n", info.restart_interval);
        return -1;
    }

    return 0;
}

int main(void)
{
    int passed = 0, failed = 0, total = 0;

    printf("JPEG Parser Tests:\n");
    RUN_TEST(parse_minimal_jpeg);
    RUN_TEST(reject_no_soi);
    RUN_TEST(reject_null);
    RUN_TEST(parse_dri);
    RUN_TEST(parse_real_jpeg);

    printf("\nResults: %d/%d passed", passed, total);
    if (failed > 0)
        printf(", %d FAILED", failed);
    printf("\n");

    return failed > 0 ? 1 : 0;
}
