/*
 * jpeg_decoder.h - Full JPEG baseline decoder
 *
 * Orchestrates parsing, Huffman decoding, IDCT, and color conversion
 * to produce a complete decoded frame.
 */

#ifndef JPEG_DECODER_H
#define JPEG_DECODER_H

#include "claude_mjpeg.h"
#include <stdint.h>
#include <stddef.h>

/*
 * Decode a complete baseline JPEG image from memory to RGB24 pixels.
 *
 * data: pointer to JPEG data (starting with SOI marker 0xFF 0xD8)
 * len:  size of JPEG data in bytes
 * out:  pointer to cmj_frame to fill (pixels allocated internally)
 *
 * Returns CMJ_OK on success, negative error code on failure.
 * On success, caller must free frame with cmj_frame_free().
 */
int jpeg_decode(const uint8_t *data, size_t len, cmj_frame *out);

#endif /* JPEG_DECODER_H */
