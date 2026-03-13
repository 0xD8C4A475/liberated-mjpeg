/*
 * gen_test_jpeg.c - Generate a minimal test JPEG file for decoder testing
 *
 * Creates an 8x8 grayscale JPEG with a known pixel pattern.
 * Uses the standard JPEG luminance Huffman tables.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Write big-endian 16-bit value */
static void write_u16(FILE *f, uint16_t val)
{
    fputc((val >> 8) & 0xFF, f);
    fputc(val & 0xFF, f);
}

/* Bit output buffer */
typedef struct {
    FILE *f;
    uint8_t buffer;
    int bits_used;
} bit_writer;

static void bw_init(bit_writer *bw, FILE *f)
{
    bw->f = f;
    bw->buffer = 0;
    bw->bits_used = 0;
}

static void bw_write_bits(bit_writer *bw, int value, int nbits)
{
    for (int i = nbits - 1; i >= 0; i--) {
        bw->buffer = (bw->buffer << 1) | ((value >> i) & 1);
        bw->bits_used++;
        if (bw->bits_used == 8) {
            fputc(bw->buffer, bw->f);
            if (bw->buffer == 0xFF)
                fputc(0x00, bw->f);  /* Byte stuffing */
            bw->buffer = 0;
            bw->bits_used = 0;
        }
    }
}

static void bw_flush(bit_writer *bw)
{
    if (bw->bits_used > 0) {
        bw->buffer <<= (8 - bw->bits_used);
        fputc(bw->buffer, bw->f);
        if (bw->buffer == 0xFF)
            fputc(0x00, bw->f);
        bw->buffer = 0;
        bw->bits_used = 0;
    }
}

/* Standard luminance quantization table (T.81 Table K.1) */
static const uint8_t std_lum_quant[64] = {
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68,109,103, 77,
    24, 35, 55, 64, 81,104,113, 92,
    49, 64, 78, 87,103,121,120,101,
    72, 92, 95, 98,112,100,103, 99
};

/* Zigzag order */
static const int zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

/* Standard DC luminance Huffman table codes */
/* Category -> (code, code_length) */
static const struct { int code; int len; } dc_codes[12] = {
    {0x00, 2}, /* cat 0 */
    {0x02, 3}, /* cat 1 */
    {0x03, 3}, /* cat 2 */
    {0x04, 3}, /* cat 3 */
    {0x05, 3}, /* cat 4 */
    {0x06, 3}, /* cat 5 */
    {0x0E, 4}, /* cat 6 */
    {0x1E, 5}, /* cat 7 */
    {0x3E, 6}, /* cat 8 */
    {0x7E, 7}, /* cat 9 */
    {0xFE, 8}, /* cat 10 */
    {0x1FE,9}, /* cat 11 */
};

/* Standard AC luminance Huffman table
 * Only encoding EOB (0x00) and a few common symbols */
/* EOB = symbol 0x00, code = 1010 (4 bits) in standard table */
/* For simplicity, we'll create a flat gray image where all AC = 0 */
static const int ac_eob_code = 0x0A;  /* 1010 */
static const int ac_eob_len = 4;

static int get_category(int value)
{
    if (value < 0) value = -value;
    int cat = 0;
    while (value > 0) {
        cat++;
        value >>= 1;
    }
    return cat;
}

static int encode_value(int value, int category)
{
    if (value >= 0)
        return value;
    return value + (1 << category) - 1;
}

