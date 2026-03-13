#!/usr/bin/env python3
"""Generate benchmark charts from benchmark_results.json."""

import json
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
DOCS_DIR = os.path.join(PROJECT_DIR, "docs")

def main():
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    results_path = os.path.join(PROJECT_DIR, "benchmark_results.json")
    with open(results_path) as f:
        results = json.load(f)

    os.makedirs(DOCS_DIR, exist_ok=True)

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
        "axes.edgecolor": "#444466",
        "axes.labelcolor": "#e2e2e2",
        "text.color": "#e2e2e2",
        "xtick.color": "#c0c0c0",
        "ytick.color": "#c0c0c0",
        "grid.color": "#2a2a4a",
        "font.family": "sans-serif",
        "font.size": 12,
    })

    resolutions = results["resolutions"]

    # ── Chart 1: FPS Comparison bar chart (1080p) ──
    fig, ax = plt.subplots(figsize=(12, 6), dpi=300)

    best_res = resolutions[-1]  # 1080p

    categories = []
    fps_values = []
    bar_colors = []

    for mode, label, color in [
        ("ffmpeg", "FFmpeg", colors["ffmpeg"]),
        ("simd", "claude-mjpeg\n(SIMD)", colors["simd"]),
        ("optimized", "claude-mjpeg\n(optimized)", colors["optimized"]),
        ("naive", "claude-mjpeg\n(naive)", colors["naive"]),
    ]:
        d = best_res["data"].get(mode)
        if d:
            categories.append(label)
            fps_values.append(d["fps"])
            bar_colors.append(color)

    bars = ax.bar(categories, fps_values, color=bar_colors, width=0.55,
                  edgecolor="white", linewidth=0.5, zorder=3)

    for bar, val in zip(bars, fps_values):
        if val >= 1:
            label = f"{val:.1f}"
        else:
            label = f"{val:.2f}"
        ax.text(bar.get_x() + bar.get_width() / 2,
                bar.get_height() + max(fps_values) * 0.03,
                label, ha="center", va="bottom",
                fontweight="bold", fontsize=12, color="#e2e2e2")

    ax.set_ylabel("Frames Per Second (FPS)", fontsize=14, fontweight="bold")
    ax.set_title("Decode Performance \u2014 1080p MJPEG (1920\u00d71080)",
                 fontsize=16, fontweight="bold", pad=20)
    ax.grid(axis="y", alpha=0.3, zorder=0)
    ax.set_axisbelow(True)

    # Add percentage annotation
    ff_fps = best_res["data"]["ffmpeg"]["fps"]
    simd_fps = best_res["data"]["simd"]["fps"]
    pct = simd_fps / ff_fps * 100
    ax.text(0.98, 0.95,
            f"claude-mjpeg achieves {pct:.0f}% of FFmpeg speed\n(single-threaded, pure C, zero dependencies)",
            transform=ax.transAxes, ha="right", va="top",
            fontsize=10, color="#888888", fontstyle="italic")

    plt.tight_layout()
    path = os.path.join(DOCS_DIR, "benchmark_fps.png")
    plt.savefig(path, dpi=300, bbox_inches="tight")
    plt.close()
    print(f"Saved: {path}")

    # ── Chart 2: Grouped bar chart across resolutions ──
    fig, ax = plt.subplots(figsize=(12, 6), dpi=300)

    res_labels = [r["label"] for r in resolutions]
    x = np.arange(len(res_labels))
    n_modes = 4
    width = 0.18
    offset = 0

    for mode, label, color in [
        ("ffmpeg", "FFmpeg", colors["ffmpeg"]),
        ("simd", "SIMD", colors["simd"]),
        ("optimized", "Optimized", colors["optimized"]),
        ("naive", "Naive", colors["naive"]),
    ]:
        fps_list = [r["data"].get(mode, {}).get("fps", 0) for r in resolutions]
        bars = ax.bar(x + offset * width, fps_list, width, label=label,
                     color=color, edgecolor="white", linewidth=0.5, zorder=3)

        for bar, val in zip(bars, fps_list):
            if val > 0:
                label_text = f"{val:.0f}" if val >= 1 else f"{val:.1f}"
                ax.text(bar.get_x() + bar.get_width() / 2,
                        bar.get_height() + 2,
                        label_text, ha="center", va="bottom",
                        fontsize=7, color="#c0c0c0", fontweight="bold")
        offset += 1

    ax.set_xlabel("Resolution", fontsize=14, fontweight="bold")
    ax.set_ylabel("Frames Per Second (FPS)", fontsize=14, fontweight="bold")
    ax.set_title("Performance Across Resolutions",
                 fontsize=16, fontweight="bold", pad=20)
    ax.set_xticks(x + width * (n_modes - 1) / 2)
    ax.set_xticklabels(res_labels, fontsize=13)
    ax.legend(loc="upper right", framealpha=0.85, facecolor="#1a1a2e",
              edgecolor="#444466", fontsize=11)
    ax.grid(axis="y", alpha=0.3, zorder=0)
    ax.set_axisbelow(True)

    plt.tight_layout()
    path = os.path.join(DOCS_DIR, "benchmark_resolution.png")
    plt.savefig(path, dpi=300, bbox_inches="tight")
    plt.close()
    print(f"Saved: {path}")

    # ── Chart 3: Optimization speedup ──
    fig, ax = plt.subplots(figsize=(12, 6), dpi=300)

    # Use 1080p data
    data = resolutions[-1]["data"]
    naive_fps = data.get("naive", {}).get("fps", 1)
    if naive_fps <= 0:
        naive_fps = 0.01

    labels = []
    speedups = []
    bar_cols = []

    for mode, label, color in [
        ("naive", "Naive\n(baseline)", colors["naive"]),
        ("optimized", "AAN IDCT +\nLookup Huffman", colors["optimized"]),
        ("simd", "Optimized +\nSSE2 Color", colors["simd"]),
        ("ffmpeg", "FFmpeg\n(reference)", colors["ffmpeg"]),
    ]:
        d = data.get(mode)
        if d:
            labels.append(label)
            speedups.append(d["fps"] / naive_fps)
            bar_cols.append(color)

    bars = ax.bar(labels, speedups, color=bar_cols, width=0.55,
                  edgecolor="white", linewidth=0.5, zorder=3)

    for bar, val in zip(bars, speedups):
        ax.text(bar.get_x() + bar.get_width() / 2,
                bar.get_height() + max(speedups) * 0.03,
                f"{val:.0f}x", ha="center", va="bottom",
                fontweight="bold", fontsize=13, color="#e2e2e2")

    ax.set_ylabel("Speedup (vs Naive)", fontsize=14, fontweight="bold")
    ax.set_title("Optimization Speedup \u2014 1080p MJPEG",
                 fontsize=16, fontweight="bold", pad=20)
    ax.grid(axis="y", alpha=0.3, zorder=0)
    ax.set_axisbelow(True)
    ax.axhline(y=1.0, color="#e2e2e2", linestyle="--", alpha=0.2, zorder=1)

    # Annotation
    opt_speedup = data["optimized"]["fps"] / naive_fps
    ax.text(0.98, 0.95,
            f"AAN IDCT + lookup Huffman = {opt_speedup:.0f}x speedup over naive",
            transform=ax.transAxes, ha="right", va="top",
            fontsize=10, color="#888888", fontstyle="italic")

    plt.tight_layout()
    path = os.path.join(DOCS_DIR, "benchmark_breakdown.png")
    plt.savefig(path, dpi=300, bbox_inches="tight")
    plt.close()
    print(f"Saved: {path}")

    print("\nAll charts generated!")


if __name__ == "__main__":
    main()
