/*
 * claude_mjpeg.c - Main library implementation
 */

#include "claude_mjpeg.h"
#include "jpeg_decoder.h"
#include "avi_demuxer.h"
#include "simd_detect.h"
#include <stdlib.h>
#include <string.h>

int cmj_decode_frame(const uint8_t *data, size_t len, cmj_frame *out)
{
    if (!data || !out || len == 0)
        return CMJ_ERROR_INVALID_ARG;

    return jpeg_decode(data, len, out);
}

void cmj_frame_free(cmj_frame *frame)
{
    if (frame && frame->pixels) {
        free(frame->pixels);
        frame->pixels = NULL;
    }
}

int cmj_open_file(const char *path, cmj_context **ctx)
{
    if (!path || !ctx)
        return CMJ_ERROR_INVALID_ARG;

    return avi_open(path, ctx);
}

int cmj_read_next_frame(cmj_context *ctx, cmj_frame *out)
{
    if (!ctx || !out)
        return CMJ_ERROR_INVALID_ARG;

    const uint8_t *data;
    size_t data_len;
    int ret = avi_read_next_frame_data(ctx, &data, &data_len);
    if (ret != CMJ_OK)
        return ret;

    return jpeg_decode(data, data_len, out);
}

int cmj_seek_frame(cmj_context *ctx, int frame_number)
{
    if (!ctx || frame_number < 0)
        return CMJ_ERROR_INVALID_ARG;

    return avi_seek(ctx, frame_number);
}

int cmj_get_frame_count(const cmj_context *ctx)
{
    if (!ctx) return -1;
    return ctx->frame_count;
}

void cmj_close(cmj_context *ctx)
{
    avi_close(ctx);
}

const char *cmj_error_string(int error_code)
{
    switch (error_code) {
    case CMJ_OK:                    return "Success";
    case CMJ_ERROR_INVALID_ARG:     return "Invalid argument";
    case CMJ_ERROR_INVALID_DATA:    return "Invalid or corrupt data";
    case CMJ_ERROR_NOT_IMPLEMENTED: return "Not implemented";
    case CMJ_ERROR_OUT_OF_MEMORY:   return "Out of memory";
    case CMJ_ERROR_IO:              return "I/O error";
    case CMJ_ERROR_UNSUPPORTED:     return "Unsupported format";
    case CMJ_ERROR_EOF:             return "End of file";
    default:                        return "Unknown error";
    }
}

void cmj_set_simd_enabled(int enabled)
{
    cmj_simd_enabled = enabled;
}

void cmj_set_fast_enabled(int enabled)
{
    cmj_fast_enabled = enabled;
}

int cmj_get_cpu_features(void)
{
    return cmj_detect_cpu_features();
}
