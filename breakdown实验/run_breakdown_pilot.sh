#!/usr/bin/env bash
set -euo pipefail

export LC_ALL=C
export OMP_WAIT_POLICY=PASSIVE
export OMP_DYNAMIC=FALSE
export OMP_PROC_BIND=false
unset OMP_PLACES

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_file="$script_dir/csr_spmv_breakdown.cpp"
temp_dir="$(mktemp -d)"
executable="$temp_dir/csr_spmv_breakdown"
output_tmp="$temp_dir/output.csv"

usage() {
    cat <<'EOF'
Usage: run_breakdown_pilot.sh MODE [OUTPUT_FILE]

Modes:
  original-500x5          original, 500 repeats, 5 independent processes
  instrumented-100x5     instrumented, 100 repeats, 5 independent processes
  instrumented-500x5     instrumented, 500 repeats, 5 independent processes
  instrumented-1000x15   instrumented, 1000 repeats, 15 independent processes
  all                     run all four groups in breakdown_pilot.csv order

The script always creates a new CSV and refuses to overwrite an existing path.
EOF
}

if [[ "$#" -lt 1 || "$#" -gt 2 ]]; then
    usage >&2
    exit 1
fi

mode="$1"
case "$mode" in
    original-500x5)
        default_output="$script_dir/breakdown_original_500x5.csv"
        ;;
    instrumented-100x5)
        default_output="$script_dir/breakdown_instrumented_100x5.csv"
        ;;
    instrumented-500x5)
        default_output="$script_dir/breakdown_instrumented_500x5.csv"
        ;;
    instrumented-1000x15)
        default_output="$script_dir/breakdown_instrumented_1000x15.csv"
        ;;
    all)
        default_output="$script_dir/breakdown_pilot_reproduced.csv"
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        echo "error: unknown mode: $mode" >&2
        usage >&2
        exit 1
        ;;
esac
output_file="${2:-$default_output}"

cleanup() {
    rm -f "$executable" "$output_tmp"
    rmdir "$temp_dir" 2>/dev/null || true
}
trap cleanup EXIT

if [[ -e "$output_file" ]]; then
    echo "error: refusing to overwrite existing $output_file" >&2
    exit 1
fi
if [[ ! -d "$(dirname "$output_file")" ]]; then
    echo "error: output directory does not exist: $(dirname "$output_file")" >&2
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

expected_records=0

run_group() {
    local measurement_mode="$1"
    local repeats="$2"
    local process_count="$3"
    local records_per_process=1
    if [[ "$measurement_mode" == instrumented ]]; then
        records_per_process=16
    fi

    for ((run_id = 1; run_id <= process_count; ++run_id)); do
        "$executable" --mode "$measurement_mode" --repeats "$repeats" --run-id "$run_id" \
            >>"$output_tmp"
        echo "$measurement_mode repeats=$repeats: completed process $run_id/$process_count" >&2
    done
    expected_records=$((expected_records + process_count * records_per_process))
}

case "$mode" in
    original-500x5)
        run_group original 500 5
        ;;
    instrumented-100x5)
        run_group instrumented 100 5
        ;;
    instrumented-500x5)
        run_group instrumented 500 5
        ;;
    instrumented-1000x15)
        run_group instrumented 1000 15
        ;;
    all)
        run_group original 500 5
        run_group instrumented 100 5
        run_group instrumented 500 5
        run_group instrumented 1000 15
        ;;
esac

expected_lines=$((1 + expected_records))
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
echo "breakdown data written to $output_file" >&2
