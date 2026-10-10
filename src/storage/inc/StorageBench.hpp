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
 *  - Bounded execution time: a benchmark's wall time, setup and syncs
 *    included, stays within its time budget plus one I/O and one sync of at
 *    most the larger of SYNC_INTERVAL_BYTES and ioSize
 *  - Configurable I/O sizes and patterns
 *  - Minimal setup/teardown overhead
 *  - Syncs only the benchmark's own file; no system-wide sync
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

/// Default time budget per benchmark (seconds); the initial timeBudgetSec.
inline constexpr double MAX_BENCH_TIME_SEC = 30.0;

/// Sync interval for synced benchmark writes (4 MiB): setup writes always,
/// measured sequential writes when useFsync is set. A write that would take
/// the unsynced bytes past this is preceded by a sync, so no sync flushes more
/// than the larger of this and the I/O size; that bounds how long one sync
/// takes on slow media such as SD cards.
inline constexpr std::size_t SYNC_INTERVAL_BYTES = 4 * 1024 * 1024;

/* ----------------------------- BenchConfig ----------------------------- */

/**
 * @brief Configuration for storage benchmarks.
 *
 * Time budget: each benchmark's wall time, setup and every sync included, is
 * at most timeBudgetSec plus one I/O and one sync of at most the larger of
 * SYNC_INTERVAL_BYTES and ioSize; a suite run is at most five times that.
 * The sequential-read, random-read and random-write benchmarks first write
 * their file within half the budget and then run on the bytes actually
 * written.
 *
 * Data size: benchmarks write whole blocks (ioSize, or 4K for the random
 * benchmarks' files) and never past dataSize; the budget can stop them first.
 *
 * useFsync syncs the measured writes: sequential writes before the unsynced
 * bytes would pass SYNC_INTERVAL_BYTES (a larger block is synced on its own)
 * and once at the end, random writes after each write. Setup writes are
 * always synced.
 */
struct BenchConfig {
  std::array<char, BENCH_PATH_SIZE> directory{}; ///< Directory to run benchmarks in
  std::size_t ioSize{DEFAULT_IO_SIZE};           ///< I/O block size in bytes
  std::size_t dataSize{DEFAULT_DATA_SIZE};       ///< Most data per benchmark, in whole blocks
  std::size_t iterations{DEFAULT_ITERATIONS};    ///< Iterations for latency tests
  double timeBudgetSec{MAX_BENCH_TIME_SEC};      ///< Wall-time budget per benchmark (seconds)
  bool useDirectIo{false};                       ///< Use O_DIRECT (bypass page cache)
  bool useFsync{true};                           ///< Sync the measured writes

  /// @brief Set directory path.
  void setDirectory(const char* path) noexcept;

  /// @brief Validate configuration.
  /// @return true if configuration is valid.
  [[nodiscard]] bool isValid() const noexcept;
};

/* ----------------------------- BenchResult ----------------------------- */

/**
 * @brief Result from a single benchmark operation.
 *
 * elapsedSec and the throughput cover the measured phase, including the syncs
 * it issues. A setup that writes the benchmark's file first counts against
 * the time budget but not against elapsedSec.
 */
struct BenchResult {
  bool success{false};             ///< Benchmark completed successfully
  double elapsedSec{0.0};          ///< Measured phase duration, syncs included
  std::size_t operations{0};       ///< Number of operations completed
  std::size_t bytesTransferred{0}; ///< Total bytes transferred

  // Derived metrics (computed after benchmark)
  double throughputBytesPerSec{0.0}; ///< bytesTransferred / elapsedSec (bytes/second)
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
 * Creates a temporary file and writes whole ioSize blocks, never past
 * dataSize, until the time budget runs out. With useFsync it syncs before the
 * unsynced bytes would pass SYNC_INTERVAL_BYTES (a larger block is synced on
 * its own) and once at the end, inside the measured time, so the throughput
 * includes the cost of making the data durable. File is deleted after
 * benchmark.
 */
[[nodiscard]] BenchResult runSeqWriteBench(const BenchConfig& config) noexcept;

/**
 * @brief Run sequential read throughput benchmark.
 * @param config Benchmark configuration.
 * @return Benchmark result with throughput metrics.
 * @note NOT RT-safe: Performs file I/O.
 *
 * Setup writes whole ioSize blocks, never past dataSize, within half the time
 * budget, syncs them and asks the kernel to drop the file's cached pages so
 * the reads reach the device. That request is best effort: where the kernel
 * keeps the pages (tmpfs, for one), reads come from memory. The measured
 * phase then reads the bytes actually written, sequentially, until the
 * budget runs out.
 * Measures read throughput only (excludes setup write time).
 */
[[nodiscard]] BenchResult runSeqReadBench(const BenchConfig& config) noexcept;

/**
 * @brief Run fsync latency benchmark.
 * @param config Benchmark configuration.
 * @return Benchmark result with latency statistics.
 * @note NOT RT-safe: Performs file I/O with sync.
 *
 * Writes small blocks and measures fsync latency for each, until iterations
 * are done or the time budget runs out.
 * Provides min/max/avg/p99 latency statistics.
 */
[[nodiscard]] BenchResult runFsyncBench(const BenchConfig& config) noexcept;

/**
 * @brief Run random read latency benchmark.
 * @param config Benchmark configuration.
 * @return Benchmark result with latency statistics.
 * @note NOT RT-safe: Performs random file I/O.
 *
 * Setup writes whole 4K blocks, never past dataSize, within half the time
 * budget, syncs them and asks the kernel to drop the file's cached pages so
 * the reads reach the device. That request is best effort: where the kernel
 * keeps the pages (tmpfs, for one), reads come from memory. The measured
 * phase then performs random 4K reads within the bytes actually written,
 * until iterations are done or the budget runs out.
 * Measures read latency distribution.
 */
[[nodiscard]] BenchResult runRandReadBench(const BenchConfig& config) noexcept;

/**
 * @brief Run random write latency benchmark.
 * @param config Benchmark configuration.
 * @return Benchmark result with latency statistics.
 * @note NOT RT-safe: Performs random file I/O.
 *
 * Setup writes whole 4K blocks, never past dataSize, within half the time
 * budget and syncs them; the measured phase then performs random 4K writes
 * within the bytes actually written, each followed by fdatasync when useFsync
 * is set, until iterations are done or the budget runs out.
 * Measures write+sync latency distribution.
 */
[[nodiscard]] BenchResult runRandWriteBench(const BenchConfig& config) noexcept;

/**
 * @brief Run complete benchmark suite.
 * @param config Benchmark configuration.
 * @return Suite of all benchmark results.
 * @note NOT RT-safe: Performs extensive file I/O.
 *
 * Each benchmark gets its own time budget, so the suite takes at most five
 * times the per-benchmark bound. Runs all benchmarks in sequence:
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