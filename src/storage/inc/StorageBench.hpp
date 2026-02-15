#ifndef SEEKER_STORAGE_STORAGE_BENCH_HPP
#define SEEKER_STORAGE_STORAGE_BENCH_HPP
/**
 * @file StorageBench.hpp
 * @brief Bounded filesystem micro-benchmarks for storage characterization.
 * @note Linux-only. Performs actual I/O operations.
 * @note Thread-safe: Functions are stateless but perform file I/O.
 *
 * Provides storage performance characterization:
 *  - Sequential read/write throughput
 *  - fsync latency measurement
 *  - Random I/O latency
 *
 * Design goals:
 *  - Bounded execution time via iteration/time limits
 *  - Configurable I/O sizes and patterns
 *  - Minimal setup/teardown overhead
 *
 * @warning NOT RT-safe: Performs active I/O with unbounded latency.
 *          Use only for offline characterization, not in RT paths.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace storage {

/* ----------------------------- Constants ----------------------------- */

/// Maximum path length for benchmark directory.
inline constexpr std::size_t BENCH_PATH_SIZE = 512;

/// Default I/O block size (4 KiB - typical page size).
inline constexpr std::size_t DEFAULT_IO_SIZE = 4096;

/// Default total data size for throughput tests (64 MiB).
inline constexpr std::size_t DEFAULT_DATA_SIZE = 64 * 1024 * 1024;

/// Default number of iterations for latency tests.
inline constexpr std::size_t DEFAULT_ITERATIONS = 1000;

/// Maximum time budget for any single benchmark (seconds).
inline constexpr double MAX_BENCH_TIME_SEC = 30.0;

/* ----------------------------- BenchConfig ----------------------------- */

/**
 * @brief Configuration for storage benchmarks.
 */
struct BenchConfig {
  std::array<char, BENCH_PATH_SIZE> directory{}; ///< Directory to run benchmarks in
  std::size_t ioSize{DEFAULT_IO_SIZE};           ///< I/O block size in bytes
  std::size_t dataSize{DEFAULT_DATA_SIZE};       ///< Total data for throughput tests
  std::size_t iterations{DEFAULT_ITERATIONS};    ///< Iterations for latency tests
  double timeBudgetSec{MAX_BENCH_TIME_SEC};      ///< Max time per benchmark
  bool useDirectIo{false};                       ///< Use O_DIRECT (bypass page cache)
  bool useFsync{true};                           ///< fsync after writes

  /// @brief Set directory path.
  void setDirectory(const char* path) noexcept;

  /// @brief Validate configuration.
  /// @return true if configuration is valid.
  [[nodiscard]] bool isValid() const noexcept;
};

/* ----------------------------- BenchResult ----------------------------- */

/**
 * @brief Result from a single benchmark operation.
 */
struct BenchResult {
  bool success{false};             ///< Benchmark completed successfully
  double elapsedSec{0.0};          ///< Total elapsed time
  std::size_t operations{0};       ///< Number of operations completed
  std::size_t bytesTransferred{0}; ///< Total bytes transferred

  // Derived metrics (computed after benchmark)
  double throughputBytesPerSec{0.0}; ///< Throughput (bytes/second)
  double avgLatencyUs{0.0};          ///< Average latency per operation (us)
  double minLatencyUs{0.0};          ///< Minimum latency observed (us)
  double maxLatencyUs{0.0};          ///< Maximum latency observed (us)
  double p99LatencyUs{0.0};          ///< 99th percentile latency (us)

  /// @brief Get throughput in human-readable format.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string formatThroughput() const;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- BenchSuite ----------------------------- */

/**
 * @brief Complete benchmark suite results.
 */
struct BenchSuite {
  BenchResult seqWrite{};     ///< Sequential write throughput
  BenchResult seqRead{};      ///< Sequential read throughput
  BenchResult fsyncLatency{}; ///< fsync latency
  BenchResult randRead{};     ///< Random read latency (4K)
  BenchResult randWrite{};    ///< Random write latency (4K)

  /// @brief Check if all benchmarks succeeded.
  [[nodiscard]] bool allSuccess() const noexcept;

  /// @brief Human-readable summary of all results.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Run sequential write throughput benchmark.
 * @param config Benchmark configuration.
 * @return Benchmark result with throughput metrics.
 * @note NOT RT-safe: Performs file I/O.
 *
 * Creates a temporary file, writes dataSize bytes in ioSize chunks,
 * and measures throughput. File is deleted after benchmark.
 */
[[nodiscard]] BenchResult runSeqWriteBench(const BenchConfig& config) noexcept;

/**
 * @brief Run sequential read throughput benchmark.
 * @param config Benchmark configuration.
 * @return Benchmark result with throughput metrics.
 * @note NOT RT-safe: Performs file I/O.
 *
 * Creates a file, writes data, then reads it back sequentially.
 * Measures read throughput only (excludes setup write time).
 */
[[nodiscard]] BenchResult runSeqReadBench(const BenchConfig& config) noexcept;

/**
 * @brief Run fsync latency benchmark.
 * @param config Benchmark configuration.
 * @return Benchmark result with latency statistics.
 * @note NOT RT-safe: Performs file I/O with sync.
 *
 * Writes small blocks and measures fsync latency for each.
 * Provides min/max/avg/p99 latency statistics.
 */
[[nodiscard]] BenchResult runFsyncBench(const BenchConfig& config) noexcept;

/**
 * @brief Run random read latency benchmark.
 * @param config Benchmark configuration.
 * @return Benchmark result with latency statistics.
 * @note NOT RT-safe: Performs random file I/O.
 *
 * Creates a file, then performs random 4K reads.
 * Measures read latency distribution.
 */
[[nodiscard]] BenchResult runRandReadBench(const BenchConfig& config) noexcept;

/**
 * @brief Run random write latency benchmark.
 * @param config Benchmark configuration.
 * @return Benchmark result with latency statistics.
 * @note NOT RT-safe: Performs random file I/O.
 *
 * Creates a file, then performs random 4K writes with fsync.
 * Measures write+sync latency distribution.
 */
[[nodiscard]] BenchResult runRandWriteBench(const BenchConfig& config) noexcept;

/**
 * @brief Run complete benchmark suite.
 * @param config Benchmark configuration.
 * @return Suite of all benchmark results.
 * @note NOT RT-safe: Performs extensive file I/O.
 *
 * Runs all benchmarks in sequence:
 *  1. Sequential write
 *  2. Sequential read
 *  3. fsync latency
 *  4. Random read
 *  5. Random write
 */
[[nodiscard]] BenchSuite runBenchSuite(const BenchConfig& config) noexcept;

} // namespace storage

} // namespace seeker

#endif // SEEKER_STORAGE_STORAGE_BENCH_HPP