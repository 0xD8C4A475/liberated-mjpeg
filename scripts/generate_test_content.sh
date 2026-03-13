#!/bin/bash
# Generate test content using FFmpeg for benchmarking claude-mjpeg
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TESTDATA="$PROJECT_DIR/testdata"
FFMPEG="${FFMPEG:-ffmpeg}"

mkdir -p "$TESTDATA"

echo "=== Generating test content ==="

echo "[1/4] 1080p MJPEG AVI (10s, 30fps, quality 2)..."
"$FFMPEG" -y -f lavfi -i testsrc=duration=10:size=1920x1080:rate=30 \
    -c:v mjpeg -q:v 2 "$TESTDATA/test_1080p_q2.avi"

echo "[2/4] 720p MJPEG AVI (10s, 30fps, quality 2)..."
"$FFMPEG" -y -f lavfi -i testsrc=duration=10:size=1280x720:rate=30 \
    -c:v mjpeg -q:v 2 "$TESTDATA/test_720p_q2.avi"

echo "[3/4] 480p MJPEG AVI (5s, 30fps, quality 5)..."
"$FFMPEG" -y -f lavfi -i testsrc=duration=5:size=640x480:rate=30 \
    -c:v mjpeg -q:v 5 "$TESTDATA/test_480p_q5.avi"

echo "[4/4] Reference 1080p JPEG frame..."
"$FFMPEG" -y -f lavfi -i testsrc=size=1920x1080 \
    -frames:v 1 -q:v 2 "$TESTDATA/reference_frame.jpg"

echo ""
echo "=== Done! Generated files: ==="
ls -lh "$TESTDATA/"
