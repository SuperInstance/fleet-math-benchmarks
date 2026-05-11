/*
 * bench_scalar.c — Scalar-only wrappers compiled WITHOUT auto-vectorization
 *
 * Compiled with -fno-tree-vectorize and -DFLEET_MATH_NO_AUTOSELECT to ensure
 * these functions run as pure scalar code, for accurate scalar vs SIMD comparison.
 *
 * This file is part of fleet-math-benchmarks.
 * License: MIT
 */

#include "fleet_math.h"

/* Disable auto-vectorization for this translation unit */
#pragma GCC optimize("no-tree-vectorize")

int
bench_scalar_check(const plato_tile_t *tiles, int n, float threshold)
{
    int total = 0;
    for (int i = 0; i < n; i++)
        total += tile_check_violations_scalar(&tiles[i], threshold);
    return total;
}

float
bench_scalar_holo(const float *weights, int n)
{
    float total = 0.0f;
    for (int i = 0; i < n; i++)
        total += holonomy_4cycle_scalar(&weights[i * 4]);
    return total;
}

int
bench_scalar_batch(const plato_tile_t *tiles, int n, float threshold)
{
    return batch_check_tiles_scalar(tiles, n, threshold);
}

void
bench_scalar_batch_holo(const float *weights, int n, float *out)
{
    batch_holonomy_4cycles_scalar(weights, n, out);
}
