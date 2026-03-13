#!/usr/bin/env python3
"""
Quality validation script for claude-mjpeg decoder.

Compares output of cmj_decode with FFmpeg's decode of the same JPEG,
computes PSNR and SSIM, and generates a side-by-side comparison image.

Usage:
    python scripts/validate_quality.py [--ffmpeg PATH]
"""

import subprocess
import os
import sys
import json
import struct
import platform
import math

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
BUILD_DIR = os.path.join(PROJECT_DIR, "build")
TESTDATA_DIR = os.path.join(PROJECT_DIR, "testdata")
DOCS_DIR = os.path.join(PROJECT_DIR, "docs")

CMJ_DECODE = os.path.join(BUILD_DIR, "cmj_decode.exe" if platform.system() == "Windows" else "cmj_decode")
REFERENCE_JPEG = os.path.join(TESTDATA_DIR, "reference_frame.jpg")


def find_ffmpeg():
    """Find FFmpeg executable."""
    candidates = [
        "ffmpeg",
        r"C:\Program Files\ShareX\ffmpeg.exe",
        r"C:\ffmpeg\bin\ffmpeg.exe",
        "/usr/bin/ffmpeg",
    ]
    for c in candidates:
        try:
            subprocess.run([c, "-version"], capture_output=True, timeout=5)
            return c
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
    return None


