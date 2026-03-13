#!/bin/bash
# bench.sh - Run claude-mjpeg benchmarks and generate comparison charts
#
# Prerequisites:
#   - cmake + C compiler
#   - ffmpeg (optional, for comparison)
#   - python3 + matplotlib (optional, for charts)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
TEST_DIR="$SCRIPT_DIR/test_data"
RESULTS_DIR="$SCRIPT_DIR/bench_results"

# Create directories
mkdir -p "$TEST_DIR" "$RESULTS_DIR"

# Build if needed
if [ ! -f "$BUILD_DIR/cmj_bench" ] && [ ! -f "$BUILD_DIR/cmj_bench.exe" ]; then
    echo "Building claude-mjpeg..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake .. && cmake --build .
    cd "$SCRIPT_DIR"
fi

# Find the benchmark executable
CMJ_BENCH="$BUILD_DIR/cmj_bench"
[ -f "$CMJ_BENCH.exe" ] && CMJ_BENCH="$CMJ_BENCH.exe"

GEN_JPEG="$BUILD_DIR/gen_test_jpeg"
[ -f "$GEN_JPEG.exe" ] && GEN_JPEG="$GEN_JPEG.exe"

# Generate test JPEG if none exists
TEST_JPEG="$TEST_DIR/test_8x8.jpg"
if [ ! -f "$TEST_JPEG" ]; then
    echo "Generating test JPEG..."
    "$GEN_JPEG" "$TEST_JPEG"
fi

# Try to create a larger test image using ffmpeg
TEST_LARGE="$TEST_DIR/test_640x480.jpg"
if [ ! -f "$TEST_LARGE" ] && command -v ffmpeg &>/dev/null; then
    echo "Generating 640x480 test JPEG using FFmpeg..."
    ffmpeg -y -f lavfi -i "testsrc=duration=0.04:size=640x480:rate=25" \
           -frames:v 1 -q:v 2 "$TEST_LARGE" 2>/dev/null || true
fi

# Try to create an MJPEG AVI using ffmpeg
TEST_AVI="$TEST_DIR/test_mjpeg.avi"
if [ ! -f "$TEST_AVI" ] && command -v ffmpeg &>/dev/null; then
    echo "Generating MJPEG AVI test file (10 frames, 640x480)..."
    ffmpeg -y -f lavfi -i "testsrc=duration=0.4:size=640x480:rate=25" \
           -c:v mjpeg -q:v 2 "$TEST_AVI" 2>/dev/null || true
fi

echo ""
echo "============================================"
echo "  claude-mjpeg Benchmark Suite"
echo "============================================"
echo ""

# Benchmark 1: Small JPEG
if [ -f "$TEST_JPEG" ]; then
    echo "--- Benchmark: 8x8 JPEG (100 iterations) ---"
    "$CMJ_BENCH" "$TEST_JPEG" 100 --json "$RESULTS_DIR/bench_8x8.json" --no-ffmpeg
    echo ""
fi

# Benchmark 2: Larger JPEG
if [ -f "$TEST_LARGE" ]; then
    echo "--- Benchmark: 640x480 JPEG (10 iterations) ---"
    "$CMJ_BENCH" "$TEST_LARGE" 10 --json "$RESULTS_DIR/bench_640x480.json"
    echo ""
fi

# Benchmark 3: MJPEG AVI
if [ -f "$TEST_AVI" ]; then
    echo "--- Benchmark: MJPEG AVI (10 iterations) ---"
    "$CMJ_BENCH" "$TEST_AVI" 10 --json "$RESULTS_DIR/bench_mjpeg_avi.json"
    echo ""
fi

# Generate charts
if command -v python3 &>/dev/null; then
    for json_file in "$RESULTS_DIR"/*.json; do
        if [ -f "$json_file" ]; then
            base=$(basename "$json_file" .json)
            python3 "$SCRIPT_DIR/bench_plot.py" "$json_file" "$RESULTS_DIR/${base}_chart.png" 2>/dev/null || true
        fi
    done
elif command -v python &>/dev/null; then
    for json_file in "$RESULTS_DIR"/*.json; do
        if [ -f "$json_file" ]; then
            base=$(basename "$json_file" .json)
            python "$SCRIPT_DIR/bench_plot.py" "$json_file" "$RESULTS_DIR/${base}_chart.png" 2>/dev/null || true
        fi
    done
fi

echo "============================================"
echo "  Benchmark complete!"
echo "  Results in: $RESULTS_DIR/"
echo "============================================"