int main(int argc, char **argv)
{
    const char *output = "test_gray8x8.jpg";
    if (argc > 1) output = argv[1];

    FILE *f = fopen(output, "wb");
    if (!f) {
        fprintf(stderr, "Cannot open %s\n", output);
        return 1;
    }

    /* Create an 8x8 pixel pattern (gradient) */
    uint8_t pixels[64];
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            /* Simple gradient: top-left dark, bottom-right bright */
            pixels[y * 8 + x] = (uint8_t)((x + y) * 255 / 14);
        }
    }

    /* Forward DCT */
    double dct[64];
    for (int v = 0; v < 8; v++) {
        for (int u = 0; u < 8; u++) {
            double sum = 0;
            double cu = (u == 0) ? 1.0 / sqrt(2.0) : 1.0;
            double cv = (v == 0) ? 1.0 / sqrt(2.0) : 1.0;
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    double val = (double)pixels[y * 8 + x] - 128.0;
                    sum += val * cos((2*x+1) * u * M_PI / 16.0)
                              * cos((2*y+1) * v * M_PI / 16.0);
                }
            }
            dct[v * 8 + u] = sum * cu * cv / 4.0;
        }
    }

    /* Quantize */
    int quantized[64];
    for (int i = 0; i < 64; i++) {
        int zi = zigzag[i];
        int row = zi / 8, col = zi % 8;
        double q = std_lum_quant[row * 8 + col]; /* quant table is in natural order but we store in zigzag */
        quantized[i] = (int)(dct[row * 8 + col] / q + (dct[row * 8 + col] >= 0 ? 0.5 : -0.5));
    }

    /* Wait - the quant table in the JPEG file is stored in zigzag order.
     * Let me redo this properly.
     */
    /* Actually, the standard says DQT stores values in zigzag order.
     * And the coefficients after Huffman decoding are also in zigzag order.
     * Let me be more careful.
     *
     * DCT output is in natural (row-major) order.
     * We quantize in natural order: Q[i] = round(DCT[i] / QTable_natural[i])
     * Then we reorder to zigzag for Huffman encoding.
     */

    /* Re-quantize properly */
    int quant_natural[64];  /* quantized in natural order */
    for (int i = 0; i < 64; i++) {
        double q = std_lum_quant[i];
        quant_natural[i] = (int)(dct[i] / q + (dct[i] >= 0 ? 0.5 : -0.5));
    }

    /* Reorder to zigzag for encoding */
    int quant_zigzag[64];
    for (int i = 0; i < 64; i++) {
        quant_zigzag[i] = quant_natural[zigzag[i]];
    }

    /* === Write JPEG file === */

    /* SOI */
    fputc(0xFF, f); fputc(0xD8, f);

    /* DQT - quantization table in zigzag order */
    fputc(0xFF, f); fputc(0xDB, f);
    write_u16(f, 67); /* length */
    fputc(0x00, f);   /* Pq=0 (8-bit), Tq=0 */
    /* Write quant table in zigzag order */
    for (int i = 0; i < 64; i++) {
        /* The quant values from std_lum_quant are in natural order.
         * DQT expects them in zigzag order. */
        fputc(std_lum_quant[zigzag[i]], f);
    }

    /* SOF0 */
    fputc(0xFF, f); fputc(0xC0, f);
    write_u16(f, 11);  /* length */
    fputc(8, f);       /* precision */
    write_u16(f, 8);   /* height */
    write_u16(f, 8);   /* width */
    fputc(1, f);       /* 1 component (grayscale) */
    fputc(1, f);       /* component ID */
    fputc(0x11, f);    /* H=1, V=1 */
    fputc(0, f);       /* quant table 0 */

    /* DHT - DC table 0 */
    fputc(0xFF, f); fputc(0xC4, f);
    write_u16(f, 31);  /* length */
    fputc(0x00, f);    /* class=0 (DC), id=0 */
    /* BITS */
    uint8_t dc_bits[] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
    fwrite(dc_bits, 1, 16, f);
    /* HUFFVAL */
    uint8_t dc_vals[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    fwrite(dc_vals, 1, 12, f);

    /* DHT - AC table 0 */
    fputc(0xFF, f); fputc(0xC4, f);
    write_u16(f, 181);  /* length */
    fputc(0x10, f);     /* class=1 (AC), id=0 */
    /* Standard AC luminance BITS */
    uint8_t ac_bits[] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D};
    fwrite(ac_bits, 1, 16, f);
    /* Standard AC luminance HUFFVAL (162 values) */
    uint8_t ac_vals[] = {
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
    };
    fwrite(ac_vals, 1, 162, f);

    /* SOS */
    fputc(0xFF, f); fputc(0xDA, f);
    write_u16(f, 8);  /* length */
    fputc(1, f);      /* 1 component */
    fputc(1, f);      /* component ID */
    fputc(0x00, f);   /* DC table 0, AC table 0 */
    fputc(0, f);      /* Ss */
    fputc(63, f);     /* Se */
    fputc(0, f);      /* Ah=0, Al=0 */

    /* Entropy-coded data */
    bit_writer bw;
    bw_init(&bw, f);

    /* Encode DC coefficient */
    int dc_val = quant_zigzag[0];
    int dc_cat = get_category(dc_val);
    bw_write_bits(&bw, dc_codes[dc_cat].code, dc_codes[dc_cat].len);
    if (dc_cat > 0) {
        int encoded = encode_value(dc_val, dc_cat);
        bw_write_bits(&bw, encoded, dc_cat);
    }

    /* Encode AC coefficients */
    int last_nonzero = 0;
    for (int i = 63; i >= 1; i--) {
        if (quant_zigzag[i] != 0) {
            last_nonzero = i;
            break;
        }
    }

    /* For simplicity with the standard Huffman table, we need to look up the
     * actual codes. Let me just encode all-zero ACs with EOB. */
    /* Actually, let's properly encode the AC values using the standard table.
     *
     * But building the full AC encoding table from the standard spec is tedious.
     * For a simpler approach, let's create a flat gray image where DC encodes
     * the mean and all ACs are zero (EOB immediately).
     */

    /* Re-create with flat gray pixels */
    /* Actually, we already have the DCT coefficients computed. Most ACs are
     * non-zero for the gradient. Let's just encode a flat gray instead for
     * the test image - it's simpler and we can verify the decode. */

    /* Let me restart with a flat 128-gray image (level-shifted to 0) */
    fclose(f);

    /* === Redo with flat gray === */
    f = fopen(output, "wb");
    if (!f) return 1;

    /* SOI */
    fputc(0xFF, f); fputc(0xD8, f);

    /* DQT */
    fputc(0xFF, f); fputc(0xDB, f);
    write_u16(f, 67);
    fputc(0x00, f);
    for (int i = 0; i < 64; i++)
        fputc(std_lum_quant[zigzag[i]], f);

    /* SOF0 */
    fputc(0xFF, f); fputc(0xC0, f);
    write_u16(f, 11);
    fputc(8, f);
    write_u16(f, 8);
    write_u16(f, 8);
    fputc(1, f);
    fputc(1, f);
    fputc(0x11, f);
    fputc(0, f);

    /* DHT DC */
    fputc(0xFF, f); fputc(0xC4, f);
    write_u16(f, 31);
    fputc(0x00, f);
    fwrite(dc_bits, 1, 16, f);
    fwrite(dc_vals, 1, 12, f);

    /* DHT AC */
    fputc(0xFF, f); fputc(0xC4, f);
    write_u16(f, 181);
    fputc(0x10, f);
    fwrite(ac_bits, 1, 16, f);
    fwrite(ac_vals, 1, 162, f);

    /* SOS */
    fputc(0xFF, f); fputc(0xDA, f);
    write_u16(f, 8);
    fputc(1, f);
    fputc(1, f);
    fputc(0x00, f);
    fputc(0, f);
    fputc(63, f);
    fputc(0, f);

    /* ECS: flat gray = all DCT coefficients are 0 (after level shift 128-128=0)
     * DC category 0: code = 00 (2 bits)
     * AC: EOB immediately: code = 1010 (4 bits)
     * Total: 00 1010 = 001010xx -> 0x28 (pad with 0s)
     */
    bw_init(&bw, f);
    /* DC: category 0 (value = 0), code = 00 */
    bw_write_bits(&bw, 0x00, 2);
    /* AC: EOB, code = 1010 */
    bw_write_bits(&bw, 0x0A, 4);
    bw_flush(&bw);

    /* EOI */
    fputc(0xFF, f); fputc(0xD9, f);

    fclose(f);

    printf("Generated %s (8x8 flat gray JPEG)\n", output);
    printf("Expected decode: all pixels = 128 (gray)\n");

    return 0;
}
