#ifndef SEEKER_TIMING_LATENCY_BENCH_HPP
#define SEEKER_TIMING_LATENCY_BENCH_HPP
/**
 * @file LatencyBench.hpp
 * @brief Timer overhead and sleep jitter benchmarks (Linux).
 * @note Linux-only for clock_nanosleep, portable for basic measurements.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides timer latency and sleep jitter characterization for RT systems:
 *  - steady_clock::now() overhead measurement
 *  - sleep_for() jitter analysis with detailed statistics
 *  - Optional clock_nanosleep with TIMER_ABSTIME for reduced jitter
 *  - Optional RT priority elevation for accurate measurements
 *
 * Use these benchmarks during system characterization, not in production code.
 * The results help quantify the timing precision achievable on the platform.
 */

#include <chrono>  // std::chrono types
#include <cstddef> // std::size_t
#include <cstdint> // std::uint64_t
#include <string>  // std::string

namespace seeker {

namespace timing {

/* ----------------------------- Constants ----------------------------- */

/// Maximum number of samples to collect in fixed-size mode.
inline constexpr std::size_t MAX_LATENCY_SAMPLES = 8192;

/// Minimum benchmark budget (enforced floor).
inline constexpr std::chrono::milliseconds MIN_BENCH_BUDGET{50};

/* ----------------------------- LatencyStats ----------------------------- */

/**
 * @brief Sleep latency and jitter statistics.
 *
 * Contains comprehensive statistics from a sleep jitter measurement run.
 * All time values are in nanoseconds unless otherwise noted.
 */
struct LatencyStats {
  std::size_t sampleCount{0}; ///< Number of sleep samples collected

  // Timer overhead
  double nowOverheadNs{0.0}; ///< steady_clock::now() call overhead

  // Sleep statistics (actual sleep durations)
  double targetNs{0.0}; ///< Requested sleep duration
  double minNs{0.0};    ///< Minimum observed sleep
  double maxNs{0.0};    ///< Maximum observed sleep
  double meanNs{0.0};   ///< Mean sleep duration
  double medianNs{0.0}; ///< Median (p50) sleep duration
  double p90Ns{0.0};    ///< 90th percentile
  double p95Ns{0.0};    ///< 95th percentile
  double p99Ns{0.0};    ///< 99th percentile
  double p999Ns{0.0};   ///< 99.9th percentile
  double stdDevNs{0.0}; ///< Standard deviation

  // Measurement metadata
  bool usedAbsoluteTime{false}; ///< True if TIMER_ABSTIME was used
  bool usedRtPriority{false};   ///< True if RT priority was elevated
  int rtPriorityUsed{0};        ///< SCHED_FIFO priority (0 = not elevated)

  /// @brief Mean jitter (mean - target).
  [[nodiscard]] double jitterMeanNs() const noexcept;

  /// @brief 95th percentile jitter (p95 - target).
  [[nodiscard]] double jitterP95Ns() const noexcept;

  /// @brief 99th percentile jitter (p99 - target).
  [[nodiscard]] double jitterP99Ns() const noexcept;

  /// @brief Maximum jitter (max - target).
  [[nodiscard]] double jitterMaxNs() const noexcept;

  /// @brief Minimum undershoot (target - min), positive if woke early.
  [[nodiscard]] double undershootNs() const noexcept;

  /// @brief Check if results indicate good RT behavior (p99 jitter < 100us).
  [[nodiscard]] bool isGoodForRt() const noexcept;

  /// @brief Get RT suitability score (0-100) based on jitter characteristics.
  [[nodiscard]] int rtScore() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- BenchConfig ----------------------------- */

/**
 * @brief Configuration for latency benchmark.
 */
struct BenchConfig {
  std::chrono::milliseconds budget{250};       ///< Total measurement time
  std::chrono::microseconds sleepTarget{1000}; ///< Target sleep duration (default 1ms)
  bool useAbsoluteTime{false};                 ///< Use clock_nanosleep TIMER_ABSTIME
  int rtPriority{0}; ///< SCHED_FIFO priority (0 = don't change, 1-99 = elevate)

  /// @brief Create config for quick measurement.
  [[nodiscard]] static BenchConfig quick() noexcept;

  /// @brief Create config for thorough measurement.
  [[nodiscard]] static BenchConfig thorough() noexcept;

  /// @brief Create config optimized for RT characterization.
  [[nodiscard]] static BenchConfig rtCharacterization() noexcept;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Measure timer latency and sleep jitter.
 * @param config Benchmark configuration.
 * @return LatencyStats with measurement results.
 * @note NOT RT-safe: Active benchmark, may allocate, may change scheduling.
 *
 * This function:
 *  1. Measures steady_clock::now() overhead
 *  2. Samples sleep_for() or clock_nanosleep() over the budget period
 *  3. Computes comprehensive statistics
 *
 * If rtPriority > 0, the thread is temporarily elevated to SCHED_FIFO.
 * Requires CAP_SYS_NICE or root to use RT priority.
 */
[[nodiscard]] LatencyStats measureLatency(const BenchConfig& config) noexcept;

/**
 * @brief Measure timer latency with default configuration.
 * @param budget Total measurement time (minimum 50ms).
 * @return LatencyStats with measurement results.
 * @note NOT RT-safe: Active benchmark.
 *
 * Convenience overload using 1ms sleep target and no RT priority.
 */
[[nodiscard]] LatencyStats measureLatency(std::chrono::milliseconds budget) noexcept;

/**
 * @brief Measure steady_clock::now() overhead only.
 * @param iterations Number of iterations (default 10000).
 * @return Estimated overhead in nanoseconds per call.
 * @note RT-safe after warmup: No allocation in measurement loop.
 */
[[nodiscard]] double measureNowOverhead(std::size_t iterations = 10000) noexcept;

} // namespace timing

} // namespace seeker

#endif // SEEKER_TIMING_LATENCY_BENCH_HPP
