/*
 * cmj_decode - CLI tool to extract frames from MJPEG files as PPM images
 *
 * Usage: cmj_decode <input.avi|input.jpg> [output_dir]
 */

#include "claude_mjpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

static int write_ppm(const char *path, const cmj_frame *frame)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s' for writing\n", path);
        return -1;
    }

    fprintf(f, "P6\n%d %d\n255\n", frame->width, frame->height);

    for (int y = 0; y < frame->height; y++) {
        const uint8_t *row = frame->pixels + y * frame->stride;
        fwrite(row, 3, frame->width, f);
    }

    fclose(f);
    return 0;
}

static int ends_with(const char *str, const char *suffix)
{
    size_t slen = strlen(str);
    size_t suflen = strlen(suffix);
    if (suflen > slen) return 0;
    return strcmp(str + slen - suflen, suffix) == 0;
}

static int decode_single_jpeg(const char *input, const char *output_dir)
{
    FILE *f = fopen(input, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s'\n", input);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = (uint8_t *)malloc(size);
    if (!data) {
        fclose(f);
        fprintf(stderr, "Error: out of memory\n");
        return 1;
    }

    fread(data, 1, size, f);
    fclose(f);

    cmj_frame frame = {0};
    int ret = cmj_decode_frame(data, size, &frame);
    free(data);

    if (ret != CMJ_OK) {
        fprintf(stderr, "Error: decode failed: %s\n", cmj_error_string(ret));
        return 1;
    }

    char outpath[512];
    snprintf(outpath, sizeof(outpath), "%s/frame_0000.ppm", output_dir);

    printf("Decoded %dx%d frame -> %s\n", frame.width, frame.height, outpath);
    write_ppm(outpath, &frame);

    cmj_frame_free(&frame);
    return 0;
}

static int decode_avi(const char *input, const char *output_dir)
{
    cmj_context *ctx = NULL;
    int ret = cmj_open_file(input, &ctx);
    if (ret != CMJ_OK) {
        fprintf(stderr, "Error: cannot open '%s': %s\n", input, cmj_error_string(ret));
        return 1;
    }

    int frame_num = 0;
    cmj_frame frame = {0};

    while ((ret = cmj_read_next_frame(ctx, &frame)) == CMJ_OK) {
        char outpath[512];
        snprintf(outpath, sizeof(outpath), "%s/frame_%04d.ppm", output_dir, frame_num);

        printf("Frame %d: %dx%d -> %s\n", frame_num, frame.width, frame.height, outpath);
        write_ppm(outpath, &frame);

        cmj_frame_free(&frame);
        memset(&frame, 0, sizeof(frame));
        frame_num++;
    }

    if (ret != CMJ_ERROR_EOF) {
        fprintf(stderr, "Error at frame %d: %s\n", frame_num, cmj_error_string(ret));
    }

    printf("Extracted %d frames\n", frame_num);
    cmj_close(ctx);
    return 0;
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <input.avi|input.jpg> [output_dir]\n", prog);
    fprintf(stderr, "\nExtracts frames from MJPEG AVI files or single JPEG images as PPM.\n");
    fprintf(stderr, "Default output directory: ./frames\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *input = argv[1];
    const char *output_dir = argc > 2 ? argv[2] : "frames";

    /* Create output dir (best effort) */
#ifdef _WIN32
    _mkdir(output_dir);
#else
    mkdir(output_dir, 0755);
#endif

    if (ends_with(input, ".avi") || ends_with(input, ".AVI")) {
        return decode_avi(input, output_dir);
    } else {
        return decode_single_jpeg(input, output_dir);
    }
}
