/*
 * avi_demuxer.c - AVI RIFF container parser
 *
 * Parses AVI files with MJPEG video streams.
 * Supports: RIFF, LIST, avih, strh, strf, movi chunks.
 * Builds frame index from "00dc" (compressed video) chunks in movi list.
 */

#include "avi_demuxer.h"
#include "claude_mjpeg.h"
#include <stdlib.h>
#include <string.h>

/* AVI FourCC codes */
#define FOURCC(a,b,c,d) ((uint32_t)(a) | ((uint32_t)(b)<<8) | ((uint32_t)(c)<<16) | ((uint32_t)(d)<<24))
#define FOURCC_RIFF FOURCC('R','I','F','F')
#define FOURCC_AVI  FOURCC('A','V','I',' ')
#define FOURCC_LIST FOURCC('L','I','S','T')
#define FOURCC_hdrl FOURCC('h','d','r','l')
#define FOURCC_avih FOURCC('a','v','i','h')
#define FOURCC_strl FOURCC('s','t','r','l')
#define FOURCC_strh FOURCC('s','t','r','h')
#define FOURCC_strf FOURCC('s','t','r','f')
#define FOURCC_movi FOURCC('m','o','v','i')
#define FOURCC_00dc FOURCC('0','0','d','c')
#define FOURCC_01dc FOURCC('0','1','d','c')
#define FOURCC_idx1 FOURCC('i','d','x','1')
#define FOURCC_JUNK FOURCC('J','U','N','K')
#define FOURCC_vids FOURCC('v','i','d','s')
#define FOURCC_MJPG FOURCC('M','J','P','G')
#define FOURCC_mjpg FOURCC('m','j','p','g')

