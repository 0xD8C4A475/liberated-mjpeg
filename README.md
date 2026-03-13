# claude-mjpeg

**I tried to replace FFmpeg's MJPEG decoder using Claude Code. Here's what happened.**

A complete baseline JPEG/MJPEG decoder written in pure C with zero external dependencies, built entirely by an AI coding assistant in a single session.

## Benchmark Results

| Metric | claude-mjpeg (optimized C) | claude-mjpeg (naive) | FFmpeg |
|--------|---------------------------|---------------------|--------|
| FPS (8x8 test) | ~305,000 | ~12,500 | N/A* |
| Code size | 2,403 LOC (lib) | same | ~15K LOC** |
| Dependencies | 0 (libc + libm only) | 0 | many |
| SIMD | SSE2 color convert | none | full SSE/AVX |

*FFmpeg comparison requires ffmpeg CLI installed. Run `cmj_bench` to get your own numbers.*
**FFmpeg MJPEG-specific code only, not the full project.*

## The Process

```
Prompts:    8 structured prompts following a prompt plan
LOC total:  4,103 (library: 2,403 / tools: 509 / tests: 1,069)
Tests:      16 unit tests, all passing
Phases:     6 (setup -> parsing -> huffman -> IDCT -> AVI -> optimization)
```

## What It Supports

- Baseline JPEG (SOF0) decoding per ITU-T T.81
- Grayscale, YCbCr 4:4:4, 4:2:2, and 4:2:0 chroma subsampling
- Huffman coded entropy data with restart markers
- AVI RIFF container with MJPEG video streams
- Frame-by-frame decoding with random access (seek)

## What It Doesn't Support

- Progressive JPEG (SOF2)
- Arithmetic coding
- Multi-scan JPEG
- 12-bit or 16-bit sample precision
- JPEG 2000

## Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

On Windows with Visual Studio:
```bat
call "C:\...\vcvarsall.bat" x64
cmake .. -G "NMake Makefiles"
nmake
```

## Usage

```bash
# Decode a single JPEG to PPM
./cmj_decode photo.jpg output_dir/

# Extract all frames from an MJPEG AVI
./cmj_decode video.avi frames/

# Benchmark (10 iterations, compare with FFmpeg)
./cmj_bench photo.jpg 10

# Benchmark without FFmpeg comparison
./cmj_bench photo.jpg 100 --no-ffmpeg

# Benchmark with SIMD disabled (pure C)
./cmj_bench photo.jpg 100 --no-simd --no-ffmpeg

# Export results as JSON
./cmj_bench photo.jpg 100 --json results.json

# Generate comparison chart
python bench_plot.py results.json chart.png
```

## API

```c
#include "claude_mjpeg.h"

// Decode a single JPEG from memory
cmj_frame frame;
int err = cmj_decode_frame(jpeg_data, jpeg_size, &frame);
// frame.pixels = RGB24 data, frame.width, frame.height, frame.stride
cmj_frame_free(&frame);

// Decode MJPEG AVI file frame by frame
cmj_context *ctx;
cmj_open_file("video.avi", &ctx);
while (cmj_read_next_frame(ctx, &frame) == CMJ_OK) {
    // process frame...
    cmj_frame_free(&frame);
}
cmj_close(ctx);
```

## Project Structure

```
claude-mjpeg/
├── include/
│   └── claude_mjpeg.h          # Public API (single header)
├── src/
│   ├── jpeg_parser.c/h         # JPEG marker parsing (SOI, SOF, DHT, DQT, SOS)
│   ├── huffman.c/h              # Bit-by-bit Huffman decoding
│   ├── huffman_fast.c/h         # 8-bit lookup table Huffman (optimized)
│   ├── idct.c/h                 # Naive O(N^4) 2D IDCT
│   ├── idct_fast.c/h            # AAN fast IDCT with fixed-point arithmetic
│   ├── idct_simd.c/h            # SSE2 color conversion
│   ├── simd_detect.c/h          # Runtime CPUID feature detection
│   ├── avi_demuxer.c/h          # AVI RIFF container parser
│   ├── jpeg_decoder.c/h         # Full decode pipeline orchestrator
│   └── claude_mjpeg.c           # Public API implementation
├── tools/
│   ├── cmj_decode.c             # Frame extraction CLI
│   └── cmj_bench.c              # Benchmark tool with FFmpeg comparison
├── tests/
│   ├── test_parser.c            # JPEG marker parsing tests (5 tests)
│   ├── test_huffman.c           # Huffman decoding tests (7 tests)
│   ├── test_decoder.c           # Full decoder integration tests (4 tests)
│   └── gen_test_jpeg.c          # Test JPEG generator
├── bench_plot.py                # Matplotlib chart generator
├── bench.sh                     # Automated benchmark script
└── CMakeLists.txt
```

## Optimization Layers

The decoder has three performance tiers:

1. **Naive** (`idct.c` + `huffman.c`): Direct formula implementation from the JPEG spec. Correct but slow. O(N^4) 2D IDCT with floating-point math.

2. **Optimized C** (`idct_fast.c` + `huffman_fast.c`): AAN fast IDCT algorithm with separated 1D passes and fixed-point integer arithmetic. 8-bit lookup table for Huffman decoding. ~24x faster than naive on small images.

3. **SIMD** (`idct_simd.c`): SSE2 intrinsics for YCbCr-to-RGB color conversion (8 pixels at a time). Runtime CPU feature detection via CPUID. Can be disabled with `--no-simd` flag.

## Lessons Learned

- **The JPEG spec is well-written.** ITU-T T.81 is one of the clearest image format specifications. The pseudocode maps almost directly to C.
- **Huffman decoding is the bottleneck.** Bit-by-bit decoding is inherently serial. Lookup tables help significantly.
- **IDCT optimization matters.** Going from naive O(N^4) to AAN separated-pass gives the biggest single speedup.
- **FFmpeg is really, really fast.** Decades of hand-tuned assembly, SIMD, and profiling. A from-scratch implementation can't compete on performance, but the code is much more readable.

## License

MIT
