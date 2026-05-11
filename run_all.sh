#!/usr/bin/env bash
#
# run_all.sh — Build and run all benchmarks, produce complete report
#
# Usage:
#   bash run_all.sh              # run benchmarks, print report
#   bash run_all.sh --save       # save report to results/
#   bash run_all.sh --csv        # also export CSV for plotting
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RESULTS_DIR="${SCRIPT_DIR}/results"
mkdir -p "$RESULTS_DIR"

# Build
echo "=== Building bench_hpc ==="
make clean 2>/dev/null || true
make bench_hpc 2>&1 || {
    echo "ERROR: Build failed. Check that fleet-math-c is at ../fleet-math-c"
    exit 1
}

echo ""
echo "=== Running Benchmarks ==="
echo ""

RUN_TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUTPUT_FILE="${RESULTS_DIR}/benchmark_output_${RUN_TIMESTAMP}.txt"
RAW_CSV="${RESULTS_DIR}/benchmark_data_${RUN_TIMESTAMP}.csv"

# Run benchmarks
./bench_hpc 2>&1 | tee "$OUTPUT_FILE"

echo ""
echo "=== Generating Plots ==="
python3 "${SCRIPT_DIR}/bench_plot.py" "$OUTPUT_FILE" 2>/dev/null | tee -a "$OUTPUT_FILE"

# CSV export
awk '
BEGIN { in_data = 0 }
/^[A-Za-z].*\|/ {
    gsub(/ ns/, "", $0)
    gsub(/ x/, "", $0)
    gsub(/ *\| */, ",", $0)
    gsub(/^ +| +$/, "", $0)
    if (!in_data) {
        print "Operation,Scalar_ns,SIMD_ns,Speedup"
        in_data = 1
    }
    print
}' "$OUTPUT_FILE" > "$RAW_CSV" 2>/dev/null || true

echo "=== Done ==="
echo "Report:    ${OUTPUT_FILE}"
echo "CSV:       ${RAW_CSV}"

# Print summary line count
TOTAL_OPS=$(grep -c "Speedup" "$OUTPUT_FILE" 2>/dev/null || echo "0")
echo "Benchmarks: ${TOTAL_OPS} operations measured"
echo ""