/* Read 32-bit little-endian */
static uint32_t read_le32(FILE *f)
{
    uint8_t buf[4];
    if (fread(buf, 1, 4, f) != 4) return 0;
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

/* Read 16-bit little-endian */
static uint16_t read_le16(FILE *f)
{
    uint8_t buf[2];
    if (fread(buf, 1, 2, f) != 2) return 0;
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static int fourcc_match(uint32_t a, uint32_t b)
{
    return a == b;
}

int avi_open(const char *path, cmj_context **out_ctx)
{
    if (!path || !out_ctx)
        return CMJ_ERROR_INVALID_ARG;

    FILE *f = fopen(path, "rb");
    if (!f)
        return CMJ_ERROR_IO;

    /* Check RIFF header */
    uint32_t riff = read_le32(f);
    uint32_t file_size = read_le32(f);
    uint32_t avi_type = read_le32(f);
    (void)file_size;

    if (!fourcc_match(riff, FOURCC_RIFF) || !fourcc_match(avi_type, FOURCC_AVI)) {
        fclose(f);
        return CMJ_ERROR_INVALID_DATA;
    }

    cmj_context *ctx = (cmj_context *)calloc(1, sizeof(cmj_context));
    if (!ctx) {
        fclose(f);
        return CMJ_ERROR_OUT_OF_MEMORY;
    }
    ctx->file = f;

    /* Parse chunks */
    long movi_start = 0;
    long movi_end = 0;
    int found_video = 0;

    while (!feof(f)) {
        long chunk_start = ftell(f);
        uint32_t chunk_id = read_le32(f);
        uint32_t chunk_size = read_le32(f);

        if (feof(f) || chunk_id == 0)
            break;

        if (fourcc_match(chunk_id, FOURCC_LIST)) {
            uint32_t list_type = read_le32(f);

            if (fourcc_match(list_type, FOURCC_movi)) {
                movi_start = ftell(f);
                movi_end = movi_start + chunk_size - 4;
                /* Don't skip — we'll scan this later */
                fseek(f, chunk_start + 8 + chunk_size, SEEK_SET);
            }
            /* For hdrl and strl, recurse into them (they contain sub-chunks) */
            /* Just continue — inner chunks will be parsed sequentially */
            continue;
        }

        if (fourcc_match(chunk_id, FOURCC_avih)) {
            /* Main AVI header */
            uint32_t microsec_per_frame = read_le32(f);
            fseek(f, 12, SEEK_CUR);  /* Skip max_bytes_per_sec, padding, flags */
            ctx->total_frames = (int)read_le32(f);
            fseek(f, 4, SEEK_CUR);   /* initial_frames */
            fseek(f, 4, SEEK_CUR);   /* num_streams */
            fseek(f, 4, SEEK_CUR);   /* suggested_buffer */
            ctx->width = (int)read_le32(f);
            ctx->height = (int)read_le32(f);
            if (microsec_per_frame > 0) {
                ctx->fps_num = 1000000;
                ctx->fps_den = microsec_per_frame;
            }
            /* Skip rest of avih */
            fseek(f, chunk_start + 8 + chunk_size, SEEK_SET);
            continue;
        }

        if (fourcc_match(chunk_id, FOURCC_strh)) {
            uint32_t fcc_type = read_le32(f);
            uint32_t fcc_handler = read_le32(f);
            if (fourcc_match(fcc_type, FOURCC_vids)) {
                if (fourcc_match(fcc_handler, FOURCC_MJPG) ||
                    fourcc_match(fcc_handler, FOURCC_mjpg)) {
                    found_video = 1;
                }
                /* Also read rate/scale for FPS */
                fseek(f, 12, SEEK_CUR);  /* flags, priority, initial_frames */
                uint32_t scale = read_le32(f);
                uint32_t rate = read_le32(f);
                if (scale > 0 && rate > 0) {
                    ctx->fps_num = (int)rate;
                    ctx->fps_den = (int)scale;
                }
            }
            fseek(f, chunk_start + 8 + chunk_size, SEEK_SET);
            continue;
        }

        if (fourcc_match(chunk_id, FOURCC_strf)) {
            if (found_video) {
                /* BITMAPINFOHEADER */
                fseek(f, 4, SEEK_CUR);  /* biSize */
                int w = (int)read_le32(f);
                int h = (int)read_le32(f);
                if (w > 0) ctx->width = w;
                if (h > 0) ctx->height = h;
                else if (h < 0) ctx->height = -h;  /* Negative = top-down */
                fseek(f, 2, SEEK_CUR);  /* planes */
                /* uint16_t bpp = */ read_le16(f);
                uint32_t compression = read_le32(f);
                if (!fourcc_match(compression, FOURCC_MJPG) &&
                    !fourcc_match(compression, FOURCC_mjpg)) {
                    /* Not MJPEG compression */
                    found_video = 0;
                }
            }
            fseek(f, chunk_start + 8 + chunk_size, SEEK_SET);
            continue;
        }

        /* Skip unknown chunks */
        /* Align to word boundary */
        long next = chunk_start + 8 + chunk_size;
        if (chunk_size & 1) next++;  /* Pad to word */
        fseek(f, next, SEEK_SET);
    }

    if (!found_video || movi_start == 0) {
        /* Try: even if handler isn't MJPG, scan movi for JPEG data */
        if (movi_start == 0) {
            fclose(f);
            free(ctx);
            return CMJ_ERROR_INVALID_DATA;
        }
    }

    /* Build frame index by scanning movi list */
    fseek(f, movi_start, SEEK_SET);
    int index_capacity = 256;
    ctx->frame_index = (avi_frame_entry *)malloc(index_capacity * sizeof(avi_frame_entry));
    if (!ctx->frame_index) {
        fclose(f);
        free(ctx);
        return CMJ_ERROR_OUT_OF_MEMORY;
    }
    ctx->frame_count = 0;

    while (ftell(f) < movi_end && !feof(f)) {
        long pos = ftell(f);
        uint32_t chunk_id = read_le32(f);
        uint32_t chunk_size = read_le32(f);

        if (feof(f) || chunk_id == 0)
            break;

        if (fourcc_match(chunk_id, FOURCC_LIST)) {
            /* Nested LIST (e.g., rec list) — read type and continue */
            read_le32(f);  /* list type */
            continue;
        }

        /* Check if this is a video data chunk (00dc or 01dc) */
        if (fourcc_match(chunk_id, FOURCC_00dc) || fourcc_match(chunk_id, FOURCC_01dc)) {
            if (chunk_size > 0) {
                /* Grow index if needed */
                if (ctx->frame_count >= index_capacity) {
                    index_capacity *= 2;
                    avi_frame_entry *new_idx = (avi_frame_entry *)realloc(
                        ctx->frame_index, index_capacity * sizeof(avi_frame_entry));
                    if (!new_idx) {
                        fclose(f);
                        free(ctx->frame_index);
                        free(ctx);
                        return CMJ_ERROR_OUT_OF_MEMORY;
                    }
                    ctx->frame_index = new_idx;
                }

                ctx->frame_index[ctx->frame_count].offset = ftell(f);
                ctx->frame_index[ctx->frame_count].size = chunk_size;
                ctx->frame_count++;
            }
        }

        /* Skip chunk data (align to word boundary) */
        long next = pos + 8 + chunk_size;
        if (chunk_size & 1) next++;
        fseek(f, next, SEEK_SET);
    }

    if (ctx->frame_count == 0) {
        fclose(f);
        free(ctx->frame_index);
        free(ctx);
        return CMJ_ERROR_INVALID_DATA;
    }

    /* Allocate initial read buffer */
    ctx->read_buffer_size = 0;
    ctx->read_buffer = NULL;
    ctx->current_frame = 0;

    *out_ctx = ctx;
    return CMJ_OK;
}

int avi_read_next_frame_data(cmj_context *ctx, const uint8_t **data, size_t *data_len)
{
    if (!ctx || !data || !data_len)
        return CMJ_ERROR_INVALID_ARG;

    if (ctx->current_frame >= ctx->frame_count)
        return CMJ_ERROR_EOF;

    avi_frame_entry *entry = &ctx->frame_index[ctx->current_frame];

    /* Ensure read buffer is large enough */
    if (entry->size > ctx->read_buffer_size) {
        uint8_t *new_buf = (uint8_t *)realloc(ctx->read_buffer, entry->size);
        if (!new_buf)
            return CMJ_ERROR_OUT_OF_MEMORY;
        ctx->read_buffer = new_buf;
        ctx->read_buffer_size = entry->size;
    }

    /* Read frame data */
    fseek(ctx->file, entry->offset, SEEK_SET);
    size_t read = fread(ctx->read_buffer, 1, entry->size, ctx->file);
    if (read != entry->size)
        return CMJ_ERROR_IO;

    *data = ctx->read_buffer;
    *data_len = entry->size;
    ctx->current_frame++;

    return CMJ_OK;
}

int avi_seek(cmj_context *ctx, int frame_number)
{
    if (!ctx)
        return CMJ_ERROR_INVALID_ARG;
    if (frame_number < 0 || frame_number >= ctx->frame_count)
        return CMJ_ERROR_INVALID_ARG;

    ctx->current_frame = frame_number;
    return CMJ_OK;
}

void avi_close(cmj_context *ctx)
{
    if (!ctx) return;
    if (ctx->file) fclose(ctx->file);
    free(ctx->frame_index);
    free(ctx->read_buffer);
    free(ctx);
}
