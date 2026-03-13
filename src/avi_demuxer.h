/*
 * avi_demuxer.h - AVI RIFF container parser for MJPEG video files
 */

#ifndef AVI_DEMUXER_H
#define AVI_DEMUXER_H

#include "claude_mjpeg.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* Frame index entry */
typedef struct {
    long offset;     /* File offset to start of JPEG data */
    uint32_t size;   /* Size of JPEG data in bytes */
} avi_frame_entry;

/* AVI file context */
struct cmj_context {
    FILE *file;
    int width;
    int height;
    int fps_num;
    int fps_den;
    int total_frames;
    int current_frame;

    /* Frame index */
    avi_frame_entry *frame_index;
    int frame_count;

    /* Read buffer */
    uint8_t *read_buffer;
    size_t read_buffer_size;
};

/*
 * Open and parse an AVI MJPEG file.
 * Builds a frame index for random access.
 */
int avi_open(const char *path, cmj_context **ctx);

/*
 * Read raw JPEG data for the next frame.
 *
 * data:     receives pointer to JPEG data (valid until next call or close)
 * data_len: receives size of JPEG data
 *
 * Returns CMJ_OK, or CMJ_ERROR_EOF at end.
 */
int avi_read_next_frame_data(cmj_context *ctx, const uint8_t **data, size_t *data_len);

/*
 * Seek to a specific frame.
 */
int avi_seek(cmj_context *ctx, int frame_number);

/*
 * Close the AVI file and free context.
 */
void avi_close(cmj_context *ctx);

#endif /* AVI_DEMUXER_H */
