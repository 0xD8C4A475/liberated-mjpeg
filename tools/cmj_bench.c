/*
 * cmj_bench - Comprehensive benchmark tool for claude-mjpeg decoder
 *
 * Usage: cmj_bench <input.avi|input.jpg> [iterations] [--json output.json] [--no-ffmpeg]
 *
 * Benchmarks the claude-mjpeg decoder and optionally compares with FFmpeg.
 * Reports results as markdown table and JSON.
 */

#include "claude_mjpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
static double get_time_ms(void)
{
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart * 1000.0;
}

static size_t get_peak_memory_kb(void)
{
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return pmc.PeakWorkingSetSize / 1024;
    return 0;
}
#else
#include <sys/resource.h>
static double get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static size_t get_peak_memory_kb(void)
{
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0)
        return (size_t)ru.ru_maxrss;
    return 0;
}
#endif

static int ends_with(const char *str, const char *suffix)
{
    size_t slen = strlen(str);
    size_t suflen = strlen(suffix);
    if (suflen > slen) return 0;
    return strcmp(str + slen - suflen, suffix) == 0;
}

typedef struct {
    const char *decoder_name;
    double total_ms;
    double avg_ms;
    double fps;
    double mp_per_sec;
    size_t peak_memory_kb;
    int width;
    int height;
    int frame_count;
    int iterations;
} bench_result;

static int bench_single_jpeg(const char *input, int iterations, bench_result *result)
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

    /* First decode to get dimensions */
    cmj_frame frame = {0};
    int ret = cmj_decode_frame(data, size, &frame);
    if (ret != CMJ_OK) {
        free(data);
        fprintf(stderr, "Error: decode failed: %s\n", cmj_error_string(ret));
        return 1;
    }

    result->width = frame.width;
    result->height = frame.height;
    result->frame_count = 1;
    result->iterations = iterations;
    cmj_frame_free(&frame);

    /* Benchmark loop */
    double start = get_time_ms();

    for (int i = 0; i < iterations; i++) {
        memset(&frame, 0, sizeof(frame));
        ret = cmj_decode_frame(data, size, &frame);
        if (ret != CMJ_OK) {
            fprintf(stderr, "Error at iteration %d: %s\n", i, cmj_error_string(ret));
            free(data);
            return 1;
        }
        cmj_frame_free(&frame);
    }

    double end = get_time_ms();
    result->total_ms = end - start;
    result->avg_ms = result->total_ms / iterations;
    result->fps = 1000.0 / result->avg_ms;
    result->mp_per_sec = ((double)(result->width * result->height) / 1000000.0) * result->fps;
    result->peak_memory_kb = get_peak_memory_kb();
    result->decoder_name = "claude-mjpeg";

    free(data);
    return 0;
}

static int bench_avi(const char *input, int iterations, bench_result *result)
{
    cmj_context *ctx = NULL;
    int ret = cmj_open_file(input, &ctx);
    if (ret != CMJ_OK) {
        fprintf(stderr, "Error: cannot open '%s': %s\n", input, cmj_error_string(ret));
        return 1;
    }

    int frame_count = cmj_get_frame_count(ctx);
    result->frame_count = frame_count;
    result->iterations = iterations;

    /* First pass to get dimensions */
    cmj_frame frame = {0};
    ret = cmj_read_next_frame(ctx, &frame);
    if (ret != CMJ_OK) {
        fprintf(stderr, "Error: first frame decode failed: %s\n", cmj_error_string(ret));
        cmj_close(ctx);
        return 1;
    }
    result->width = frame.width;
    result->height = frame.height;
    cmj_frame_free(&frame);

    /* Benchmark loop */
    double start = get_time_ms();
    int total_decoded = 0;

    for (int iter = 0; iter < iterations; iter++) {
        cmj_seek_frame(ctx, 0);

        for (int i = 0; i < frame_count; i++) {
            memset(&frame, 0, sizeof(frame));
            ret = cmj_read_next_frame(ctx, &frame);
            if (ret != CMJ_OK) break;
            cmj_frame_free(&frame);
            total_decoded++;
        }
    }

    double end = get_time_ms();
    result->total_ms = end - start;
    result->avg_ms = result->total_ms / total_decoded;
    result->fps = 1000.0 / result->avg_ms;
    result->mp_per_sec = ((double)(result->width * result->height) / 1000000.0) * result->fps;
    result->peak_memory_kb = get_peak_memory_kb();
    result->decoder_name = "claude-mjpeg";

    cmj_close(ctx);
    return 0;
}

static double bench_ffmpeg(const char *input)
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -hide_banner -benchmark -i \"%s\" -f rawvideo -pix_fmt rgb24 -an - > "
#ifdef _WIN32
        "NUL"
#else
        "/dev/null"
#endif
        " 2>&1", input);

    double start = get_time_ms();
    int ret = system(cmd);
    double end = get_time_ms();

    if (ret != 0) {
        fprintf(stderr, "Warning: ffmpeg command failed (ret=%d). Is ffmpeg installed?\n", ret);
        return -1.0;
    }

    return end - start;
}

