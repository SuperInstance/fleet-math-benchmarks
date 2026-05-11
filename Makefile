CC       = gcc
CFLAGS   = -O3 -ffast-math -march=native -I../fleet-math-c
LDFLAGS  = -lm
WARN     = -Wall -Wextra -Wpedantic -Wstrict-prototypes

ARCH     = $(shell uname -m)
NEON_FLAG =

# On ARM, ensure AVX-512 is disabled
ifneq ($(filter aarch64 arm64,$(ARCH)),)
    NEON_FLAG = -DFLEET_MATH_ENABLE_AVX512=0
endif

# fleet-math-c source (sibling directory)
FLEET_MATH_C = ../fleet-math-c
FLEET_MATH_O = $(FLEET_MATH_C)/fleet_math.o

# Objects
OBJS = bench_hpc.o bench_scalar_scalar.o

.PHONY: all bench_hpc clean run run-save results info

all: bench_hpc

# Build fleet_math.o from the sibling repo if needed
$(FLEET_MATH_O):
	$(MAKE) -C $(FLEET_MATH_C) fleet_math.o

# bench_hpc.o — compiled with full optimization (NEON intrinsics active)
bench_hpc.o: bench_hpc.c bench_scalar.h
	$(CC) $(CFLAGS) $(NEON_FLAG) $(WARN) -c -o $@ bench_hpc.c

# bench_scalar_scalar.o — compiled WITHOUT auto-vectorization for true scalar
bench_scalar_scalar.o: bench_scalar.c
	$(CC) $(CFLAGS) $(NEON_FLAG) $(WARN) -fno-tree-vectorize -DFLEET_MATH_NO_AUTOSELECT \
		-c -o $@ bench_scalar.c

bench_hpc: bench_hpc.o bench_scalar_scalar.o $(FLEET_MATH_O)
	$(CC) $(CFLAGS) -o $@ bench_hpc.o bench_scalar_scalar.o $(FLEET_MATH_O) $(LDFLAGS)

# Standalone build (no dependency on sibling repo's .o)
standalone: bench_hpc.c bench_scalar.c $(FLEET_MATH_C)/fleet_math.c
	$(CC) $(CFLAGS) $(NEON_FLAG) $(WARN) -c -o bench_hpc_so.o bench_hpc.c
	$(CC) $(CFLAGS) $(NEON_FLAG) $(WARN) -fno-tree-vectorize -DFLEET_MATH_NO_AUTOSELECT \
		-c -o bench_scalar_so.o bench_scalar.c
	$(CC) $(CFLAGS) -o bench_hpc_standalone \
		bench_hpc_so.o bench_scalar_so.o $(FLEET_MATH_C)/fleet_math.c $(LDFLAGS)

run: bench_hpc
	@mkdir -p results
	./bench_hpc | tee results/benchmark_output.txt

run-save: bench_hpc
	@mkdir -p results
	./bench_hpc 2>&1 | tee results/benchmark_output_$$(date +%Y%m%d_%H%M%S).txt

# Raw CSV output for plotting
csv: bench_hpc
	@mkdir -p results
	./bench_hpc 2>&1 | grep -E "^[A-Za-z].*\|" | sed 's/|/,/g;s/ ns//g;s/ x//g;s/ //g' \
		> results/benchmark_data.csv
	@echo "CSV saved to results/benchmark_data.csv"

clean:
	rm -f bench_hpc bench_hpc_standalone bench_hpc.o bench_scalar_scalar.o
	rm -f bench_hpc_so.o bench_scalar_so.o
	rm -rf results/

info:
	@echo "Architecture:  $(ARCH)"
	@echo "Compiler:      $(CC)"
	@echo "CFLAGS:        $(CFLAGS) $(NEON_FLAG)"
	@echo "Fleet-math:    $(FLEET_MATH_C)"
	@echo ""
	@echo "Build with:    make"
	@echo "Run:           make run"
	@echo "Save output:   make run-save"
	@echo "CSV export:    make csv"
	@echo "Clean:         make clean"
