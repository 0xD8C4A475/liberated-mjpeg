/*
 * jpeg_decoder.c - Full JPEG baseline decoder implementation
 *
 * Decodes baseline JPEG (SOF0) images with:
 *   - 4:4:4, 4:2:2, and 4:2:0 chroma subsampling
 *   - Grayscale (1 component)
 *   - Restart marker support
 *
 * Uses optimized paths: fast Huffman lookup, AAN IDCT, integer color convert.
 */

#include "jpeg_decoder.h"
#include "jpeg_parser.h"
#include "huffman.h"
#include "huffman_fast.h"
#include "idct.h"
#include "idct_fast.h"
#include "claude_mjpeg.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Map component IDs from scan to component indices in SOF */
static int find_component_index(const jpeg_scan_info *info, uint8_t comp_id)
{
    for (int i = 0; i < info->num_components; i++) {
        if (info->components[i].id == comp_id)
            return i;
    }
    return -1;
}

int jpeg_decode(const uint8_t *data, size_t len, cmj_frame *out)
{
    if (!data || !out || len < 2)
        return CMJ_ERROR_INVALID_ARG;

    /* Step 1: Parse all markers */
    jpeg_scan_info scan;
    int ret = jpeg_parse(data, len, &scan);
    if (ret != CMJ_OK)
        return ret;

    int width = scan.width;
    int height = scan.height;
    int ncomp = scan.num_components;
    int max_h = scan.max_h_sampling;
    int max_v = scan.max_v_sampling;

    /* Sanity checks */
    if (width <= 0 || height <= 0 || width > 65535 || height > 65535)
        return CMJ_ERROR_INVALID_DATA;
    if (ncomp != 1 && ncomp != 3)
        return CMJ_ERROR_UNSUPPORTED;

    /* Step 2: Build fast Huffman decode tables */
    huff_fast_table dc_tables[JPEG_MAX_HUFF_TABLES];
    huff_fast_table ac_tables[JPEG_MAX_HUFF_TABLES];
    memset(dc_tables, 0, sizeof(dc_tables));
    memset(ac_tables, 0, sizeof(ac_tables));

    for (int i = 0; i < JPEG_MAX_HUFF_TABLES; i++) {
        if (scan.dc_huff_tables[i].valid) {
            ret = huff_fast_build(&scan.dc_huff_tables[i], &dc_tables[i]);
            if (ret != CMJ_OK) return ret;
        }
        if (scan.ac_huff_tables[i].valid) {
            ret = huff_fast_build(&scan.ac_huff_tables[i], &ac_tables[i]);
            if (ret != CMJ_OK) return ret;
        }
    }

    /* Step 3: Calculate MCU dimensions */
    int mcu_width = max_h * 8;
    int mcu_height = max_v * 8;
    int mcus_x = (width + mcu_width - 1) / mcu_width;
    int mcus_y = (height + mcu_height - 1) / mcu_height;

    /* Step 4: Allocate component buffers */
    int comp_width[JPEG_MAX_COMPONENTS];
    int comp_height[JPEG_MAX_COMPONENTS];
    uint8_t *comp_data[JPEG_MAX_COMPONENTS] = {0};

    for (int c = 0; c < ncomp; c++) {
        int h_factor = scan.components[c].h_sampling;
        int v_factor = scan.components[c].v_sampling;
        comp_width[c] = mcus_x * h_factor * 8;
        comp_height[c] = mcus_y * v_factor * 8;
        comp_data[c] = (uint8_t *)calloc(comp_width[c] * comp_height[c], 1);
        if (!comp_data[c]) {
            for (int j = 0; j < c; j++) free(comp_data[j]);
            return CMJ_ERROR_OUT_OF_MEMORY;
        }
    }

    /* Step 5: Decode entropy-coded data MCU by MCU */
    bitstream bs;
    bitstream_init(&bs, scan.ecs_data, scan.ecs_length);

    int prev_dc[JPEG_MAX_COMPONENTS] = {0};
    int mcu_count = 0;

    /* Map scan component order to SOF component order */
    int scan_to_comp[JPEG_MAX_COMPONENTS];
    for (int s = 0; s < scan.scan_num_components; s++) {
        scan_to_comp[s] = find_component_index(&scan, scan.scan_components[s].component_id);
        if (scan_to_comp[s] < 0) {
            for (int c = 0; c < ncomp; c++) free(comp_data[c]);
            return CMJ_ERROR_INVALID_DATA;
        }
    }

    for (int mcu_y = 0; mcu_y < mcus_y; mcu_y++) {
        for (int mcu_x = 0; mcu_x < mcus_x; mcu_x++) {

            /* Check restart interval */
            if (scan.restart_interval > 0 && mcu_count > 0 &&
                (mcu_count % scan.restart_interval) == 0) {
                memset(prev_dc, 0, sizeof(prev_dc));
                bs.bits_left = 0;
                bs.bit_buffer = 0;
            }

            /* For each component in the scan */
            for (int s = 0; s < scan.scan_num_components; s++) {
                int c = scan_to_comp[s];
                int h_factor = scan.components[c].h_sampling;
                int v_factor = scan.components[c].v_sampling;
                int qt_id = scan.components[c].quant_table_id;
                int dc_id = scan.scan_components[s].dc_table_id;
                int ac_id = scan.scan_components[s].ac_table_id;

                for (int bv = 0; bv < v_factor; bv++) {
                    for (int bh = 0; bh < h_factor; bh++) {
                        int block[64];
                        memset(block, 0, sizeof(block));

                        /* Decode DC (fast path) */
                        int dc = decode_dc_fast(&bs, &dc_tables[dc_id], prev_dc[c]);
                        if (dc == INT_MIN) {
                            for (int cc = 0; cc < ncomp; cc++) free(comp_data[cc]);
                            return CMJ_ERROR_INVALID_DATA;
                        }
                        prev_dc[c] = dc;
                        block[0] = dc;

                        /* Decode AC (fast path) */
                        ret = decode_ac_fast(&bs, &ac_tables[ac_id], block);
                        if (ret != 0) {
                            for (int cc = 0; cc < ncomp; cc++) free(comp_data[cc]);
                            return CMJ_ERROR_INVALID_DATA;
                        }

                        /* Fast IDCT + dequantize */
                        uint8_t pixels[64];
                        idct_fast_dequant_block(block, scan.quant_tables[qt_id].table, pixels);

                        /* Copy 8x8 block to component buffer */
                        int block_x = mcu_x * h_factor * 8 + bh * 8;
                        int block_y = mcu_y * v_factor * 8 + bv * 8;

                        for (int py = 0; py < 8; py++) {
                            int dst_y = block_y + py;
                            if (dst_y >= comp_height[c]) break;
                            uint8_t *dst = &comp_data[c][dst_y * comp_width[c] + block_x];
                            int copy_w = 8;
                            if (block_x + 8 > comp_width[c])
                                copy_w = comp_width[c] - block_x;
                            memcpy(dst, &pixels[py * 8], copy_w);
                        }
                    }
                }
            }

            mcu_count++;
        }
    }

    /* Step 6: Convert to RGB24 output */
    int stride = width * 3;
    uint8_t *out_pixels = (uint8_t *)malloc(stride * height);
    if (!out_pixels) {
        for (int c = 0; c < ncomp; c++) free(comp_data[c]);
        return CMJ_ERROR_OUT_OF_MEMORY;
    }

    if (ncomp == 1) {
        /* Grayscale */
        for (int y = 0; y < height; y++) {
            uint8_t *dst = out_pixels + y * stride;
            const uint8_t *src = comp_data[0] + y * comp_width[0];
            for (int x = 0; x < width; x++) {
                uint8_t val = src[x];
                dst[x * 3 + 0] = val;
                dst[x * 3 + 1] = val;
                dst[x * 3 + 2] = val;
            }
        }
    } else {
        /* YCbCr -> RGB (fast integer path) */
        for (int y = 0; y < height; y++) {
            uint8_t *dst = out_pixels + y * stride;
            const uint8_t *y_row = comp_data[0] + y * comp_width[0];

            int cb_y = y * scan.components[1].v_sampling / max_v;
            int cr_y = y * scan.components[2].v_sampling / max_v;
            const uint8_t *cb_row = comp_data[1] + cb_y * comp_width[1];
            const uint8_t *cr_row = comp_data[2] + cr_y * comp_width[2];

            int cb_h = scan.components[1].h_sampling;
            int cr_h = scan.components[2].h_sampling;

            for (int x = 0; x < width; x++) {
                int Y = y_row[x];
                int Cb = cb_row[x * cb_h / max_h];
                int Cr = cr_row[x * cr_h / max_h];

                ycbcr_to_rgb_fast(Y, Cb, Cr, &dst[x*3], &dst[x*3+1], &dst[x*3+2]);
            }
        }
    }

    /* Cleanup */
    for (int c = 0; c < ncomp; c++)
        free(comp_data[c]);

    out->width = width;
    out->height = height;
    out->stride = stride;
    out->pixel_format = CMJ_PIXFMT_RGB24;
    out->pixels = out_pixels;

    return CMJ_OK;
}
