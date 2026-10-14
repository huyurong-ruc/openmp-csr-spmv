#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <omp.h>

namespace {

constexpr int kRows = 20000;
constexpr int kCols = 100000;
constexpr int kNormalNnz = 50;
constexpr int kLongRows = 200;
constexpr int kLongNnz = 4650;
constexpr int kThreads = 16;
constexpr unsigned kSeed = 20260926u;

struct CsrMatrix {
    int rows = 0;
    int cols = 0;
    std::vector<int> row_ptr;
    std::vector<int> col_idx;
    std::vector<double> values;
};

struct Options {
    std::string mode;
    int repeats = 0;
    int run_id = 0;
};

struct alignas(64) ThreadTimes {
    double ordinary_ms = 0.0;
    double partial_ms = 0.0;
    double barrier_ms = 0.0;
    double merge_ms = 0.0;
};

struct CallTimes {
    double total_ms = 0.0;
    double preprocess_ms = 0.0;
    double parallel_ms = 0.0;
    int actual_threads = 0;
};

double elapsed_ms(std::chrono::steady_clock::time_point begin,
                  std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

int read_int(const char* value, const std::string& name) {
    try {
        std::size_t used = 0;
        const int result = std::stoi(value, &used);
        if (value[used] != '\0') {
            throw std::invalid_argument("trailing characters");
        }
        return result;
    } catch (const std::exception&) {
        throw std::invalid_argument("invalid value for " + name + ": " + value);
    }
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (i + 1 >= argc) {
            throw std::invalid_argument("missing value for " + arg);
        }
        if (arg == "--mode") {
            options.mode = argv[++i];
        } else if (arg == "--repeats") {
            options.repeats = read_int(argv[++i], arg);
        } else if (arg == "--run-id") {
            options.run_id = read_int(argv[++i], arg);
        } else {
            throw std::invalid_argument("unknown option: " + arg);
        }
    }
    if ((options.mode != "original" && options.mode != "instrumented") ||
        options.repeats <= 0 || options.run_id <= 0) {
        throw std::invalid_argument(
            "usage: --mode original|instrumented --repeats N --run-id N");
    }
    return options;
}

CsrMatrix make_matrix() {
    CsrMatrix matrix;
    matrix.rows = kRows;
    matrix.cols = kCols;
    matrix.row_ptr.resize(static_cast<std::size_t>(kRows) + 1, 0);

    std::mt19937 rng(kSeed);
    std::vector<int> row_ids(kRows);
    std::iota(row_ids.begin(), row_ids.end(), 0);
    std::shuffle(row_ids.begin(), row_ids.end(), rng);
    std::vector<char> is_long(kRows, false);
    for (int i = 0; i < kLongRows; ++i) {
        is_long[row_ids[i]] = true;
    }

    std::uniform_real_distribution<double> value_dist(-1.0, 1.0);
    std::uniform_int_distribution<int> column_dist(0, kCols - 1);
    matrix.col_idx.reserve(static_cast<std::size_t>(kRows - kLongRows) * kNormalNnz +
                           static_cast<std::size_t>(kLongRows) * kLongNnz);
    matrix.values.reserve(matrix.col_idx.capacity());
    for (int row = 0; row < kRows; ++row) {
        const int nnz = is_long[row] ? kLongNnz : kNormalNnz;
        matrix.row_ptr[row + 1] = matrix.row_ptr[row] + nnz;
        for (int k = 0; k < nnz; ++k) {
            matrix.col_idx.push_back(column_dist(rng));
            matrix.values.push_back(value_dist(rng));
        }
    }
    return matrix;
}

std::vector<double> make_vector() {
    std::mt19937 rng(kSeed ^ 0x9e3779b9u);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<double> x(kCols);
    for (double& value : x) {
        value = dist(rng);
    }
    return x;
}

void spmv_serial(const CsrMatrix& matrix, const std::vector<double>& x,
                 std::vector<double>& y) {
    for (int row = 0; row < matrix.rows; ++row) {
        double sum = 0.0;
        for (int p = matrix.row_ptr[row]; p < matrix.row_ptr[row + 1]; ++p) {
            sum += matrix.values[p] * x[matrix.col_idx[p]];
        }
        y[row] = sum;
    }
}

// Exact uninstrumented copy of the formal student2 kernel.
__attribute__((noinline))
void spmv_student2_original(const CsrMatrix& matrix, const std::vector<double>& x,
                            std::vector<double>& y) {
    const int total_nnz = matrix.row_ptr[matrix.rows];
    const int average_nnz = total_nnz / matrix.rows;
    std::vector<int> long_row_ids;
    for (int row = 0; row < matrix.rows; ++row) {
        const int row_nnz = matrix.row_ptr[row + 1] - matrix.row_ptr[row];
        if (row_nnz > average_nnz) {
            long_row_ids.push_back(row);
        }
    }

    const int max_threads = omp_get_max_threads();
    const int long_row_count = static_cast<int>(long_row_ids.size());
    std::vector<double> partial_sums(
        static_cast<std::size_t>(max_threads) * long_row_count, 0.0);

#pragma omp parallel
    {
        const int thread_id = omp_get_thread_num();
        const int thread_count = omp_get_num_threads();

#pragma omp for schedule(static) nowait
        for (int row = 0; row < matrix.rows; ++row) {
            const int begin = matrix.row_ptr[row];
            const int end = matrix.row_ptr[row + 1];
            if (end - begin > average_nnz) {
                continue;
            }
            double sum = 0.0;
            for (int p = begin; p < end; ++p) {
                sum += matrix.values[p] * x[matrix.col_idx[p]];
            }
            y[row] = sum;
        }

        const std::size_t partial_base =
            static_cast<std::size_t>(thread_id) * long_row_count;
        for (int long_index = 0; long_index < long_row_count; ++long_index) {
            const int row = long_row_ids[long_index];
            const int begin = matrix.row_ptr[row];
            const int length = matrix.row_ptr[row + 1] - begin;
            const int part_begin = begin + length * thread_id / thread_count;
            const int part_end = begin + length * (thread_id + 1) / thread_count;
            double local_sum = 0.0;
            for (int p = part_begin; p < part_end; ++p) {
                local_sum += matrix.values[p] * x[matrix.col_idx[p]];
            }
            partial_sums[partial_base + long_index] = local_sum;
        }

#pragma omp barrier

#pragma omp for schedule(static) nowait
        for (int long_index = 0; long_index < long_row_count; ++long_index) {
            double sum = 0.0;
            for (int thread = 0; thread < thread_count; ++thread) {
                const std::size_t index =
                    static_cast<std::size_t>(thread) * long_row_count + long_index;
                sum += partial_sums[index];
            }
            y[long_row_ids[long_index]] = sum;
        }
    }
}

// The kernel operations and OpenMP directives match student2; only timers and
// writes to the instrumentation buffers are added.
__attribute__((noinline))
CallTimes spmv_student2_instrumented(const CsrMatrix& matrix,
                                     const std::vector<double>& x,
                                     std::vector<double>& y,
                                     std::vector<ThreadTimes>& thread_times) {
    CallTimes call;
    const auto total_begin = std::chrono::steady_clock::now();
    const auto preprocess_begin = total_begin;

    const int total_nnz = matrix.row_ptr[matrix.rows];
    const int average_nnz = total_nnz / matrix.rows;
    std::vector<int> long_row_ids;
    for (int row = 0; row < matrix.rows; ++row) {
        const int row_nnz = matrix.row_ptr[row + 1] - matrix.row_ptr[row];
        if (row_nnz > average_nnz) {
            long_row_ids.push_back(row);
        }
    }

    const int max_threads = omp_get_max_threads();
    const int long_row_count = static_cast<int>(long_row_ids.size());
    std::vector<double> partial_sums(
        static_cast<std::size_t>(max_threads) * long_row_count, 0.0);

    const auto preprocess_end = std::chrono::steady_clock::now();
    const auto parallel_begin = preprocess_end;

#pragma omp parallel
    {
        const int thread_id = omp_get_thread_num();
        const int thread_count = omp_get_num_threads();
        if (thread_id == 0) {
            call.actual_threads = thread_count;
        }

        const double ordinary_begin = omp_get_wtime();
#pragma omp for schedule(static) nowait
        for (int row = 0; row < matrix.rows; ++row) {
            const int begin = matrix.row_ptr[row];
            const int end = matrix.row_ptr[row + 1];
            if (end - begin > average_nnz) {
                continue;
            }
            double sum = 0.0;
            for (int p = begin; p < end; ++p) {
                sum += matrix.values[p] * x[matrix.col_idx[p]];
            }
            y[row] = sum;
        }
        const double ordinary_end = omp_get_wtime();

        const double partial_begin = ordinary_end;
        const std::size_t partial_base =
            static_cast<std::size_t>(thread_id) * long_row_count;
        for (int long_index = 0; long_index < long_row_count; ++long_index) {
            const int row = long_row_ids[long_index];
            const int begin = matrix.row_ptr[row];
            const int length = matrix.row_ptr[row + 1] - begin;
            const int part_begin = begin + length * thread_id / thread_count;
            const int part_end = begin + length * (thread_id + 1) / thread_count;
            double local_sum = 0.0;
            for (int p = part_begin; p < part_end; ++p) {
                local_sum += matrix.values[p] * x[matrix.col_idx[p]];
            }
            partial_sums[partial_base + long_index] = local_sum;
        }
        const double partial_end = omp_get_wtime();

        const double t_before_barrier = partial_end;
#pragma omp barrier
        const double t_after_barrier = omp_get_wtime();

        const double merge_begin = t_after_barrier;
#pragma omp for schedule(static) nowait
        for (int long_index = 0; long_index < long_row_count; ++long_index) {
            double sum = 0.0;
            for (int thread = 0; thread < thread_count; ++thread) {
                const std::size_t index =
                    static_cast<std::size_t>(thread) * long_row_count + long_index;
                sum += partial_sums[index];
            }
            y[long_row_ids[long_index]] = sum;
        }
        const double merge_end = omp_get_wtime();

        ThreadTimes& out = thread_times[thread_id];
        out.ordinary_ms = (ordinary_end - ordinary_begin) * 1000.0;
        out.partial_ms = (partial_end - partial_begin) * 1000.0;
        out.barrier_ms = (t_after_barrier - t_before_barrier) * 1000.0;
        out.merge_ms = (merge_end - merge_begin) * 1000.0;
    }

    const auto parallel_end = std::chrono::steady_clock::now();
    call.preprocess_ms = elapsed_ms(preprocess_begin, preprocess_end);
    call.parallel_ms = elapsed_ms(parallel_begin, parallel_end);
    call.total_ms = elapsed_ms(total_begin, parallel_end);
    return call;
}

bool check_result(const std::vector<double>& expected,
                  const std::vector<double>& actual) {
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const double error = std::abs(expected[i] - actual[i]);
        const double scale = std::max(1.0, std::abs(expected[i]));
        if (error > 1e-10 + 1e-10 * scale) {
            std::cerr << "correctness mismatch at row " << i << '\n';
            return false;
        }
    }
    return true;
}

