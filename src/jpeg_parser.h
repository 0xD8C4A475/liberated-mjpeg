/*
 * jpeg_parser.h - JPEG marker parsing and metadata extraction
 *
 * Parses JPEG markers per ITU-T T.81 sections B.2.1 through B.2.4
 */

#ifndef JPEG_PARSER_H
#define JPEG_PARSER_H

#include <stdint.h>
#include <stddef.h>

/* JPEG markers */
#define JPEG_MARKER_SOI  0xFFD8  /* Start of Image */
#define JPEG_MARKER_EOI  0xFFD9  /* End of Image */
#define JPEG_MARKER_SOF0 0xFFC0  /* Baseline DCT */
#define JPEG_MARKER_SOF2 0xFFC2  /* Progressive DCT (unsupported) */
#define JPEG_MARKER_DHT  0xFFC4  /* Define Huffman Table */
#define JPEG_MARKER_DQT  0xFFDB  /* Define Quantization Table */
#define JPEG_MARKER_DRI  0xFFDD  /* Define Restart Interval */
#define JPEG_MARKER_SOS  0xFFDA  /* Start of Scan */
#define JPEG_MARKER_APP0 0xFFE0  /* Application segment 0 (JFIF) */
#define JPEG_MARKER_APP1 0xFFE1  /* Application segment 1 (EXIF) */
#define JPEG_MARKER_COM  0xFFFE  /* Comment */

/* RST markers: 0xFFD0 - 0xFFD7 */
#define JPEG_MARKER_RST0 0xFFD0
#define JPEG_MARKER_RST7 0xFFD7

#define JPEG_MAX_COMPONENTS  4
#define JPEG_MAX_QUANT_TABLES 4
#define JPEG_MAX_HUFF_TABLES  4  /* 2 DC + 2 AC typically */

/* Component info from SOF0 */
typedef struct {
    uint8_t id;               /* Component identifier */
    uint8_t h_sampling;       /* Horizontal sampling factor (1-4) */
    uint8_t v_sampling;       /* Vertical sampling factor (1-4) */
    uint8_t quant_table_id;   /* Quantization table selector */
} jpeg_component;

/* Quantization table */
typedef struct {
    uint8_t precision;        /* 0 = 8-bit, 1 = 16-bit */
    uint16_t table[64];       /* Quantization values in zigzag order */
    int valid;
} jpeg_quant_table;

/* Huffman table */
typedef struct {
    uint8_t bits[17];         /* Number of codes of each length (1-16), bits[0] unused */
    uint8_t huffval[256];     /* Symbol values */
    int total_codes;          /* Total number of defined codes */
    int valid;
} jpeg_huff_table;

/* Scan component selector */
typedef struct {
    uint8_t component_id;
    uint8_t dc_table_id;      /* DC Huffman table selector */
    uint8_t ac_table_id;      /* AC Huffman table selector */
} jpeg_scan_component;

/* Complete JPEG scan info */
typedef struct {
    /* From SOF0 */
    int width;
    int height;
    int num_components;
    int precision;            /* Sample precision (typically 8) */
    jpeg_component components[JPEG_MAX_COMPONENTS];
    int max_h_sampling;       /* Maximum horizontal sampling factor */
    int max_v_sampling;       /* Maximum vertical sampling factor */

    /* Quantization tables from DQT */
    jpeg_quant_table quant_tables[JPEG_MAX_QUANT_TABLES];

    /* Huffman tables from DHT */
    jpeg_huff_table dc_huff_tables[JPEG_MAX_HUFF_TABLES];
    jpeg_huff_table ac_huff_tables[JPEG_MAX_HUFF_TABLES];

    /* From DRI */
    int restart_interval;     /* MCU count between restart markers, 0 = disabled */

    /* From SOS */
    int scan_num_components;
    jpeg_scan_component scan_components[JPEG_MAX_COMPONENTS];

    /* Entropy coded data location */
    const uint8_t *ecs_data;  /* Pointer to start of entropy-coded segment */
    size_t ecs_length;        /* Length of entropy-coded data */

    /* Parsing status */
    int sof_found;
    int sos_found;
} jpeg_scan_info;

/*
 * Parse all JPEG markers from a complete JPEG image in memory.
 *
 * data: pointer to JPEG data (must start with SOI: 0xFF 0xD8)
 * len:  total length of JPEG data
 * info: output struct to fill with parsed metadata
 *
 * Returns 0 on success, negative error code on failure.
 */
int jpeg_parse(const uint8_t *data, size_t len, jpeg_scan_info *info);

/* Zigzag order table: zigzag_order[i] gives the 2D position for zigzag index i */
extern const uint8_t jpeg_zigzag_order[64];

/* Natural order to zigzag mapping */
extern const uint8_t jpeg_natural_order[64];

#endif /* JPEG_PARSER_H */
