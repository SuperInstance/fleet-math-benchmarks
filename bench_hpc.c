/*
 * bench_hpc.c — High-performance benchmarks for fleet-math SIMD operations
 *
 * Compares SCALAR (compiled without auto-vectorization) vs ARM NEON SIMD
 * implementations on actual Neoverse-N1 hardware.
 *
 * The scalar wrappers live in bench_scalar.c and are compiled separately
 * with -fno-tree-vectorize to prevent GCC from auto-vectorizing them.
 * The NEON wrappers use explicit intrinsics from fleet_math.h.
 *
 * Build:
 *   gcc -O3 -march=native -ffast-math -I../fleet-math-c \
 *       -o bench_hpc bench_hpc.c bench_scalar.c ../fleet-math-c/fleet_math.c -lm
 *
 * This file is part of fleet-math-benchmarks.
 * License: MIT
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>

/* fleet_math.h from fleet-math-c */
#include "fleet_math.h"
#include "bench_scalar.h"  /* non-vectorized scalar wrappers */

/* =========================================================================
 * Compiler barrier — prevents reordering across volatile store
 * ========================================================================= */
#define COMPILER_BARRIER()  __asm__ volatile("" ::: "memory")

/* =========================================================================
 * NEON-only wrappers (separate compilation unit prevents cross-unit IPO)
 * ========================================================================= */
static int __attribute__((noinline))
wrap_neon_check(const plato_tile_t *tiles, int n, float threshold)
{
    int total = 0;
    for (int i = 0; i < n; i++)
        total += tile_check_violations_neon(&tiles[i], threshold);
    return total;
}

static float __attribute__((noinline))
wrap_neon_holo(const float *weights, int n)
{
    float total = 0.0f;
    for (int i = 0; i < n; i++)
        total += holonomy_4cycle_neon(&weights[i * 4]);
    return total;
}

static int __attribute__((noinline))
wrap_neon_batch(const plato_tile_t *tiles, int n, float threshold)
{
    return batch_check_tiles_neon(tiles, n, threshold);
}

static void __attribute__((noinline))
wrap_neon_batch_holo(const float *weights, int n, float *out)
{
    batch_holonomy_4cycles_neon(weights, n, out);
}

/* =========================================================================
 * Memory bandwidth — forced volatile read of one field per tile
 * ========================================================================= */
static double __attribute__((noinline))
wrap_stream_read(const volatile plato_tile_t *tiles, int n)
{
    double acc = 0.0;
    for (int i = 0; i < n; i++)
        acc += (double)tiles[i].confidence;
    return acc;
}

/* =========================================================================
 * Nanosecond timer
 * ========================================================================= */
static inline double
time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/* =========================================================================
 * CPU warmup
 * ========================================================================= */
static void
warmup(void)
{
    double acc = 0.0;
    for (int i = 0; i < 5000000; i++)
        acc += (acc * 0.5 + 1.0) * 0.001;
    volatile int dummy[4096];
    for (int i = 0; i < 100000; i++)
        dummy[(i * 37) & 4095] = i;
    (void)acc;
}

/* =========================================================================
 * Print helpers
 * ========================================================================= */
static void
print_header(void)
{
    printf("%-24s | %-9s | %-9s | %s\n", "Operation", "Scalar", "NEON SIMD", "Speedup");
    printf("%-24s-+-%-9s-+-%-9s-+-%s\n",
           "------------------------", "---------", "---------", "-------");
}

static void
print_row(const char *label, double scalar_ns, double simd_ns)
{
    double speedup = (simd_ns > 0) ? scalar_ns / simd_ns : 0.0;
    printf("%-24s | %9.1f ns | %9.1f ns | %5.1fx\n",
           label, scalar_ns, simd_ns, speedup);
}

/* =========================================================================
 * Benchmark 1: Per-tile validation throughput
 * ========================================================================= */
static void
bench_tile_check(plato_tile_t *tiles, int n, float threshold)
{
    enum { EPOCHS = 7 };
    double best_scalar = 1e100, best_simd = 1e100;
    volatile int sink = 0;

    /* Warmup + bring data into cache */
    sink += bench_scalar_check(tiles, n, threshold);
    sink += wrap_neon_check(tiles, n, threshold);
    COMPILER_BARRIER();

    for (int ep = 0; ep < EPOCHS; ep++) {
        double t0, t1;

        /* Scalar (no auto-vectorization) */
        t0 = time_ns();
        sink += bench_scalar_check(tiles, n, threshold);
        COMPILER_BARRIER();
        t1 = time_ns();
        double ns_per_op = (t1 - t0) / (double)n;
        if (ns_per_op < best_scalar) best_scalar = ns_per_op;

        /* NEON SIMD */
        t0 = time_ns();
        sink += wrap_neon_check(tiles, n, threshold);
        COMPILER_BARRIER();
        t1 = time_ns();
        ns_per_op = (t1 - t0) / (double)n;
        if (ns_per_op < best_simd) best_simd = ns_per_op;
    }

    print_row("Tile check (1 tile)", best_scalar, best_simd);
    (void)sink;
}

