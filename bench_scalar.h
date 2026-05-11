/*
 * bench_scalar.h — Interface for scalar-only benchmark wrappers
 *
 * These functions are compiled in a separate translation unit with
 * -fno-tree-vectorize to prevent auto-vectorization, ensuring a true
 * scalar vs SIMD comparison.
 *
 * This file is part of fleet-math-benchmarks.
 * License: MIT
 */

#ifndef BENCH_SCALAR_H
#define BENCH_SCALAR_H

#include "fleet_math.h"

int   bench_scalar_check(const plato_tile_t *tiles, int n, float threshold);
float bench_scalar_holo(const float *weights, int n);
int   bench_scalar_batch(const plato_tile_t *tiles, int n, float threshold);
void  bench_scalar_batch_holo(const float *weights, int n, float *out);

#endif /* BENCH_SCALAR_H */
