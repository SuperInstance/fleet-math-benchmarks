#!/usr/bin/env python3
"""
bench_plot.py — Generate ASCII plots and tables from benchmark results.

Reads the raw output of bench_hpc and produces:
1. A bar chart of SIMD speedups
2. A formatted summary table
3. Throughput in tiles/second and holonomies/second

Usage:
    ./bench_hpc | python3 bench_plot.py
    python3 bench_plot.py results/benchmark_output.txt
"""

import sys
import re
import os


def parse_results(lines):
    """Parse benchmark output into a list of result dicts."""
    results = []
    for line in lines:
        line = line.strip()
        m = re.match(
            r"^(.+?)\s*\|\s*([\d.]+)\s*ns\s*\|\s*([\d.]+)\s*ns\s*\|\s*([\d.]+)x",
            line,
        )
        if m:
            results.append(
                {
                    "operation": m.group(1).strip(),
                    "scalar_ns": float(m.group(2)),
                    "simd_ns": float(m.group(3)),
                    "speedup": float(m.group(4)),
                }
            )

    # Also extract bandwidth lines
    bandwidths = {}
    for line in lines:
        line = line.strip()
        m = re.search(r"bandwidth:\s*([\d.]+)\s*GB/s", line)
        if m:
            bw = float(m.group(1))
            if "Memory" in line:
                bandwidths["DRAM"] = bw
            elif "L2" in line:
                bandwidths["L2"] = bw
            elif "L1" in line:
                bandwidths["L1"] = bw

    if bandwidths:
        results.append(
            {
                "operation": "bandwidth",
                "bandwidths": bandwidths,
            }
        )

    return results


def print_table(results):
    """Print formatted summary table."""
    ops = [r for r in results if "bandwidth" not in r.get("operation", "")]
    if not ops:
        return

    print()
    print("=" * 72)
    print("  FLEET MATH BENCHMARKS — SUMMARY TABLE")
    print("=" * 72)
    print(f"  {'Operation':<28s} {'Scalar (ns)':>12s} {'SIMD (ns)':>12s} {'Speedup':>8s}")
    print("  " + "-" * 60)
    for r in ops:
        print(
            f"  {r['operation']:<28s} {r['scalar_ns']:>10.1f} ns {r['simd_ns']:>10.1f} ns {r['speedup']:>6.1f}x"
        )
    print()

    # Print throughput
    print("-" * 72)
    print("  THROUGHPUT")
    print("-" * 72)
    for r in ops:
        name = r["operation"]
        scalar_ns = r["scalar_ns"]
        simd_ns = r["simd_ns"]
        speedup = r["speedup"]

        # Compute ops/second
        if "Batch" in name:
            # For batches, convert total batch time to tiles/second
            # ns total → tiles/second = 1e9 / ns * n_tiles
            # Actually we need n_tiles. Parse it.
            m = re.search(r"(\d+)", name)
            n_tiles = int(m.group(1)) if m else 1
            scalar_tps = 1e9 / scalar_ns * n_tiles
            simd_tps = 1e9 / simd_ns * n_tiles
            unit = "tiles/s"
        elif "check" in name.lower() or "Tile" in name:
            scalar_tps = 1e9 / scalar_ns
            simd_tps = 1e9 / simd_ns
            unit = "tiles/s"
        else:
            scalar_tps = 1e9 / scalar_ns
            simd_tps = 1e9 / simd_ns
            unit = "ops/s"

        print(f"  {name:<28s} {scalar_tps:>12.1e} {unit:<8s} | SIMD: {simd_tps:>12.1e} {unit:<8s}")

    print()

    # Print bandwidth
    for r in results:
        if "bandwidth" in r.get("operation", ""):
            bws = r["bandwidths"]
            print("-" * 72)
            print("  MEMORY BANDWIDTH")
            print("-" * 72)
            for level in ["L1", "L2", "DRAM"]:
                if level in bws:
                    print(f"  {level:>4s} cache:    {bws[level]:>6.1f} GB/s")
            print()


def plot_speedup_bar(results):
    """Print an ASCII bar chart of speedups."""
    ops = [r for r in results if "bandwidth" not in r.get("operation", "")]
    if not ops:
        return

    max_speedup = max(r["speedup"] for r in ops)
    bar_width = 50
    scale = bar_width / max_speedup if max_speedup > 0 else 1

    print("-" * 72)
    print("  SPEEDUP (SIMD / Scalar)")
    print("-" * 72)

    for r in ops:
        name = r["operation"]
        speedup = r["speedup"]
        bar_len = int(speedup * scale)
        bar = "█" * bar_len
        if bar_len > 0 and len(bar) > 0:
            bar = "█" * bar_len
        else:
            bar = "░"
        print(f"  {name:<28s} │{bar:<{bar_width+2}s} {speedup:.1f}x")

    print(f"  {'':<28s} └{'─' * (bar_width+1)}>")
    print(f"  {'':<28s}  0{' ' * (bar_width//2 - 1)}{max_speedup/2:.0f}x{' ' * (bar_width//2 - 3)}{max_speedup:.0f}x")
    print()


def main():
    # Read input
    if len(sys.argv) > 1:
        with open(sys.argv[1]) as f:
            lines = f.readlines()
    else:
        lines = sys.stdin.readlines()

    results = parse_results(lines)
    if not results:
        print("No benchmark data found. Pipe the output of bench_hpc:")
        print("  ./bench_hpc | python3 bench_plot.py")
        print("  python3 bench_plot.py results/benchmark_output.txt")
        sys.exit(1)

    # Print plots and tables
    plot_speedup_bar(results)
    print_table(results)


if __name__ == "__main__":
    main()
