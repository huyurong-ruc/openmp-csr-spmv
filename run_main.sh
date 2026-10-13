#!/usr/bin/env bash
set -euo pipefail

export LC_ALL=C
export OMP_WAIT_POLICY=PASSIVE
export OMP_DYNAMIC=FALSE
export OMP_PROC_BIND=false
unset OMP_PLACES

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_file="$script_dir/csr_spmv_lab.cpp"
results_dir="$script_dir/results/main"
raw_file="$results_dir/raw.csv"
summary_file="$results_dir/summary.csv"
raw_tmp="$results_dir/.raw.csv.tmp"
summary_tmp="$results_dir/.summary.csv.tmp"
temp_dir="$(mktemp -d)"
executable="$temp_dir/csr_spmv_lab"

cleanup() {
    rm -f "$executable"
    rm -f "$raw_tmp" "$summary_tmp"
    rmdir "$temp_dir" 2>/dev/null || true
}
trap cleanup EXIT

levels=(1 2 3)
thread_counts=(1 2 4 8 16)
chunks=(1 2 4 8 16 32 64)
strategies=(dynamic guided)
rounds=(1 2 3 4 5)

student_1_count=$((${#levels[@]} * ${#thread_counts[@]} * ${#chunks[@]} * ${#strategies[@]}))
long_row_count=$((${#levels[@]} * ${#thread_counts[@]}))
logical_count=$((student_1_count + long_row_count))
raw_count=$((logical_count * ${#rounds[@]}))
if [[ "$logical_count" -ne 225 ]]; then
    echo "error: expected 225 logical configurations, got $logical_count" >&2
    exit 1
fi
if [[ "$raw_count" -ne 1125 ]]; then
    echo "error: expected 1125 raw records, got $raw_count" >&2
    exit 1
fi
echo "Logical configuration check passed: $logical_count"

if [[ ! -f "$source_file" ]]; then
    echo "error: missing source file: $source_file" >&2
    exit 1
fi
if ! command -v g++ >/dev/null 2>&1; then
    echo "error: g++ is not available" >&2
    exit 1
fi
visible_cpus="$(nproc)"
if [[ "$visible_cpus" -ne 16 ]]; then
    echo "error: expected 16 CPUs in WSL, found $visible_cpus" >&2
    exit 1
fi

g++ -O2 -std=c++17 -fopenmp -pthread "$source_file" -o "$executable"

case_parameters() {
    case "$1" in
        1) normal_nnz=90; long_nnz=690 ;;
        2) normal_nnz=75; long_nnz=2175 ;;
        3) normal_nnz=50; long_nnz=4650 ;;
        *) echo "error: unknown level $1" >&2; return 1 ;;
    esac
}

run_program() {
    local schedule="$1"
    local level="$2"
    local threads="$3"
    local repeats="$4"
    case_parameters "$level"
    if ! program_output=$(
        OMP_SCHEDULE="$schedule" "$executable" \
            --rows 20000 \
            --cols 100000 \
            --normal-nnz "$normal_nnz" \
            --long-rows 200 \
            --long-nnz "$long_nnz" \
            --threads "$threads" \
            --repeats "$repeats" \
            --seed 20260926 2>&1
    ); then
        echo "$program_output" >&2
        return 1
    fi
    if ! grep -q '^All correctness checks passed\.$' <<<"$program_output"; then
        echo "error: correctness check did not pass" >&2
        echo "$program_output" >&2
        return 1
    fi
}

runtime_for_student() {
    local student="$1"
    awk -v student="$student" '
        $1 == "OpenMP" && $2 == student { print $(NF - 1); found = 1 }
        END { if (!found) exit 1 }
    ' <<<"$program_output"
}

echo "Running smoke tests..."
run_program "dynamic,1" 1 1 1
runtime_for_student 1 >/dev/null
echo "  passed: dynamic, Level 1, threads=1, chunk=1"

run_program "dynamic,16" 3 16 1
runtime_for_student 1 >/dev/null
runtime_for_student 2 >/dev/null
echo "  passed: dynamic, Level 3, threads=16, chunk=16"
echo "  passed: long-row, Level 3, threads=16"

run_program "guided,16" 3 16 1
runtime_for_student 1 >/dev/null
echo "  passed: guided, Level 3, threads=16, chunk=16"
echo "All smoke tests passed."

mkdir -p "$results_dir"
printf '%s\n' 'strategy,level,threads,chunk,run_id,rows,cols,normal_nnz,long_rows,long_nnz,seed,correctness,runtime' >"$raw_tmp"

completed=0
for run_id in "${rounds[@]}"; do
    round_completed=0
    for level in "${levels[@]}"; do
        case_parameters "$level"
        for threads in "${thread_counts[@]}"; do
            long_row_recorded=0
            for strategy in "${strategies[@]}"; do
                for chunk in "${chunks[@]}"; do
                    run_program "$strategy,$chunk" "$level" "$threads" 10
                    student_1_runtime="$(runtime_for_student 1)"
                    printf '%s,%s,%s,%s,%s,20000,100000,%s,200,%s,20260926,pass,%s\n' \
                        "$strategy" "$level" "$threads" "$chunk" "$run_id" \
                        "$normal_nnz" "$long_nnz" "$student_1_runtime" >>"$raw_tmp"
                    completed=$((completed + 1))
                    round_completed=$((round_completed + 1))

                    if [[ "$long_row_recorded" -eq 0 ]]; then
                        long_row_runtime="$(runtime_for_student 2)"
                        printf 'long-row,%s,%s,,%s,20000,100000,%s,200,%s,20260926,pass,%s\n' \
                            "$level" "$threads" "$run_id" "$normal_nnz" "$long_nnz" \
                            "$long_row_runtime" >>"$raw_tmp"
                        long_row_recorded=1
                        completed=$((completed + 1))
                        round_completed=$((round_completed + 1))
                    fi
                done
            done
            echo "Round $run_id: completed Level $level, threads=$threads ($round_completed/$logical_count records)"
        done
    done
    if [[ "$round_completed" -ne "$logical_count" ]]; then
        echo "error: round $run_id expected $logical_count records, got $round_completed" >&2
        exit 1
    fi
done

actual_count=$(($(wc -l <"$raw_tmp") - 1))
if [[ "$actual_count" -ne "$raw_count" ]]; then
    echo "error: expected $raw_count CSV records, got $actual_count" >&2
    exit 1
fi

awk -F, '
    NR == 1 { next }
    {
        config = $1 SUBSEP $2 SUBSEP $3 SUBSEP $4
        run = config SUBSEP $5
        if (++seen_run[run] != 1) {
            print "error: duplicate configuration/run_id at input line " NR > "/dev/stderr"
            failed = 1
        }
        count[config]++
    }
    END {
        for (config in count) {
            if (count[config] != 5) {
                print "error: configuration does not have exactly five runs" > "/dev/stderr"
                failed = 1
            }
            configurations++
        }
        if (configurations != 225) {
            print "error: expected 225 unique configurations, got " configurations > "/dev/stderr"
            failed = 1
        }
        exit failed
    }
' "$raw_tmp"

awk -F, '
    BEGIN {
        OFS = ","
        print "strategy", "level", "threads", "chunk", "median_ms", "mean_ms", "min_ms", "max_ms"
    }
    NR == 1 { next }
    {
        key = $1 SUBSEP $2 SUBSEP $3 SUBSEP $4
        if (!(key in seen)) {
            seen[key] = 1
            order[++configuration_count] = key
            strategy[key] = $1
            level[key] = $2
            threads[key] = $3
            chunk[key] = $4
        }
        n = ++count[key]
        value[key, n] = $13 + 0
        total[key] += $13
    }
    END {
        for (i = 1; i <= configuration_count; ++i) {
            key = order[i]
            if (count[key] != 5) {
                print "error: summary input does not contain five runs" > "/dev/stderr"
                exit 1
            }
            for (j = 1; j <= 5; ++j) sorted[j] = value[key, j]
            for (j = 1; j <= 5; ++j) {
                for (k = j + 1; k <= 5; ++k) {
                    if (sorted[k] < sorted[j]) {
                        temporary = sorted[j]
                        sorted[j] = sorted[k]
                        sorted[k] = temporary
                    }
                }
            }
            printf "%s,%s,%s,%s,%.9g,%.9g,%.9g,%.9g\n", \
                strategy[key], level[key], threads[key], chunk[key], \
                sorted[3], total[key] / 5, sorted[1], sorted[5]
            delete sorted
        }
    }
' "$raw_tmp" >"$summary_tmp"

summary_count=$(($(wc -l <"$summary_tmp") - 1))
if [[ "$summary_count" -ne 225 ]]; then
    echo "error: expected 225 summary records, got $summary_count" >&2
    exit 1
fi

mv "$raw_tmp" "$raw_file"
mv "$summary_tmp" "$summary_file"
echo "Main experiment complete: $actual_count raw records written to $raw_file"
echo "Summary complete: $summary_count records written to $summary_file"
