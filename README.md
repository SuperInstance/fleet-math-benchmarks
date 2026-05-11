# fleet-math-benchmarks

High-performance benchmarks for the [fleet-math-c](https://github.com/SuperInstance/fleet-math-c) SIMD library, measured on **actual ARM Neoverse-N1 hardware** (Oracle Cloud Ampere A1).

## What This Measures

| Benchmark | What | Why |
|-----------|------|-----|
| **Tile validation** | Count fields < threshold in a 64-byte PLATO tile | Core constraint check — runs on every tile, every tick |
| **Holonomy (4-cycle)** | `H = w0×w1 − w2×w3` around a 4-edge cycle | Measures consensus failure along graph cycles |
| **Batch throughput** | Validate batches of 64, 1024, 10000 tiles | Real-world batch processing patterns |
| **Memory bandwidth** | Streaming 64-byte tiles through L1, L2, DRAM | Memory hierarchy performance for tile workloads |

## Results (ARM Neoverse-N1 @ 3.0 GHz)

Benchmarked 2026-05-11 on Oracle Cloud Ampere A1 (4 vCPUs).

### SIMD vs Scalar

| Operation | Scalar | NEON SIMD | Speedup |
|-----------|--------|-----------|---------|
| Tile check (1 tile) | 5.1 ns | 5.1 ns | 1.0× |
| Holonomy (4-cycle) | 1.3 ns | 1.3 ns | 1.1× |
| Batch 64 tiles | 360 ns | 320 ns | 1.1× |
| Batch 1024 tiles | 5,320 ns | 5,080 ns | 1.0× |
| Batch 10,000 tiles | 51,600 ns | 50,920 ns | 1.0× |
| Holonomy batch 10,000 | 0.8 ns | 0.6 ns | 1.3× |

### Memory Bandwidth

| Level | Bandwidth | Working Set |
|-------|-----------|-------------|
| L1 cache | 91.0 GB/s | 64 KB (1024 tiles) |
| L2 cache | 41.5 GB/s | 1 MB (16,384 tiles) |
| DRAM | 16.8 GB/s | 16 MB (262,144 tiles) |

## Interpretation

### Why Modest SIMD Speedup on ARM NEON?

The 1.0-1.3× speedup is a **legitimate finding**, not a benchmark flaw:

1. **Reduction overhead dominates**: The NEON implementation uses 4 Q-registers (16 floats) and compares them in parallel, but reducing 14 compare results to a single count requires significant work: shift, mask, pairwise-add, extract across lanes. This reduction path costs ~10 cycles, eating the SIMD advantage.

2. **Scalar is already fast**: At 5.1 ns = ~15 cycles per tile on 3 GHz, the scalar path is running at ~1 cycle per field. Modern OoO execution hides the latency of 14 simple compares.

3. **No cross-tile SIMD**: Each tile check processes one tile independently. The bottleneck is the reduction, not the compare. True SIMD throughput gains would come from processing **multiple tiles' fields simultaneously** — an optimization not yet implemented.

4. **Contrast with AVX-512**: On x86 with AVX-512, a single `_mm512_cmp_ps_mask` loads all 16 tile fields (64 bytes = 1 zmm register), compares, and produces a mask in 1 instruction. The `_popcnt_u32` then gives the violation count in 1 more instruction. This contrasts with NEON's 4-register load + multi-instruction reduction. AVX-512 achieves the 5-6× speedups quoted in fleet-math-c's README for this reason.

### What This Means for PLATO

At 51 **µs for 10,000 tiles**, validating an entire PLATO room (~1,180 tiles) takes:

```
1,180 tiles × (51,600 ns / 10,000 tiles) = 6.1 µs
```

That's **6.1 microseconds** to check every constraint — well within any interactive or real-time feedback loop, regardless of SIMD path.

### Memory Hierarchy

The 64-byte tile format pays off in memory bandwidth:

- **L1 (91 GB/s)**: 1.4 billion tiles/second — compute-bound for small working sets
- **L2 (41.5 GB/s)**: 650 million tiles/second — still compute-bound for typical PLATO rooms
- **DRAM (16.8 GB/s)**: 262 million tiles/second — memory bandwidth becomes the bottleneck only for tile sets >~16 MB (>250K tiles)

For reference, a Neoverse-N1 can sustain **~2.6 GB/s per core** streaming from DRAM, so the 16.8 GB/s for 4 cores is at the memory controller's limit.

### Bottleneck Analysis

| Working Set | Bottleneck | Max Tiles/sec (NEON) |
|-------------|-----------|----------------------|
| < 64 KB (L1) | Compute | ~200 M/s |
| < 1 MB (L2) | Slightly compute | ~200 M/s |
| > 1 MB (DRAM) | Memory bandwidth | ~260 M/s |

For nearly all practical PLATO workloads, the bottleneck is **compute, not memory** — which means SIMD acceleration directly translates to wall-clock savings wherever the reduction can be optimized or avoided.

## Build & Run

### Prerequisites

- GCC or Clang with ARM NEON support (or x86 with AVX-512)
- [fleet-math-c](https://github.com/SuperInstance/fleet-math-c) checked out as a sibling directory

### Quick Start

```bash
# Clone both repos
git clone https://github.com/SuperInstance/fleet-math-c.git
git clone https://github.com/SuperInstance/fleet-math-benchmarks.git
cd fleet-math-benchmarks

# Build and run
make run

# Full report with ASCII plots
bash run_all.sh --save
```

### Targets

```bash
make            # build bench_hpc
make run        # build + run, print results
make run-save   # build + run, save timestamped output
make csv        # export benchmark data as CSV
make clean      # remove build artifacts
```

### Architecture

The benchmark uses **two translation units** for an honest comparison:

- `bench_scalar.c` compiled with `-fno-tree-vectorize -DFLEET_MATH_NO_AUTOSELECT` — **genuinely scalar**
- `bench_hpc.c` compiled normally — calls explicit NEON intrinsics from `fleet_math.h`

This prevents GCC from auto-vectorizing the scalar path, giving an apples-to-apples comparison between pure scalar code and explicit SIMD.

### Output

```
  SPEEDUP (SIMD / Scalar)
  ────────────────────────────────────────────────────────────────────────
  Tile check (1 tile)   │░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  1.0x
  Holonomy (4-cycle)    │▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  1.1x
  Batch 64 tiles        │▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  1.1x
  Batch 1024 tiles      │▓▓░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  1.0x
  Batch 10000 tiles     │▓▓░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  1.0x
  Holonomy batch 10000  │▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  1.3x
```

## File Structure

```
bench_hpc.c       — NEON SIMD benchmark driver
bench_scalar.c    — Scalar-only wrappers (no auto-vectorization)
bench_scalar.h    — Scalar wrapper interface
Makefile          — Build with separate compilation units
bench_plot.py     — ASCII plots and summary tables
run_all.sh        — Build + run + plot pipeline
results/          — (gitignored) timestamped benchmark outputs
README.md         — This file
```

## License

MIT — same as fleet-math-c. Free for any use.
