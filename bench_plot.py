#!/usr/bin/env python3
"""
bench_plot.py - Generate comparison bar charts from benchmark JSON output.

Usage: python bench_plot.py bench_results.json [output.png]
"""

import json
import sys

def plot_results(json_path, output_path="benchmark_chart.png"):
    with open(json_path, 'r') as f:
        data = json.load(f)

    try:
        import matplotlib.pyplot as plt
        import matplotlib
        matplotlib.use('Agg')
    except ImportError:
        print("matplotlib not installed. Install with: pip install matplotlib")
        print("\nRaw results:")
        print(json.dumps(data, indent=2))
        return

    decoders = []
    fps_values = []
    avg_ms_values = []
    colors = []

    # claude-mjpeg results
    cmj = data.get("claude_mjpeg", {})
    if cmj:
        decoders.append("claude-mjpeg\n(naive)")
        fps_values.append(cmj.get("fps", 0))
        avg_ms_values.append(cmj.get("avg_ms", 0))
        colors.append("#4A90D9")

    # FFmpeg results
    ff = data.get("ffmpeg", {})
    if ff:
        decoders.append("FFmpeg")
        fps_values.append(ff.get("fps", 0))
        avg_ms_values.append(ff.get("avg_ms", 0))
        colors.append("#E74C3C")

    if not decoders:
        print("No results to plot.")
        return

    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    fig.suptitle(f'MJPEG Decoder Benchmark - {data.get("width", "?")}x{data.get("height", "?")}',
                 fontsize=14, fontweight='bold')

    # FPS chart
    ax1 = axes[0]
    bars1 = ax1.bar(decoders, fps_values, color=colors, edgecolor='black', linewidth=0.5)
    ax1.set_ylabel('Frames per Second')
    ax1.set_title('Decode Speed (FPS)')
    for bar, val in zip(bars1, fps_values):
        ax1.text(bar.get_x() + bar.get_width()/2, bar.get_height() + max(fps_values)*0.02,
                f'{val:.1f}', ha='center', va='bottom', fontweight='bold')

    # Avg ms chart
    ax2 = axes[1]
    bars2 = ax2.bar(decoders, avg_ms_values, color=colors, edgecolor='black', linewidth=0.5)
    ax2.set_ylabel('Milliseconds per Frame')
    ax2.set_title('Avg Decode Time (ms)')
    for bar, val in zip(bars2, avg_ms_values):
        ax2.text(bar.get_x() + bar.get_width()/2, bar.get_height() + max(avg_ms_values)*0.02,
                f'{val:.3f}', ha='center', va='bottom', fontweight='bold')

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"Chart saved to: {output_path}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <bench_results.json> [output.png]")
        sys.exit(1)

    json_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else "benchmark_chart.png"
    plot_results(json_path, output_path)
