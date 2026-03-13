#!/usr/bin/env python3
"""
Comprehensive benchmark script for claude-mjpeg decoder.

Runs cmj_bench in all modes (naive, optimized, SIMD), compares with FFmpeg,
collects results into JSON, and generates comparison charts.

Usage:
    python scripts/benchmark.py [--ffmpeg PATH] [--iterations N]
"""

import subprocess
import json
import os
import sys
import re
import platform
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
BUILD_DIR = os.path.join(PROJECT_DIR, "build")
TESTDATA_DIR = os.path.join(PROJECT_DIR, "testdata")
DOCS_DIR = os.path.join(PROJECT_DIR, "docs")

# Find executables
CMJ_BENCH = os.path.join(BUILD_DIR, "cmj_bench.exe" if platform.system() == "Windows" else "cmj_bench")

# Test files in order of resolution
TEST_FILES = [
    ("480p", "test_480p_q5.avi"),
    ("720p", "test_720p_q2.avi"),
    ("1080p", "test_1080p_q2.avi"),
]

# Also test single JPEG
JPEG_FILE = ("1080p JPEG", "reference_frame.jpg")


def find_ffmpeg():
    """Find FFmpeg executable."""
    # Check common locations
    candidates = [
        "ffmpeg",
        r"C:\Program Files\ShareX\ffmpeg.exe",
        r"C:\ffmpeg\bin\ffmpeg.exe",
        "/usr/bin/ffmpeg",
        "/usr/local/bin/ffmpeg",
    ]
    for c in candidates:
        try:
            subprocess.run([c, "-version"], capture_output=True, timeout=5)
            return c
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
    return None


def run_cmj_bench(input_file, iterations, mode="simd"):
    """Run cmj_bench and parse results."""
    args = [CMJ_BENCH, input_file, str(iterations), "--no-ffmpeg"]

    if mode == "naive":
        args.append("--naive")
    elif mode == "optimized":
        args.append("--no-simd")
    # else: simd mode (default)

    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=600)
        output = result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        print(f"  WARNING: cmj_bench timed out for {input_file} ({mode})")
        return None
    except FileNotFoundError:
        print(f"  ERROR: cmj_bench not found at {CMJ_BENCH}")
        return None

    # Parse the table output for FPS and avg_ms
    fps_match = re.search(r'FPS\s*\|\s*([\d.]+)', output)
    avg_match = re.search(r'Avg time/frame\s*\|\s*([\d.]+)\s*ms', output)
    total_match = re.search(r'Total time\s*\|\s*([\d.]+)\s*ms', output)
    mp_match = re.search(r'Megapixels/sec\s*\|\s*([\d.]+)', output)
    mem_match = re.search(r'Peak memory.*\|\s*(\d+)', output)

    if fps_match:
        return {
            "fps": float(fps_match.group(1)),
            "avg_ms": float(avg_match.group(1)) if avg_match else 0,
            "total_ms": float(total_match.group(1)) if total_match else 0,
            "mp_per_sec": float(mp_match.group(1)) if mp_match else 0,
            "peak_memory_kb": int(mem_match.group(1)) if mem_match else 0,
            "mode": mode,
        }
    else:
        print(f"  WARNING: Could not parse cmj_bench output for {mode}:")
        print(f"  {output[:500]}")
        return None


def run_ffmpeg_bench(ffmpeg_path, input_file, iterations=1):
    """Benchmark FFmpeg decoding speed."""
    null_dev = "NUL" if platform.system() == "Windows" else "/dev/null"

    total_ms = 0
    frame_count = 0

    for _ in range(iterations):
        start = time.perf_counter()
        try:
            result = subprocess.run(
                [ffmpeg_path, "-hide_banner", "-benchmark", "-i", input_file,
                 "-f", "rawvideo", "-pix_fmt", "rgb24", "-an", "-"],
                capture_output=True, timeout=300
            )
            elapsed = (time.perf_counter() - start) * 1000  # ms

            # Try to get frame count from stderr
            stderr = result.stderr.decode("utf-8", errors="replace")
            fc_match = re.search(r'frame=\s*(\d+)', stderr)
            if fc_match:
                frame_count = int(fc_match.group(1))

            total_ms += elapsed
        except (subprocess.TimeoutExpired, FileNotFoundError):
            return None

    if frame_count == 0:
        frame_count = 1

    avg_ms = total_ms / (frame_count * iterations)
    fps = 1000.0 / avg_ms if avg_ms > 0 else 0

    return {
        "fps": fps,
        "avg_ms": avg_ms,
        "total_ms": total_ms,
        "frame_count": frame_count,
    }