def read_ppm(path):
    """Read a PPM (P6) file and return (width, height, pixels_rgb)."""
    with open(path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"P6":
            raise ValueError(f"Not a P6 PPM file: {magic}")

        # Skip comments
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()

        parts = line.strip().split()
        width, height = int(parts[0]), int(parts[1])
        maxval = int(f.readline().strip())
        if maxval != 255:
            raise ValueError(f"Unsupported maxval: {maxval}")

        pixels = f.read(width * height * 3)
        return width, height, pixels


def decode_with_cmj(jpeg_path, output_dir):
    """Decode JPEG with cmj_decode, return PPM path."""
    os.makedirs(output_dir, exist_ok=True)
    result = subprocess.run(
        [CMJ_DECODE, jpeg_path, output_dir],
        capture_output=True, text=True, timeout=30
    )
    if result.returncode != 0:
        print(f"cmj_decode error: {result.stderr}")
        return None

    # Find the output PPM
    for f in os.listdir(output_dir):
        if f.endswith(".ppm"):
            return os.path.join(output_dir, f)
    return None


def decode_with_ffmpeg(ffmpeg_path, jpeg_path, output_ppm):
    """Decode JPEG with FFmpeg to PPM."""
    result = subprocess.run(
        [ffmpeg_path, "-y", "-i", jpeg_path, output_ppm],
        capture_output=True, timeout=30
    )
    return result.returncode == 0


def compute_psnr(pixels1, pixels2, width, height):
    """Compute PSNR between two RGB pixel buffers."""
    if len(pixels1) != len(pixels2):
        return 0.0

    mse = 0.0
    n = width * height * 3
    for i in range(n):
        diff = pixels1[i] - pixels2[i]
        mse += diff * diff
    mse /= n

    if mse == 0:
        return float("inf")
    return 10.0 * math.log10(255.0 * 255.0 / mse)


def compute_ssim_simple(pixels1, pixels2, width, height):
    """Compute a simplified SSIM (luminance channel only, per-block)."""
    # Convert to grayscale
    gray1 = []
    gray2 = []
    for i in range(0, len(pixels1), 3):
        gray1.append(0.299 * pixels1[i] + 0.587 * pixels1[i+1] + 0.114 * pixels1[i+2])
        gray2.append(0.299 * pixels2[i] + 0.587 * pixels2[i+1] + 0.114 * pixels2[i+2])

    # Constants
    C1 = (0.01 * 255) ** 2
    C2 = (0.03 * 255) ** 2

    # Block-based SSIM (8x8 blocks)
    block_size = 8
    ssim_sum = 0.0
    count = 0

    for by in range(0, height - block_size + 1, block_size):
        for bx in range(0, width - block_size + 1, block_size):
            sum1 = sum2 = sum1_sq = sum2_sq = sum12 = 0.0
            for dy in range(block_size):
                for dx in range(block_size):
                    idx = (by + dy) * width + (bx + dx)
                    v1 = gray1[idx]
                    v2 = gray2[idx]
                    sum1 += v1
                    sum2 += v2
                    sum1_sq += v1 * v1
                    sum2_sq += v2 * v2
                    sum12 += v1 * v2

            n = block_size * block_size
            mu1 = sum1 / n
            mu2 = sum2 / n
            sigma1_sq = sum1_sq / n - mu1 * mu1
            sigma2_sq = sum2_sq / n - mu2 * mu2
            sigma12 = sum12 / n - mu1 * mu2

            ssim = ((2 * mu1 * mu2 + C1) * (2 * sigma12 + C2)) / \
                   ((mu1 * mu1 + mu2 * mu2 + C1) * (sigma1_sq + sigma2_sq + C2))
            ssim_sum += ssim
            count += 1

    return ssim_sum / count if count > 0 else 0.0


def generate_comparison_image(cmj_pixels, ff_pixels, width, height, psnr, ssim, output_path):
    """Generate side-by-side comparison image."""
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        print("WARNING: Pillow not installed. Skipping comparison image.")
        return

    # Create images from raw pixels
    cmj_img = Image.frombytes("RGB", (width, height), bytes(cmj_pixels))
    ff_img = Image.frombytes("RGB", (width, height), bytes(ff_pixels))

    # Create side-by-side with label bar
    label_height = 60
    gap = 4
    total_width = width * 2 + gap
    total_height = height + label_height

    canvas = Image.new("RGB", (total_width, total_height), "#1a1a2e")
    canvas.paste(ff_img, (0, label_height))
    canvas.paste(cmj_img, (width + gap, label_height))

    draw = ImageDraw.Draw(canvas)

    # Try to use a decent font
    font_size = 20
    try:
        font = ImageFont.truetype("arial.ttf", font_size)
        font_small = ImageFont.truetype("arial.ttf", 14)
    except (IOError, OSError):
        font = ImageFont.load_default()
        font_small = font

    # Labels
    draw.text((width // 2, 8), "FFmpeg", fill="#FF6B6B", anchor="mt", font=font)
    draw.text((width + gap + width // 2, 8), "claude-mjpeg", fill="#96E6A1", anchor="mt", font=font)

    # PSNR/SSIM
    psnr_text = f"PSNR: {psnr:.2f} dB" if psnr != float("inf") else "PSNR: ∞ (identical)"
    ssim_text = f"SSIM: {ssim:.4f}"
    draw.text((total_width // 2, 38), f"{psnr_text}  |  {ssim_text}", fill="#e2e2e2", anchor="mt", font=font_small)

    canvas.save(output_path, quality=95)
    print(f"  Saved: {output_path}")


def compute_pixel_diff_stats(pixels1, pixels2):
    """Compute per-channel difference statistics."""
    max_diff = 0
    total_diff = 0
    n = len(pixels1)
    channel_diff_histogram = [0] * 256

    for i in range(n):
        d = abs(pixels1[i] - pixels2[i])
        if d > max_diff:
            max_diff = d
        total_diff += d
        channel_diff_histogram[d] += 1

    num_channels = n
    return {
        "max_diff": max_diff,
        "mean_diff": total_diff / num_channels if num_channels > 0 else 0,
        "identical_channels_pct": channel_diff_histogram[0] / num_channels * 100 if num_channels > 0 else 0,
        "within_1_pct": sum(channel_diff_histogram[:2]) / num_channels * 100 if num_channels > 0 else 0,
        "within_2_pct": sum(channel_diff_histogram[:3]) / num_channels * 100 if num_channels > 0 else 0,
    }


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Validate claude-mjpeg decode quality")
    parser.add_argument("--ffmpeg", default=None, help="Path to FFmpeg executable")
    parser.add_argument("--input", default=None, help="Input JPEG file (default: testdata/reference_frame.jpg)")
    args = parser.parse_args()

    ffmpeg_path = args.ffmpeg or find_ffmpeg()
    jpeg_path = args.input or REFERENCE_JPEG

    if not os.path.exists(CMJ_DECODE):
        print(f"ERROR: cmj_decode not found at {CMJ_DECODE}")
        sys.exit(1)

    if not os.path.exists(jpeg_path):
        print(f"ERROR: Input JPEG not found: {jpeg_path}")
        sys.exit(1)

    if not ffmpeg_path:
        print("ERROR: FFmpeg not found. Cannot run quality comparison.")
        sys.exit(1)

    print(f"Input: {jpeg_path}")
    print(f"FFmpeg: {ffmpeg_path}")

    # Decode with both decoders
    cmj_output_dir = os.path.join(TESTDATA_DIR, "_cmj_output")
    ff_output_ppm = os.path.join(TESTDATA_DIR, "_ffmpeg_output.ppm")

    print("\nDecoding with cmj_decode...")
    cmj_ppm = decode_with_cmj(jpeg_path, cmj_output_dir)
    if not cmj_ppm:
        print("ERROR: cmj_decode failed")
        sys.exit(1)

    print("Decoding with FFmpeg...")
    if not decode_with_ffmpeg(ffmpeg_path, jpeg_path, ff_output_ppm):
        print("ERROR: FFmpeg decode failed")
        sys.exit(1)

    # Read both PPM files
    print("Comparing outputs...")
    cmj_w, cmj_h, cmj_pixels = read_ppm(cmj_ppm)
    ff_w, ff_h, ff_pixels = read_ppm(ff_output_ppm)

    print(f"  cmj_decode: {cmj_w}x{cmj_h}")
    print(f"  FFmpeg:     {ff_w}x{ff_h}")

    if cmj_w != ff_w or cmj_h != ff_h:
        print("ERROR: Dimension mismatch!")
        sys.exit(1)

    # Compute metrics
    print("Computing PSNR...")
    psnr = compute_psnr(cmj_pixels, ff_pixels, cmj_w, cmj_h)
    print(f"  PSNR: {psnr:.2f} dB")

    print("Computing SSIM...")
    ssim = compute_ssim_simple(cmj_pixels, ff_pixels, cmj_w, cmj_h)
    print(f"  SSIM: {ssim:.4f}")

    diff_stats = compute_pixel_diff_stats(cmj_pixels, ff_pixels)
    print(f"  Max pixel diff: {diff_stats['max_diff']}")
    print(f"  Mean pixel diff: {diff_stats['mean_diff']:.3f}")
    print(f"  Identical channels: {diff_stats['identical_channels_pct']:.1f}%")

    # Generate comparison image
    os.makedirs(DOCS_DIR, exist_ok=True)
    comparison_path = os.path.join(DOCS_DIR, "quality_comparison.png")
    print("\nGenerating comparison image...")
    generate_comparison_image(cmj_pixels, ff_pixels, cmj_w, cmj_h, psnr, ssim, comparison_path)

    # Save results JSON
    results = {
        "input": os.path.basename(jpeg_path),
        "dimensions": f"{cmj_w}x{cmj_h}",
        "psnr_db": round(psnr, 2) if psnr != float("inf") else "inf",
        "ssim": round(ssim, 4),
        "max_pixel_diff": diff_stats["max_diff"],
        "mean_pixel_diff": round(diff_stats["mean_diff"], 3),
        "identical_channels_pct": round(diff_stats["identical_channels_pct"], 1),
    }

    json_path = os.path.join(PROJECT_DIR, "quality_results.json")
    with open(json_path, "w") as f:
        json.dump(results, f, indent=2)
    print(f"\nResults saved to: {json_path}")

    # Cleanup temp files
    try:
        os.remove(ff_output_ppm)
        import shutil
        shutil.rmtree(cmj_output_dir, ignore_errors=True)
    except Exception:
        pass

    print(f"\n{'='*50}")
    print(f"QUALITY SUMMARY")
    print(f"{'='*50}")
    print(f"  PSNR:  {psnr:.2f} dB {'(excellent)' if psnr > 40 else '(good)' if psnr > 30 else '(fair)'}")
    print(f"  SSIM:  {ssim:.4f} {'(excellent)' if ssim > 0.99 else '(good)' if ssim > 0.95 else '(fair)'}")
    print(f"  Max diff: {diff_stats['max_diff']} levels")


if __name__ == "__main__":
    main()
