/*
 * test_decoder.c - Integration tests for the full JPEG decoder pipeline
 */

#include "claude_mjpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TEST(name) static int test_##name(void)
#define RUN_TEST(name) do { \
    printf("  %-40s ", #name); \
    if (test_##name() == 0) { printf("PASS\n"); passed++; } \
    else { printf("FAIL\n"); failed++; } \
    total++; \
} while(0)

static uint8_t *load_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t *)malloc(size);
    if (data) {
        fread(data, 1, size, f);
        *out_size = (size_t)size;
    }
    fclose(f);
    return data;
}

/* Test: decode the flat gray 8x8 JPEG */
TEST(decode_flat_gray)
{
    size_t size;
    uint8_t *data = load_file("test_gray8x8.jpg", &size);
    if (!data) {
        printf("(skipped: no test_gray8x8.jpg - run gen_test_jpeg first) ");
        return 0;
    }

    cmj_frame frame = {0};
    int ret = cmj_decode_frame(data, size, &frame);
    free(data);

    if (ret != CMJ_OK) {
        fprintf(stderr, "    decode failed: %s (%d)\n", cmj_error_string(ret), ret);
        return -1;
    }

    /* Check dimensions */
    if (frame.width != 8 || frame.height != 8) {
        fprintf(stderr, "    wrong dims: %dx%d\n", frame.width, frame.height);
        cmj_frame_free(&frame);
        return -1;
    }

    /* Check pixels: should all be 128 (gray) */
    int max_diff = 0;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            uint8_t r = frame.pixels[y * frame.stride + x * 3 + 0];
            uint8_t g = frame.pixels[y * frame.stride + x * 3 + 1];
            uint8_t b = frame.pixels[y * frame.stride + x * 3 + 2];
            int diff = abs((int)r - 128);
            if (diff > max_diff) max_diff = diff;
            diff = abs((int)g - 128);
            if (diff > max_diff) max_diff = diff;
            diff = abs((int)b - 128);
            if (diff > max_diff) max_diff = diff;
        }
    }

    cmj_frame_free(&frame);

    /* Allow small rounding errors (within 2) */
    if (max_diff > 2) {
        fprintf(stderr, "    max pixel diff from 128: %d (too high)\n", max_diff);
        return -1;
    }

    printf("(max_diff=%d) ", max_diff);
    return 0;
}

/* Test: decode a real-world JPEG if available */
TEST(decode_real_jpeg)
{
    size_t size;
    uint8_t *data = load_file("test_image.jpg", &size);
    if (!data) {
        printf("(skipped: no test_image.jpg) ");
        return 0;
    }

    cmj_frame frame = {0};
    int ret = cmj_decode_frame(data, size, &frame);
    free(data);

    if (ret != CMJ_OK) {
        fprintf(stderr, "    decode failed: %s (%d)\n", cmj_error_string(ret), ret);
        return -1;
    }

    printf("(%dx%d decoded OK) ", frame.width, frame.height);

    /* Write output as PPM for visual verification */
    FILE *f = fopen("test_output.ppm", "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", frame.width, frame.height);
        for (int y = 0; y < frame.height; y++) {
            fwrite(frame.pixels + y * frame.stride, 3, frame.width, f);
        }
        fclose(f);
    }

    cmj_frame_free(&frame);
    return 0;
}

/* Test: error handling for invalid data */
TEST(decode_invalid)
{
    uint8_t bad_data[] = { 0x00, 0x01, 0x02, 0x03 };
    cmj_frame frame = {0};

    int ret = cmj_decode_frame(bad_data, sizeof(bad_data), &frame);
    if (ret == CMJ_OK) {
        cmj_frame_free(&frame);
        fprintf(stderr, "    should have failed on invalid data\n");
        return -1;
    }
    return 0;
}

/* Test: null arguments */
TEST(decode_null_args)
{
    cmj_frame frame = {0};
    if (cmj_decode_frame(NULL, 0, &frame) != CMJ_ERROR_INVALID_ARG) return -1;
    uint8_t data[] = {0xFF, 0xD8};
    if (cmj_decode_frame(data, 2, NULL) != CMJ_ERROR_INVALID_ARG) return -1;
    return 0;
}

int main(void)
{
    int passed = 0, failed = 0, total = 0;

    printf("JPEG Decoder Tests:\n");
    RUN_TEST(decode_flat_gray);
    RUN_TEST(decode_real_jpeg);
    RUN_TEST(decode_invalid);
    RUN_TEST(decode_null_args);

    printf("\nResults: %d/%d passed", passed, total);
    if (failed > 0)
        printf(", %d FAILED", failed);
    printf("\n");

    return failed > 0 ? 1 : 0;
}