void print_common(const Options& options, int thread_id) {
    std::cout << options.mode << ',' << options.repeats << ',' << options.run_id << ','
              << thread_id << ',' << kRows << ',' << kCols << ',' << kNormalNnz << ','
              << kLongRows << ',' << kLongNnz << ',' << kThreads << ',' << kSeed << ',';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        omp_set_dynamic(0);
        omp_set_num_threads(kThreads);
        if (omp_get_dynamic() != 0 || omp_get_max_threads() != kThreads) {
            throw std::runtime_error("OpenMP runtime did not accept fixed 16-thread setup");
        }

        const CsrMatrix matrix = make_matrix();
        const std::vector<double> x = make_vector();
        std::vector<double> reference(kRows, 0.0);
        std::vector<double> output(kRows, 0.0);
        spmv_serial(matrix, x, reference);
        std::cout << std::fixed << std::setprecision(9);

        if (options.mode == "original") {
            spmv_student2_original(matrix, x, output);  // Untimed warm-up.
            if (!check_result(reference, output)) {
                throw std::runtime_error("original warm-up correctness check failed");
            }
            const auto begin = std::chrono::steady_clock::now();
            for (int repeat = 0; repeat < options.repeats; ++repeat) {
                spmv_student2_original(matrix, x, output);
            }
            const auto end = std::chrono::steady_clock::now();
            if (!check_result(reference, output)) {
                throw std::runtime_error("original timed correctness check failed");
            }
            print_common(options, -1);
            std::cout << elapsed_ms(begin, end) / options.repeats
                      << ",,,,,,,,pass\n";
            return 0;
        }

        std::vector<ThreadTimes> one_call(kThreads);
        std::vector<ThreadTimes> accumulated(kThreads);
        const CallTimes warmup =
            spmv_student2_instrumented(matrix, x, output, one_call);
        if (warmup.actual_threads != kThreads || !check_result(reference, output)) {
            throw std::runtime_error("instrumented warm-up validation failed");
        }

        double total_ms = 0.0;
        double preprocess_ms = 0.0;
        double parallel_ms = 0.0;
        for (int repeat = 0; repeat < options.repeats; ++repeat) {
            const CallTimes call =
                spmv_student2_instrumented(matrix, x, output, one_call);
            if (call.actual_threads != kThreads) {
                throw std::runtime_error("instrumented call did not use 16 threads");
            }
            total_ms += call.total_ms;
            preprocess_ms += call.preprocess_ms;
            parallel_ms += call.parallel_ms;
            for (int thread = 0; thread < kThreads; ++thread) {
                accumulated[thread].ordinary_ms += one_call[thread].ordinary_ms;
                accumulated[thread].partial_ms += one_call[thread].partial_ms;
                accumulated[thread].barrier_ms += one_call[thread].barrier_ms;
                accumulated[thread].merge_ms += one_call[thread].merge_ms;
            }
        }
        if (!check_result(reference, output)) {
            throw std::runtime_error("instrumented timed correctness check failed");
        }

        const double divisor = options.repeats;
        for (int thread = 0; thread < kThreads; ++thread) {
            print_common(options, thread);
            std::cout << ',' << total_ms / divisor << ',' << preprocess_ms / divisor << ','
                      << parallel_ms / divisor << ','
                      << accumulated[thread].ordinary_ms / divisor << ','
                      << accumulated[thread].partial_ms / divisor << ','
                      << accumulated[thread].barrier_ms / divisor << ','
                      << accumulated[thread].merge_ms / divisor << ",pass\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