/* =========================================================================
 * Benchmark 2: Per-cycle holonomy
 * ========================================================================= */
static void
bench_holonomy(float *weights, int n)
{
    enum { EPOCHS = 7 };
    double best_scalar = 1e100, best_simd = 1e100;
    volatile float sink = 0.0f;

    /* Warmup */
    sink += bench_scalar_holo(weights, n);
    sink += wrap_neon_holo(weights, n);
    COMPILER_BARRIER();

    for (int ep = 0; ep < EPOCHS; ep++) {
        double t0, t1;

        /* Scalar (no auto-vectorization) */
        t0 = time_ns();
        sink += bench_scalar_holo(weights, n);
        COMPILER_BARRIER();
        t1 = time_ns();
        double ns_per_op = (t1 - t0) / (double)n;
        if (ns_per_op < best_scalar) best_scalar = ns_per_op;

        /* NEON SIMD */
        t0 = time_ns();
        sink += wrap_neon_holo(weights, n);
        COMPILER_BARRIER();
        t1 = time_ns();
        ns_per_op = (t1 - t0) / (double)n;
        if (ns_per_op < best_simd) best_simd = ns_per_op;
    }

    print_row("Holonomy (4-cycle)", best_scalar, best_simd);
    (void)sink;
}

/* =========================================================================
 * Benchmark 3: Batch throughput at multiple sizes
 * ========================================================================= */
static void
bench_batch(plato_tile_t *tiles, int n, float threshold)
{
    enum { EPOCHS = 7 };
    double best_scalar = 1e100, best_simd = 1e100;
    volatile int sink = 0;

    /* Warmup */
    sink += bench_scalar_batch(tiles, n, threshold);
    sink += wrap_neon_batch(tiles, n, threshold);
    COMPILER_BARRIER();

    for (int ep = 0; ep < EPOCHS; ep++) {
        double t0, t1;

        /* Scalar */
        t0 = time_ns();
        sink += bench_scalar_batch(tiles, n, threshold);
        COMPILER_BARRIER();
        t1 = time_ns();
        double ns_total = (t1 - t0);
        if (ns_total < best_scalar) best_scalar = ns_total;

        /* NEON SIMD */
        t0 = time_ns();
        sink += wrap_neon_batch(tiles, n, threshold);
        COMPILER_BARRIER();
        t1 = time_ns();
        ns_total = (t1 - t0);
        if (ns_total < best_simd) best_simd = ns_total;
    }

    char label[48];
    snprintf(label, sizeof(label), "Batch %d tiles", n);
    print_row(label, best_scalar, best_simd);
    (void)sink;
}

/* =========================================================================
 * Benchmark 4: Batch holonomy throughput
 * ========================================================================= */
static void
bench_batch_holonomy(float *weights, int n)
{
    enum { EPOCHS = 7 };
    double best_scalar = 1e100, best_simd = 1e100;

    float *output = (float *)aligned_alloc(64, (size_t)n * sizeof(float));
    if (!output) {
        fprintf(stderr, "WARN: bench_batch_holonomy alloc failed, skipping\n");
        return;
    }

    volatile float sink = 0.0f;

    /* Warmup */
    bench_scalar_batch_holo(weights, n, output);
    COMPILER_BARRIER();
    for (int i = 0; i < n; i++) sink += output[i];

    wrap_neon_batch_holo(weights, n, output);
    COMPILER_BARRIER();
    for (int i = 0; i < n; i++) sink += output[i];

    for (int ep = 0; ep < EPOCHS; ep++) {
        double t0, t1;

        /* Scalar batch */
        t0 = time_ns();
        bench_scalar_batch_holo(weights, n, output);
        COMPILER_BARRIER();
        t1 = time_ns();
        double ns_per_cycle = (t1 - t0) / (double)n;
        if (ns_per_cycle < best_scalar) best_scalar = ns_per_cycle;

        for (int i = 0; i < n; i++) sink += output[i];

        /* NEON batch */
        t0 = time_ns();
        wrap_neon_batch_holo(weights, n, output);
        COMPILER_BARRIER();
        t1 = time_ns();
        ns_per_cycle = (t1 - t0) / (double)n;
        if (ns_per_cycle < best_simd) best_simd = ns_per_cycle;

        for (int i = 0; i < n; i++) sink += output[i];
    }

    char label[48];
    snprintf(label, sizeof(label), "Holonomy batch %d", n);
    print_row(label, best_scalar, best_simd);

    free(output);
    (void)sink;
}

/* =========================================================================
 * Benchmark 5: Memory bandwidth
 *
 * Streams 64-byte tiles through three cache/memory levels:
 *   L1:   1,024 tiles (  64 KB)
 *   L2:  16,384 tiles (   1 MB)
 *   Mem: 262,144 tiles (  16 MB)
 * ========================================================================= */