def generate_charts(results):
    """Generate benchmark comparison charts."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        print("WARNING: matplotlib/numpy not installed. Skipping chart generation.")
        print("  Install with: pip install matplotlib numpy")
        return

    os.makedirs(DOCS_DIR, exist_ok=True)

    # Color scheme from the prompt
    colors = {
        "ffmpeg": "#FF6B6B",
        "naive": "#4ECDC4",
        "optimized": "#45B7D1",
        "simd": "#96E6A1",
    }

    # Dark theme
    plt.rcParams.update({
        "figure.facecolor": "#1a1a2e",
        "axes.facecolor": "#16213e",
        "axes.edgecolor": "#e2e2e2",
        "axes.labelcolor": "#e2e2e2",
        "text.color": "#e2e2e2",
        "xtick.color": "#e2e2e2",
        "ytick.color": "#e2e2e2",
        "grid.color": "#2a2a4a",
        "font.family": "sans-serif",
        "font.size": 12,
    })

    # --- Chart 1: FPS Comparison (bar chart) ---
    fig, ax = plt.subplots(figsize=(12, 6), dpi=300)

    # Collect data for the highest resolution available
    resolutions = [r for r in results.get("resolutions", []) if r.get("data")]
    if not resolutions:
        print("WARNING: No resolution data for charts")
        return

    best_res = resolutions[-1]  # highest resolution
    res_label = best_res["label"]

    categories = []
    fps_values = []
    bar_colors = []

    if best_res["data"].get("ffmpeg"):
        categories.append("FFmpeg")
        fps_values.append(best_res["data"]["ffmpeg"]["fps"])
        bar_colors.append(colors["ffmpeg"])

    for mode in ["simd", "optimized", "naive"]:
        if best_res["data"].get(mode):
            label = {"simd": "claude-mjpeg\n(SIMD)", "optimized": "claude-mjpeg\n(optimized)", "naive": "claude-mjpeg\n(naive)"}[mode]
            categories.append(label)
            fps_values.append(best_res["data"][mode]["fps"])
            bar_colors.append(colors[mode])

    bars = ax.bar(categories, fps_values, color=bar_colors, width=0.6, edgecolor="white", linewidth=0.5)

    # Add value labels on bars
    for bar, val in zip(bars, fps_values):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + max(fps_values) * 0.02,
                f"{val:.0f}", ha="center", va="bottom", fontweight="bold", fontsize=11, color="#e2e2e2")

    ax.set_ylabel("Frames Per Second (FPS)", fontsize=13)
    ax.set_title(f"Decode Performance — {res_label}", fontsize=16, fontweight="bold", pad=15)
    ax.grid(axis="y", alpha=0.3)
    ax.set_axisbelow(True)

    plt.tight_layout()
    chart_path = os.path.join(DOCS_DIR, "benchmark_fps.png")
    plt.savefig(chart_path, dpi=300, bbox_inches="tight")
    plt.close()
    print(f"  Saved: {chart_path}")

    # --- Chart 2: Grouped bar chart across resolutions ---
    fig, ax = plt.subplots(figsize=(12, 6), dpi=300)

    res_labels = [r["label"] for r in resolutions]
    x = np.arange(len(res_labels))
    width = 0.2
    offset = 0

    modes_present = []
    for mode_key, mode_label in [("ffmpeg", "FFmpeg"), ("simd", "SIMD"), ("optimized", "Optimized"), ("naive", "Naive")]:
        fps_list = []
        has_data = False
        for r in resolutions:
            d = r["data"].get(mode_key)
            if d:
                fps_list.append(d["fps"])
                has_data = True
            else:
                fps_list.append(0)
        if has_data:
            bars = ax.bar(x + offset * width, fps_list, width, label=mode_label,
                         color=colors[mode_key], edgecolor="white", linewidth=0.5)
            modes_present.append(mode_key)
            offset += 1

    ax.set_xlabel("Resolution", fontsize=13)
    ax.set_ylabel("Frames Per Second (FPS)", fontsize=13)
    ax.set_title("Performance Across Resolutions", fontsize=16, fontweight="bold", pad=15)
    ax.set_xticks(x + width * (len(modes_present) - 1) / 2)
    ax.set_xticklabels(res_labels)
    ax.legend(loc="upper right", framealpha=0.8, facecolor="#1a1a2e", edgecolor="#e2e2e2")
    ax.grid(axis="y", alpha=0.3)
    ax.set_axisbelow(True)

    plt.tight_layout()
    chart_path = os.path.join(DOCS_DIR, "benchmark_resolution.png")
    plt.savefig(chart_path, dpi=300, bbox_inches="tight")
    plt.close()
    print(f"  Saved: {chart_path}")

    # --- Chart 3: Speedup factor chart ---
    fig, ax = plt.subplots(figsize=(12, 6), dpi=300)

    # Show speedup of each mode relative to naive
    for r in resolutions:
        naive_fps = r["data"].get("naive", {}).get("fps", 1)
        if naive_fps <= 0:
            naive_fps = 1

        labels = []
        speedups = []
        bar_cols = []

        for mode_key, mode_label in [("naive", "Naive"), ("optimized", "Optimized"), ("simd", "SIMD"), ("ffmpeg", "FFmpeg")]:
            d = r["data"].get(mode_key)
            if d:
                labels.append(mode_label)
                speedups.append(d["fps"] / naive_fps)
                bar_cols.append(colors[mode_key])

    # Use last resolution (highest) for the speedup chart
    if labels:
        bars = ax.bar(labels, speedups, color=bar_cols, width=0.6, edgecolor="white", linewidth=0.5)
        for bar, val in zip(bars, speedups):
            ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + max(speedups) * 0.02,
                    f"{val:.1f}x", ha="center", va="bottom", fontweight="bold", fontsize=11, color="#e2e2e2")

        ax.set_ylabel("Speedup (vs Naive)", fontsize=13)
        ax.set_title(f"Optimization Speedup — {resolutions[-1]['label']}", fontsize=16, fontweight="bold", pad=15)
        ax.grid(axis="y", alpha=0.3)
        ax.set_axisbelow(True)
        ax.axhline(y=1.0, color="#e2e2e2", linestyle="--", alpha=0.3)

    plt.tight_layout()
    chart_path = os.path.join(DOCS_DIR, "benchmark_breakdown.png")
    plt.savefig(chart_path, dpi=300, bbox_inches="tight")
    plt.close()
    print(f"  Saved: {chart_path}")


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Benchmark claude-mjpeg decoder")
    parser.add_argument("--ffmpeg", default=None, help="Path to FFmpeg executable")
    parser.add_argument("--iterations", type=int, default=3, help="Benchmark iterations (default: 3)")
    parser.add_argument("--avi-iterations", type=int, default=1, help="AVI benchmark iterations (default: 1)")
    parser.add_argument("--jpeg-iterations", type=int, default=50, help="JPEG benchmark iterations (default: 50)")
    args = parser.parse_args()

    ffmpeg_path = args.ffmpeg or find_ffmpeg()
    if ffmpeg_path:
        print(f"FFmpeg found: {ffmpeg_path}")
    else:
        print("WARNING: FFmpeg not found. FFmpeg benchmarks will be skipped.")

    if not os.path.exists(CMJ_BENCH):
        print(f"ERROR: cmj_bench not found at {CMJ_BENCH}")
        print("Build the project first: mkdir build && cd build && cmake .. && cmake --build .")
        sys.exit(1)

    all_results = {
        "resolutions": [],
        "jpeg": None,
        "system_info": {
            "platform": platform.platform(),
            "processor": platform.processor(),
            "python": platform.python_version(),
        }
    }

    # Benchmark AVI files at each resolution
    for label, filename in TEST_FILES:
        filepath = os.path.join(TESTDATA_DIR, filename)
        if not os.path.exists(filepath):
            print(f"SKIP: {filepath} not found")
            continue

        print(f"\n{'='*60}")
        print(f"Benchmarking: {label} ({filename})")
        print(f"{'='*60}")

        res_data = {"label": label, "file": filename, "data": {}}
        iters = args.avi_iterations

        # Run each mode
        for mode in ["naive", "optimized", "simd"]:
            print(f"  Running claude-mjpeg ({mode})...")
            result = run_cmj_bench(filepath, iters, mode)
            if result:
                res_data["data"][mode] = result
                print(f"    FPS: {result['fps']:.1f}")

        # FFmpeg comparison
        if ffmpeg_path:
            print(f"  Running FFmpeg...")
            ff_result = run_ffmpeg_bench(ffmpeg_path, filepath, iters)
            if ff_result:
                res_data["data"]["ffmpeg"] = ff_result
                print(f"    FPS: {ff_result['fps']:.1f}")

        all_results["resolutions"].append(res_data)

    # Benchmark single JPEG
    jpeg_path = os.path.join(TESTDATA_DIR, JPEG_FILE[1])
    if os.path.exists(jpeg_path):
        print(f"\n{'='*60}")
        print(f"Benchmarking: {JPEG_FILE[0]} ({JPEG_FILE[1]})")
        print(f"{'='*60}")

        jpeg_data = {"label": JPEG_FILE[0], "file": JPEG_FILE[1], "data": {}}
        iters = args.jpeg_iterations

        for mode in ["naive", "optimized", "simd"]:
            print(f"  Running claude-mjpeg ({mode})...")
            result = run_cmj_bench(jpeg_path, iters, mode)
            if result:
                jpeg_data["data"][mode] = result
                print(f"    FPS: {result['fps']:.1f}")

        all_results["jpeg"] = jpeg_data

    # Save results JSON
    os.makedirs(DOCS_DIR, exist_ok=True)
    json_path = os.path.join(PROJECT_DIR, "benchmark_results.json")
    with open(json_path, "w") as f:
        json.dump(all_results, f, indent=2)
    print(f"\nResults saved to: {json_path}")

    # Generate charts
    print("\nGenerating charts...")
    generate_charts(all_results)

    # Print summary table
    print(f"\n{'='*80}")
    print("SUMMARY")
    print(f"{'='*80}")
    print(f"{'Resolution':<12} {'FFmpeg':>12} {'SIMD':>12} {'Optimized':>12} {'Naive':>12} {'vs FFmpeg':>10}")
    print(f"{'-'*12:<12} {'-'*12:>12} {'-'*12:>12} {'-'*12:>12} {'-'*12:>12} {'-'*10:>10}")

    for r in all_results["resolutions"]:
        ff_fps = r["data"].get("ffmpeg", {}).get("fps", 0)
        simd_fps = r["data"].get("simd", {}).get("fps", 0)
        opt_fps = r["data"].get("optimized", {}).get("fps", 0)
        naive_fps = r["data"].get("naive", {}).get("fps", 0)
        vs_ff = f"{simd_fps/ff_fps*100:.0f}%" if ff_fps > 0 and simd_fps > 0 else "N/A"

        print(f"{r['label']:<12} {ff_fps:>10.0f}  {simd_fps:>10.0f}  {opt_fps:>10.0f}  {naive_fps:>10.0f}  {vs_ff:>10}")


if __name__ == "__main__":
    main()
