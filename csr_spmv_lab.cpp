#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

struct CsrMatrix {
    int rows = 0;
    int cols = 0;
    std::vector<int> row_ptr;
    std::vector<int> col_idx;
    std::vector<double> values;
};

struct Options {
    int rows = 10000;
    int cols = 100000;
    int normal_nnz = 32;
    int long_rows = 32;
    int long_nnz = 20000;
    int threads = 8;
    int repeats = 10;
    unsigned seed = 20260926u;
    std::string only = "all";
};

static void usage(const char* program) {
    std::cout
        << "Usage: " << program << " [options]\n"
        << "  --rows N          number of rows, default 10000\n"
        << "  --cols N          number of columns, default 100000\n"
        << "  --normal-nnz N    nnz per ordinary row, default 32\n"
        << "  --long-rows N     number of skewed long rows, default 32\n"
        << "  --long-nnz N      nnz per long row, default 20000\n"
        << "  --threads N       std::thread/OpenMP threads, default 8\n"
        << "  --repeats N       timed repetitions, default 10\n"
        << "  --seed N          fixed random seed, default 20260926\n"
        << "  --only NAME       run only serial, thread, omp1, or omp2\n";
}

static int read_int_arg(int argc, char** argv, int& i, const std::string& name) {
    if (i + 1 >= argc) {
        throw std::invalid_argument("missing value for " + name);
    }
    return std::stoi(argv[++i]);
}

static Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            std::exit(0);
        } else if (arg == "--rows") {
            options.rows = read_int_arg(argc, argv, i, arg);
        } else if (arg == "--cols") {
            options.cols = read_int_arg(argc, argv, i, arg);
        } else if (arg == "--normal-nnz") {
            options.normal_nnz = read_int_arg(argc, argv, i, arg);
        } else if (arg == "--long-rows") {
            options.long_rows = read_int_arg(argc, argv, i, arg);
        } else if (arg == "--long-nnz") {
            options.long_nnz = read_int_arg(argc, argv, i, arg);
        } else if (arg == "--threads") {
            options.threads = read_int_arg(argc, argv, i, arg);
        } else if (arg == "--repeats") {
            options.repeats = read_int_arg(argc, argv, i, arg);
        } else if (arg == "--seed") {
            options.seed = static_cast<unsigned>(read_int_arg(argc, argv, i, arg));
        } else if (arg == "--only") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value for " + arg);
            }
            options.only = argv[++i];
        } else {
            throw std::invalid_argument("unknown option: " + arg);
        }
    }

    if (options.rows <= 0 || options.cols <= 0 || options.normal_nnz < 0 ||
        options.long_rows < 0 || options.long_rows > options.rows ||
        options.long_nnz < 0 || options.long_nnz > options.cols ||
        options.threads <= 0 || options.repeats <= 0) {
        throw std::invalid_argument("invalid option value");
    }
    if (options.only != "all" && options.only != "serial" &&
        options.only != "thread" && options.only != "omp1" &&
        options.only != "omp2") {
        throw std::invalid_argument("invalid --only value: " + options.only);
    }
    return options;
}

static CsrMatrix make_skewed_matrix(const Options& options) {
    CsrMatrix matrix;
    matrix.rows = options.rows;
    matrix.cols = options.cols;
    matrix.row_ptr.resize(static_cast<std::size_t>(matrix.rows) + 1, 0);

    std::mt19937 rng(options.seed);
    std::vector<int> row_ids(matrix.rows);
    std::iota(row_ids.begin(), row_ids.end(), 0);
    std::shuffle(row_ids.begin(), row_ids.end(), rng);
    std::vector<char> is_long(matrix.rows, false);
    for (int i = 0; i < options.long_rows; ++i) {
        is_long[row_ids[i]] = true;
    }

    std::uniform_real_distribution<double> value_dist(-1.0, 1.0);
    std::uniform_int_distribution<int> column_dist(0, options.cols - 1);

    for (int row = 0; row < matrix.rows; ++row) {
        const int nnz = is_long[row] ? options.long_nnz : options.normal_nnz;
        matrix.row_ptr[row + 1] = matrix.row_ptr[row] + nnz;
        for (int k = 0; k < nnz; ++k) {
            matrix.col_idx.push_back(column_dist(rng));
            matrix.values.push_back(value_dist(rng));
        }
    }
    return matrix;
}

static std::vector<double> make_vector(int size, unsigned seed) {
    std::mt19937 rng(seed ^ 0x9e3779b9u);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<double> x(size);
    for (double& value : x) {
        value = dist(rng);
    }
    return x;
}

static void spmv_serial(const CsrMatrix& matrix, const std::vector<double>& x,
                        std::vector<double>& y) {
    for (int row = 0; row < matrix.rows; ++row) {
        double sum = 0.0;
        for (int p = matrix.row_ptr[row]; p < matrix.row_ptr[row + 1]; ++p) {
            sum += matrix.values[p] * x[matrix.col_idx[p]];
        }
        y[row] = sum;
    }
}