static void
bench_memory_bandwidth(const plato_tile_t *tiles, int n_full)
{
    enum { EPOCHS = 5 };
    const size_t tile_size = sizeof(plato_tile_t);

    printf("\n");
    printf("--- Memory Bandwidth ---\n");

#define MEASURE_BW(label, n_tiles) do {                                                   \
    double best_ns = 1e100;                                                                \
    double acc = 0.0;                                                                      \
    volatile double v_acc = 0.0;                                                           \
    acc += wrap_stream_read((const volatile plato_tile_t *)tiles, n_tiles);                \
    COMPILER_BARRIER();                                                                     \
    for (int ep = 0; ep < EPOCHS; ep++) {                                                   \
        double t0 = time_ns();                                                              \
        acc += wrap_stream_read((const volatile plato_tile_t *)tiles, n_tiles);             \
        COMPILER_BARRIER();                                                                 \
        double t1 = time_ns();                                                              \
        double total_ns = (t1 - t0);                                                        \
        if (total_ns < best_ns) best_ns = total_ns;                                         \
    }                                                                                       \
    double bytes = (double)(n_tiles) * (double)tile_size;                                    \
    double bw = (bytes / 1e9) / (best_ns / 1e9);                                            \
    int kb = (int)(bytes / 1024.0);                                                         \
    int mb = (int)(bytes / (1024.0 * 1024.0));                                              \
    if (mb > 0)                                                                             \
        printf("%-24s %6.1f GB/s  (%3d MB, %d tiles × 64 B)\n", label, bw, mb, n_tiles);   \
    else                                                                                    \
        printf("%-24s %6.1f GB/s  (%4d KB, %d tiles × 64 B)\n", label, bw, kb, n_tiles);   \
    v_acc += acc;                                                                           \
    (void)v_acc;                                                                            \
} while(0)

    if (n_full >= 262144)
        MEASURE_BW("Memory bandwidth:", 262144);
    if (n_full >= 16384)
        MEASURE_BW("L2 cache bandwidth:", 16384);
    if (n_full >= 1024)
        MEASURE_BW("L1 cache bandwidth:", 1024);

#undef MEASURE_BW
}

/* =========================================================================
 * Main
 * ========================================================================= */
int main(void)
{
    /* Identify hardware */
    FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
    char model[256] = "ARM Neoverse-N1 (Ampere)";
    if (cpuinfo) {
        char line[256];
        while (fgets(line, sizeof(line), cpuinfo)) {
            if (strncmp(line, "model name", 10) == 0) {
                char *p = strchr(line, ':');
                if (p) {
                    p++; while (*p == ' ') p++;
                    char *nl = strchr(p, '\n');
                    if (nl) *nl = '\0';
                    snprintf(model, sizeof(model), "%s", p);
                }
                break;
            }
        }
        fclose(cpuinfo);
    }

    printf("\n=== Fleet Math SIMD Benchmarks ===\n");
    printf("Hardware: %s\n", model);
    printf("SIMD:     %s\n", fleet_math_impl_name());
    printf("Date:     2026-05-11\n\n");

    /* Configuration */
    enum {
        N_TILES_MEM     = 262144,
        N_TILES_CHECK   = 10000,
        N_HOLO          = 10000,
        N_HOLO_BATCH    = 10000
    };

    plato_tile_t *tiles = (plato_tile_t *)aligned_alloc(64,
        (size_t)N_TILES_MEM * sizeof(plato_tile_t));
    float *weights = (float *)aligned_alloc(64,
        (size_t)N_HOLO_BATCH * 4 * sizeof(float));

    if (!tiles || !weights) {
        fprintf(stderr, "ERROR: allocation failed\n");
        free(tiles); free(weights);
        return 1;
    }

    /* Initialize with deterministic semi-random data */
    srand(42);
    for (int i = 0; i < N_TILES_MEM; i++) {
        tiles[i].confidence = (float)(rand() % 100) / 100.0f;
        tiles[i].novelty    = (float)(rand() % 1000) / 100.0f;
        for (int j = 0; j < 4; j++)
            tiles[i].gradient[j] = (float)(rand() % 100) / 100.0f;
        for (int j = 0; j < 8; j++)
            tiles[i].metadata[j] = (float)(rand() % 1000) / 100.0f;
        memset(tiles[i].hash, 0, 8);
    }
    for (int i = 0; i < N_HOLO_BATCH * 4; i++)
        weights[i] = (float)(rand() % 1000) / 100.0f;

    warmup();
    COMPILER_BARRIER();

    print_header();

    bench_tile_check(tiles, N_TILES_CHECK, 0.5f);
    bench_holonomy(weights, N_HOLO);
    bench_batch(tiles, 64,    0.5f);
    bench_batch(tiles, 1024,  0.5f);
    bench_batch(tiles, 10000, 0.5f);
    bench_batch_holonomy(weights, N_HOLO_BATCH);
    bench_memory_bandwidth(tiles, N_TILES_MEM);

    printf("\n");
    printf("--- Real-World Interpretation ---\n");
    printf("PLATO room size: ~1,180 tiles (typical)\n");

    printf("\n=== Benchmark complete ===\n\n");

    free(tiles);
    free(weights);
    return 0;
}
