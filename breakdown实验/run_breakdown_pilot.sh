#!/usr/bin/env bash
set -euo pipefail

export LC_ALL=C
export OMP_WAIT_POLICY=PASSIVE
export OMP_DYNAMIC=FALSE
export OMP_PROC_BIND=false
unset OMP_PLACES

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_file="$script_dir/csr_spmv_breakdown.cpp"
output_file="$script_dir/breakdown_pilot.csv"
temp_dir="$(mktemp -d)"
executable="$temp_dir/csr_spmv_breakdown"
output_tmp="$temp_dir/breakdown_pilot.csv"

cleanup() {
    rm -f "$executable" "$output_tmp"
    rmdir "$temp_dir" 2>/dev/null || true
}
trap cleanup EXIT

if [[ -e "$output_file" ]]; then
    echo "error: refusing to overwrite existing $output_file" >&2
    exit 1
fi
if [[ "$(nproc)" -ne 16 ]]; then
    echo "error: expected exactly 16 visible CPUs, found $(nproc)" >&2
    exit 1
fi
if [[ "$OMP_WAIT_POLICY" != PASSIVE || "$OMP_DYNAMIC" != FALSE ||
      "$OMP_PROC_BIND" != false || -n "${OMP_PLACES+x}" ]]; then
    echo "error: required OpenMP environment is not active" >&2
    exit 1
fi

g++ -O2 -std=c++17 -fopenmp -pthread "$source_file" -o "$executable"

printf '%s\n' \
    'mode,repeats,run_id,thread_id,rows,cols,normal_nnz,long_rows,long_nnz,threads,seed,original_runtime_ms,instrumented_total_ms,preprocess_ms,parallel_region_ms,ordinary_ms,long_partial_ms,barrier_ms,merge_ms,correctness' \
    >"$output_tmp"

for run_id in 1 2 3 4 5; do
    "$executable" --mode original --repeats 500 --run-id "$run_id" >>"$output_tmp"
    echo "original: completed process $run_id/5" >&2
done

for repeats in 100 500; do
    for run_id in 1 2 3 4 5; do
        "$executable" --mode instrumented --repeats "$repeats" --run-id "$run_id" \
            >>"$output_tmp"
        echo "instrumented repeats=$repeats: completed process $run_id/5" >&2
    done
done

expected_lines=$((1 + 5 + 2 * 5 * 16))
actual_lines="$(wc -l <"$output_tmp")"
if [[ "$actual_lines" -ne "$expected_lines" ]]; then
    echo "error: expected $expected_lines CSV lines, found $actual_lines" >&2
    exit 1
fi
if [[ "$(awk -F, 'NR > 1 && $20 != "pass" { bad++ } END { print bad+0 }' "$output_tmp")" -ne 0 ]]; then
    echo "error: at least one correctness check failed" >&2
    exit 1
fi

mv "$output_tmp" "$output_file"
echo "pilot data written to $output_file" >&2
