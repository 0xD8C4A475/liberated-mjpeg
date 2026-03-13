/*
 * jpeg_parser.c - JPEG marker parsing implementation
 *
 * Parses JPEG file structure per ITU-T T.81
 */

#include "jpeg_parser.h"
#include "claude_mjpeg.h"
#include <string.h>
#include <stdio.h>

/* Zigzag scan order (Table A.6 in T.81) */
const uint8_t jpeg_zigzag_order[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

/* Natural order (inverse of zigzag) */
const uint8_t jpeg_natural_order[64] = {
     0,  1,  5,  6, 14, 15, 27, 28,
     2,  4,  7, 13, 16, 26, 29, 42,
     3,  8, 12, 17, 25, 30, 41, 43,
     9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63
};

/* Read a big-endian 16-bit value */
static inline uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

/* Parse SOF0 (Start of Frame - Baseline DCT) */
static int parse_sof0(const uint8_t *data, size_t len, jpeg_scan_info *info)
{
    if (len < 8)
        return CMJ_ERROR_INVALID_DATA;

    uint16_t segment_len = read_u16(data);
    (void)segment_len;

    info->precision = data[2];
    info->height = read_u16(data + 3);
    info->width = read_u16(data + 5);
    info->num_components = data[7];

    if (info->precision != 8)
        return CMJ_ERROR_UNSUPPORTED;

    if (info->num_components < 1 || info->num_components > JPEG_MAX_COMPONENTS)
        return CMJ_ERROR_INVALID_DATA;

    if (len < (size_t)(8 + info->num_components * 3))
        return CMJ_ERROR_INVALID_DATA;

    info->max_h_sampling = 1;
    info->max_v_sampling = 1;

    for (int i = 0; i < info->num_components; i++) {
        const uint8_t *cp = data + 8 + i * 3;
        info->components[i].id = cp[0];
        info->components[i].h_sampling = (cp[1] >> 4) & 0x0F;
        info->components[i].v_sampling = cp[1] & 0x0F;
        info->components[i].quant_table_id = cp[2];

        if (info->components[i].h_sampling == 0 || info->components[i].v_sampling == 0)
            return CMJ_ERROR_INVALID_DATA;

        if (info->components[i].h_sampling > info->max_h_sampling)
            info->max_h_sampling = info->components[i].h_sampling;
        if (info->components[i].v_sampling > info->max_v_sampling)
            info->max_v_sampling = info->components[i].v_sampling;
    }

    info->sof_found = 1;
    return CMJ_OK;
}

/* Parse DQT (Define Quantization Table) */
static int parse_dqt(const uint8_t *data, size_t len, jpeg_scan_info *info)
{
    if (len < 2)
        return CMJ_ERROR_INVALID_DATA;

    uint16_t segment_len = read_u16(data);
    if (segment_len > len)
        return CMJ_ERROR_INVALID_DATA;

    size_t offset = 2;

    while (offset < segment_len) {
        if (offset >= segment_len)
            break;

        uint8_t pq_tq = data[offset++];
        int precision = (pq_tq >> 4) & 0x0F;   /* 0 = 8-bit, 1 = 16-bit */
        int table_id = pq_tq & 0x0F;

        if (table_id >= JPEG_MAX_QUANT_TABLES)
            return CMJ_ERROR_INVALID_DATA;

        jpeg_quant_table *qt = &info->quant_tables[table_id];
        qt->precision = (uint8_t)precision;

        if (precision == 0) {
            /* 8-bit precision */
            if (offset + 64 > segment_len)
                return CMJ_ERROR_INVALID_DATA;
            for (int i = 0; i < 64; i++)
                qt->table[i] = data[offset++];
        } else {
            /* 16-bit precision */
            if (offset + 128 > segment_len)
                return CMJ_ERROR_INVALID_DATA;
            for (int i = 0; i < 64; i++) {
                qt->table[i] = read_u16(data + offset);
                offset += 2;
            }
        }

        qt->valid = 1;
    }

    return CMJ_OK;
}

/* Parse DHT (Define Huffman Table) */
static int parse_dht(const uint8_t *data, size_t len, jpeg_scan_info *info)
{
    if (len < 2)
        return CMJ_ERROR_INVALID_DATA;

    uint16_t segment_len = read_u16(data);
    if (segment_len > len)
        return CMJ_ERROR_INVALID_DATA;

    size_t offset = 2;

    while (offset < segment_len) {
        if (offset >= segment_len)
            break;

        uint8_t tc_th = data[offset++];
        int table_class = (tc_th >> 4) & 0x0F;  /* 0 = DC, 1 = AC */
        int table_id = tc_th & 0x0F;

        if (table_id >= JPEG_MAX_HUFF_TABLES)
            return CMJ_ERROR_INVALID_DATA;

        jpeg_huff_table *ht;
        if (table_class == 0)
            ht = &info->dc_huff_tables[table_id];
        else
            ht = &info->ac_huff_tables[table_id];

        /* Read BITS (number of codes of each length) */
        if (offset + 16 > segment_len)
            return CMJ_ERROR_INVALID_DATA;

        ht->bits[0] = 0;
        int total = 0;
        for (int i = 1; i <= 16; i++) {
            ht->bits[i] = data[offset++];
            total += ht->bits[i];
        }

        if (total > 256 || offset + total > segment_len)
            return CMJ_ERROR_INVALID_DATA;

        /* Read HUFFVAL (symbol values) */
        ht->total_codes = total;
        for (int i = 0; i < total; i++)
            ht->huffval[i] = data[offset++];

        ht->valid = 1;
    }

    return CMJ_OK;
}

/* Parse DRI (Define Restart Interval) */
static int parse_dri(const uint8_t *data, size_t len, jpeg_scan_info *info)
{
    if (len < 4)
        return CMJ_ERROR_INVALID_DATA;

    /* uint16_t segment_len = read_u16(data); */
    info->restart_interval = read_u16(data + 2);
    return CMJ_OK;
}

/* Parse SOS (Start of Scan) */
static int parse_sos(const uint8_t *data, size_t len, jpeg_scan_info *info)
{
    if (len < 3)
        return CMJ_ERROR_INVALID_DATA;

    uint16_t segment_len = read_u16(data);
    info->scan_num_components = data[2];

    if (info->scan_num_components < 1 ||
        info->scan_num_components > JPEG_MAX_COMPONENTS)
        return CMJ_ERROR_INVALID_DATA;

    size_t expected = 3 + info->scan_num_components * 2 + 3;
    if (segment_len < expected || len < expected)
        return CMJ_ERROR_INVALID_DATA;

    size_t offset = 3;
    for (int i = 0; i < info->scan_num_components; i++) {
        info->scan_components[i].component_id = data[offset++];
        uint8_t td_ta = data[offset++];
        info->scan_components[i].dc_table_id = (td_ta >> 4) & 0x0F;
        info->scan_components[i].ac_table_id = td_ta & 0x0F;
    }

    /* Skip Ss, Se, Ah/Al (spectral selection / successive approximation) */
    /* offset += 3; */

    /* Entropy-coded data starts right after SOS */
    info->ecs_data = data + segment_len;
    info->sos_found = 1;

    return CMJ_OK;
}

/* Find the length of entropy-coded data (scan for next marker, handle byte stuffing) */
static size_t find_ecs_length(const uint8_t *data, size_t max_len)
{
    size_t i = 0;

    while (i < max_len - 1) {
        if (data[i] == 0xFF) {
            uint8_t next = data[i + 1];
            if (next == 0x00) {
                /* Byte stuffing - skip */
                i += 2;
            } else if (next >= 0xD0 && next <= 0xD7) {
                /* RST marker - part of entropy data */
                i += 2;
            } else if (next == 0xFF) {
                /* Fill bytes */
                i++;
            } else {
                /* Real marker found - end of ECS */
                return i;
            }
        } else {
            i++;
        }
    }

    return max_len;
}

int jpeg_parse(const uint8_t *data, size_t len, jpeg_scan_info *info)
{
    if (!data || !info || len < 2)
        return CMJ_ERROR_INVALID_ARG;

    memset(info, 0, sizeof(*info));

    /* Check SOI marker */
    if (data[0] != 0xFF || data[1] != 0xD8)
        return CMJ_ERROR_INVALID_DATA;

    size_t pos = 2;
    int ret;

    while (pos < len - 1) {
        /* Find next marker */
        if (data[pos] != 0xFF) {
            pos++;
            continue;
        }

        /* Skip fill bytes (0xFF padding) */
        while (pos < len - 1 && data[pos + 1] == 0xFF)
            pos++;

        if (pos >= len - 1)
            break;

        uint8_t marker = data[pos + 1];
        pos += 2;  /* Skip marker */

        switch (0xFF00 | marker) {
        case JPEG_MARKER_SOI:
            /* Already handled */
            break;

        case JPEG_MARKER_EOI:
            /* End of image */
            goto done;

        case JPEG_MARKER_SOF0:
            ret = parse_sof0(data + pos, len - pos, info);
            if (ret != CMJ_OK) return ret;
            pos += read_u16(data + pos);
            break;

        case JPEG_MARKER_SOF2:
            /* Progressive JPEG - not supported */
            return CMJ_ERROR_UNSUPPORTED;

        case JPEG_MARKER_DQT:
            ret = parse_dqt(data + pos, len - pos, info);
            if (ret != CMJ_OK) return ret;
            pos += read_u16(data + pos);
            break;

        case JPEG_MARKER_DHT:
            ret = parse_dht(data + pos, len - pos, info);
            if (ret != CMJ_OK) return ret;
            pos += read_u16(data + pos);
            break;

        case JPEG_MARKER_DRI:
            ret = parse_dri(data + pos, len - pos, info);
            if (ret != CMJ_OK) return ret;
            pos += read_u16(data + pos);
            break;

        case JPEG_MARKER_SOS:
            ret = parse_sos(data + pos, len - pos, info);
            if (ret != CMJ_OK) return ret;
            pos += read_u16(data + pos);
            /* Now find the end of entropy-coded segment */
            info->ecs_data = data + pos;
            info->ecs_length = find_ecs_length(data + pos, len - pos);
            pos += info->ecs_length;
            break;

        default:
            /* Unknown or APP markers - skip */
            if (marker >= 0xE0 && marker <= 0xEF) {
                /* APPn markers */
                if (pos + 2 <= len)
                    pos += read_u16(data + pos);
            } else if (marker == 0xFE) {
                /* COM (comment) */
                if (pos + 2 <= len)
                    pos += read_u16(data + pos);
            } else if (marker >= 0xC0 && marker <= 0xCF) {
                /* Other SOF markers we don't support */
                return CMJ_ERROR_UNSUPPORTED;
            } else {
                /* Try to skip by reading length */
                if (pos + 2 <= len)
                    pos += read_u16(data + pos);
            }
            break;
        }
    }

done:
    if (!info->sof_found)
        return CMJ_ERROR_INVALID_DATA;
    if (!info->sos_found)
        return CMJ_ERROR_INVALID_DATA;

    return CMJ_OK;
}