static void spmv_std_thread(const CsrMatrix& matrix, const std::vector<double>& x,
                            std::vector<double>& y, int thread_count) {
    const int workers = std::max(1, std::min(thread_count, matrix.rows));
    std::vector<std::thread> threads;
    threads.reserve(workers);

    for (int worker = 0; worker < workers; ++worker) {
        const int begin = matrix.rows * worker / workers;
        const int end = matrix.rows * (worker + 1) / workers;
        threads.emplace_back([&, begin, end]() {
            for (int row = begin; row < end; ++row) {
                double sum = 0.0;
                for (int p = matrix.row_ptr[row]; p < matrix.row_ptr[row + 1]; ++p) {
                    sum += matrix.values[p] * x[matrix.col_idx[p]];
                }
                y[row] = sum;
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
}

// Exploration implementation: select dynamic/guided and chunk through OMP_SCHEDULE.
static void spmv_omp_student_1(const CsrMatrix& matrix,
                                     const std::vector<double>& x,
                                     std::vector<double>& y) {
#pragma omp parallel for schedule(runtime)
    for (int row = 0; row < matrix.rows; ++row) {
        double sum = 0.0;
        for (int p = matrix.row_ptr[row]; p < matrix.row_ptr[row + 1]; ++p) {
            sum += matrix.values[p] * x[matrix.col_idx[p]];
        }
        y[row] = sum;
    }
}

// Long-row implementation: one OpenMP team handles both phases without nesting.
static void spmv_omp_student_2(const CsrMatrix& matrix,
                                      const std::vector<double>& x,
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

#ifdef _OPENMP
    const int max_threads = omp_get_max_threads();
#else
    const int max_threads = 1;
#endif
    const int long_row_count = static_cast<int>(long_row_ids.size());
    std::vector<double> partial_sums(
        static_cast<std::size_t>(max_threads) * long_row_count, 0.0);

#pragma omp parallel
    {
#ifdef _OPENMP
        const int thread_id = omp_get_thread_num();
        const int thread_count = omp_get_num_threads();
#else
        const int thread_id = 0;
        const int thread_count = 1;
#endif

        // Phase 1: each ordinary row is computed as a whole by one thread.
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

        // Phase 2: every thread computes its NNZ slice for all long rows.
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

        // Phase 3: long rows are reduced independently across the same team.
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

static bool check_result(const std::vector<double>& expected,
                         const std::vector<double>& actual,
                         double abs_tol = 1e-10, double rel_tol = 1e-10) {
    if (expected.size() != actual.size()) {
        return false;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const double error = std::abs(expected[i] - actual[i]);
        const double scale = std::max(1.0, std::abs(expected[i]));
        if (error > abs_tol + rel_tol * scale) {
            std::cerr << "mismatch at row " << i << ": expected=" << std::setprecision(17)
                      << expected[i] << ", actual=" << actual[i] << ", error=" << error
                      << '\n';
            return false;
        }
    }
    return true;
}

template <typename Spmv>
static double benchmark_ms(Spmv&& spmv, const std::vector<double>& reference,
                           std::vector<double>& output, int repeats) {
    spmv(output);
    if (!check_result(reference, output)) {
        throw std::runtime_error("correctness check failed");
    }

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < repeats; ++i) {
        spmv(output);
    }
    const auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(stop - start).count() / repeats;
}

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        const CsrMatrix matrix = make_skewed_matrix(options);
        const std::vector<double> x = make_vector(options.cols, options.seed);
        const std::size_t nnz = matrix.values.size();
        const std::vector<double> zero(matrix.rows, 0.0);
        std::vector<double> reference = zero;
        std::vector<double> output = zero;

        spmv_serial(matrix, x, reference);

        std::cout << "CSR SpMV skewed-matrix experiment\n"
                  << "rows=" << matrix.rows << ", cols=" << matrix.cols
                  << ", nnz=" << nnz << ", long_rows=" << options.long_rows
                  << ", long_nnz=" << options.long_nnz << ", seed=" << options.seed
                  << ", requested_threads=" << options.threads << '\n';
#ifdef _OPENMP
        std::cout << "OpenMP enabled, _OPENMP=" << _OPENMP << '\n';
        omp_set_num_threads(options.threads);
#else
        std::cout << "OpenMP disabled: compile with -fopenmp to enable it.\n";
#endif

        std::cout << std::fixed << std::setprecision(3);
        if (options.only == "all" || options.only == "serial") {
            const double serial_ms = benchmark_ms(
                [&](std::vector<double>& y) { spmv_serial(matrix, x, y); },
                reference, output, options.repeats);
            std::cout << "serial                  " << serial_ms << " ms\n";
        }
        if (options.only == "all" || options.only == "thread") {
            const double thread_ms = benchmark_ms(
                [&](std::vector<double>& y) {
                    spmv_std_thread(matrix, x, y, options.threads);
                },
                reference, output, options.repeats);
            std::cout << "std::thread             " << thread_ms << " ms\n";
        }
        if (options.only == "all" || options.only == "omp1") {
            const double openmp_1_ms = benchmark_ms(
                [&](std::vector<double>& y) {
                    spmv_omp_student_1(matrix, x, y);
                },
                reference, output, options.repeats);
            std::cout << "OpenMP 1 (runtime schedule)  " << openmp_1_ms << " ms\n";
        }
        if (options.only == "all" || options.only == "omp2") {
            const double openmp_2_ms = benchmark_ms(
                [&](std::vector<double>& y) {
                    spmv_omp_student_2(matrix, x, y);
                },
                reference, output, options.repeats);
            std::cout << "OpenMP 2 (long-row)          " << openmp_2_ms << " ms\n";
        }
        std::cout << "All correctness checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        usage(argv[0]);
        return 1;
    }
    return 0;
}