static void print_results(const bench_result *cmj, double ffmpeg_ms)
{
    double megapixels = (double)(cmj->width * cmj->height) / 1000000.0;

    printf("\n=== Benchmark Results ===\n\n");
    printf("Input: %dx%d (%.2f MP), %d frames\n\n", cmj->width, cmj->height,
           megapixels, cmj->frame_count);

    printf("| Metric              | claude-mjpeg  |");
    if (ffmpeg_ms > 0) printf(" FFmpeg        |");
    printf("\n");

    printf("|---------------------|---------------|");
    if (ffmpeg_ms > 0) printf("---------------|");
    printf("\n");

    printf("| Total time          | %10.2f ms |", cmj->total_ms);
    if (ffmpeg_ms > 0) printf(" %10.2f ms |", ffmpeg_ms);
    printf("\n");

    printf("| Avg time/frame      | %10.3f ms |", cmj->avg_ms);
    if (ffmpeg_ms > 0 && cmj->frame_count > 0) {
        double ff_avg = ffmpeg_ms / (cmj->frame_count * cmj->iterations);
        printf(" %10.3f ms |", ff_avg);
    }
    printf("\n");

    printf("| FPS                 | %10.1f    |", cmj->fps);
    if (ffmpeg_ms > 0 && cmj->frame_count > 0) {
        double ff_fps = 1000.0 / (ffmpeg_ms / (cmj->frame_count * cmj->iterations));
        printf(" %10.1f    |", ff_fps);
    }
    printf("\n");

    printf("| Megapixels/sec      | %10.2f    |", cmj->mp_per_sec);
    printf("\n");

    printf("| Peak memory (KB)    | %10zu    |", cmj->peak_memory_kb);
    printf("\n\n");
}

static void write_json(const char *json_path, const char *input, const bench_result *cmj, double ffmpeg_ms)
{
    FILE *f = fopen(json_path, "w");
    if (!f) {
        fprintf(stderr, "Warning: cannot write JSON to '%s'\n", json_path);
        return;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"input\": \"%s\",\n", input);
    fprintf(f, "  \"width\": %d,\n", cmj->width);
    fprintf(f, "  \"height\": %d,\n", cmj->height);
    fprintf(f, "  \"frame_count\": %d,\n", cmj->frame_count);
    fprintf(f, "  \"iterations\": %d,\n", cmj->iterations);
    fprintf(f, "  \"claude_mjpeg\": {\n");
    fprintf(f, "    \"total_ms\": %.2f,\n", cmj->total_ms);
    fprintf(f, "    \"avg_ms\": %.3f,\n", cmj->avg_ms);
    fprintf(f, "    \"fps\": %.1f,\n", cmj->fps);
    fprintf(f, "    \"mp_per_sec\": %.2f,\n", cmj->mp_per_sec);
    fprintf(f, "    \"peak_memory_kb\": %zu\n", cmj->peak_memory_kb);
    fprintf(f, "  }");

    if (ffmpeg_ms > 0) {
        double ff_avg = ffmpeg_ms / (cmj->frame_count * cmj->iterations);
        double ff_fps = 1000.0 / ff_avg;
        fprintf(f, ",\n  \"ffmpeg\": {\n");
        fprintf(f, "    \"total_ms\": %.2f,\n", ffmpeg_ms);
        fprintf(f, "    \"avg_ms\": %.3f,\n", ff_avg);
        fprintf(f, "    \"fps\": %.1f\n", ff_fps);
        fprintf(f, "  }");
    }

    fprintf(f, "\n}\n");
    fclose(f);
    printf("JSON results written to: %s\n", json_path);
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <input.avi|input.jpg> [iterations] [--json output.json] [--no-ffmpeg] [--no-simd]\n", prog);
    fprintf(stderr, "\nBenchmark the claude-mjpeg decoder and compare with FFmpeg.\n");
    fprintf(stderr, "  --no-simd    Disable SIMD optimizations (use pure C code)\n");
    fprintf(stderr, "  --no-ffmpeg  Skip FFmpeg comparison\n");
    fprintf(stderr, "Default iterations: 10\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *input = argv[1];
    int iterations = 10;
    const char *json_path = NULL;
    int skip_ffmpeg = 0;
    int no_simd = 0;

    /* Parse arguments */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            json_path = argv[++i];
        } else if (strcmp(argv[i], "--no-ffmpeg") == 0) {
            skip_ffmpeg = 1;
        } else if (strcmp(argv[i], "--no-simd") == 0) {
            no_simd = 1;
        } else {
            int n = atoi(argv[i]);
            if (n > 0) iterations = n;
        }
    }

    if (no_simd) {
        cmj_set_simd_enabled(0);
    }

    printf("claude-mjpeg Benchmark\n");
    printf("======================\n");
    printf("Input: %s\n", input);
    printf("Iterations: %d\n", iterations);
    printf("SIMD: %s\n", no_simd ? "disabled" : "enabled");

    bench_result cmj_result = {0};
    int ret;

    if (ends_with(input, ".avi") || ends_with(input, ".AVI")) {
        ret = bench_avi(input, iterations, &cmj_result);
    } else {
        ret = bench_single_jpeg(input, iterations, &cmj_result);
    }

    if (ret != 0) return ret;

    /* FFmpeg comparison */
    double ffmpeg_ms = -1.0;
    if (!skip_ffmpeg) {
        printf("\nRunning FFmpeg comparison...\n");
        ffmpeg_ms = bench_ffmpeg(input);
    }

    print_results(&cmj_result, ffmpeg_ms);

    if (json_path) {
        write_json(json_path, input, &cmj_result, ffmpeg_ms);
    }

    return 0;
}
