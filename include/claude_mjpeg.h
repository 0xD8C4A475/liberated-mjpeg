/*
 * claude_mjpeg.h - Public API for the claude-mjpeg decoder library
 *
 * A pure C MJPEG/JPEG decoder with zero external dependencies.
 * Built entirely with Claude Code as an experiment.
 */

#ifndef CLAUDE_MJPEG_H
#define CLAUDE_MJPEG_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
#define CMJ_OK                    0
#define CMJ_ERROR_INVALID_ARG    -1
#define CMJ_ERROR_INVALID_DATA   -2
#define CMJ_ERROR_NOT_IMPLEMENTED -3
#define CMJ_ERROR_OUT_OF_MEMORY  -4
#define CMJ_ERROR_IO             -5
#define CMJ_ERROR_UNSUPPORTED    -6
#define CMJ_ERROR_EOF            -7

/* Pixel formats */
typedef enum {
    CMJ_PIXFMT_RGB24 = 0,    /* 3 bytes per pixel: R, G, B */
    CMJ_PIXFMT_RGBA32,       /* 4 bytes per pixel: R, G, B, A */
    CMJ_PIXFMT_YUV420P,      /* Planar YUV 4:2:0 */
} cmj_pixel_format;

/* Decoded frame */
typedef struct {
    int width;
    int height;
    cmj_pixel_format pixel_format;
    uint8_t *pixels;
    int stride;               /* Bytes per row */
} cmj_frame;

/* MJPEG file context (opaque) */
typedef struct cmj_context cmj_context;

/*
 * Decode a single JPEG frame from memory.
 *
 * data: pointer to JPEG data (starting with SOI marker)
 * len:  size of JPEG data in bytes
 * out:  pointer to cmj_frame struct to fill (pixels allocated internally)
 *
 * Returns CMJ_OK on success, negative error code on failure.
 * On success, caller must free the frame with cmj_frame_free().
 */
int cmj_decode_frame(const uint8_t *data, size_t len, cmj_frame *out);

/*
 * Free a decoded frame's pixel buffer.
 */
void cmj_frame_free(cmj_frame *frame);

/*
 * Open an MJPEG file (AVI container) for frame-by-frame decoding.
 *
 * path: filesystem path to .avi file
 * ctx:  pointer to receive the allocated context
 *
 * Returns CMJ_OK on success.
 */
int cmj_open_file(const char *path, cmj_context **ctx);

/*
 * Read and decode the next frame from the MJPEG file.
 *
 * ctx: context from cmj_open_file
 * out: pointer to cmj_frame struct to fill
 *
 * Returns CMJ_OK on success, CMJ_ERROR_EOF when no more frames.
 */
int cmj_read_next_frame(cmj_context *ctx, cmj_frame *out);

/*
 * Seek to a specific frame number (0-based).
 *
 * Returns CMJ_OK on success.
 */
int cmj_seek_frame(cmj_context *ctx, int frame_number);

/*
 * Get total number of frames in the file.
 * Returns -1 if unknown.
 */
int cmj_get_frame_count(const cmj_context *ctx);

/*
 * Close the MJPEG file and free the context.
 */
void cmj_close(cmj_context *ctx);

/*
 * Get a human-readable error string.
 */
const char *cmj_error_string(int error_code);

/*
 * Enable or disable SIMD optimizations (default: enabled).
 * Set to 0 to use pure C code paths, 1 to use SIMD when available.
 */
void cmj_set_simd_enabled(int enabled);

/*
 * Enable or disable fast algorithms (AAN IDCT, lookup Huffman).
 * Set to 0 to use naive O(N^4) IDCT and bit-by-bit Huffman.
 * Default: enabled.
 */
void cmj_set_fast_enabled(int enabled);

/*
 * Query detected CPU features. Returns a bitmask.
 */
int cmj_get_cpu_features(void);

#ifdef __cplusplus
}
#endif

#endif /* CLAUDE_MJPEG_H */
