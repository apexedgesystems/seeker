#ifndef SEEKER_NETWORK_LOOPBACK_BENCH_HPP
#define SEEKER_NETWORK_LOOPBACK_BENCH_HPP
/**
 * @file LoopbackBench.hpp
 * @brief Bounded loopback latency and throughput measurement.
 * @note Linux-only. Uses TCP/UDP sockets on localhost.
 * @note Thread-safe: Benchmark functions are stateless and safe to call concurrently.
 *
 * Provides network stack latency and throughput measurements using localhost
 * loopback interface. Useful for validating system configuration and detecting
 * network stack overhead.
 *
 * @warning NOT RT-safe: Spawns threads, performs socket I/O, allocates internally.
 *          Do NOT call from RT threads.
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace network {

/* ----------------------------- Constants ----------------------------- */

/// Maximum latency samples to collect.
inline constexpr std::size_t MAX_LATENCY_SAMPLES = 8192;

/// Default throughput buffer size (64 KB).
inline constexpr std::size_t DEFAULT_THROUGHPUT_BUFFER_SIZE = 65536;

/// Default latency message size (64 bytes).
inline constexpr std::size_t DEFAULT_LATENCY_MESSAGE_SIZE = 64;

/* ----------------------------- LatencyResult ----------------------------- */

/**
 * @brief Latency measurement result with percentiles.
 *
 * All values are in microseconds (us).
 */
struct LatencyResult {
  double minUs{0.0};    ///< Minimum latency
  double maxUs{0.0};    ///< Maximum latency
  double meanUs{0.0};   ///< Mean (average) latency
  double medianUs{0.0}; ///< Median (p50) latency
  double p50Us{0.0};    ///< 50th percentile (same as median)
  double p90Us{0.0};    ///< 90th percentile
  double p95Us{0.0};    ///< 95th percentile
  double p99Us{0.0};    ///< 99th percentile
  double p999Us{0.0};   ///< 99.9th percentile
  double stddevUs{0.0}; ///< Standard deviation

  std::size_t sampleCount{0}; ///< Number of samples collected
  bool success{false};        ///< True if measurement succeeded

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- ThroughputResult ----------------------------- */

/**
 * @brief Throughput measurement result.
 */
struct ThroughputResult {
  double mibPerSec{0.0};             ///< Throughput in MiB/sec
  double mbitsPerSec{0.0};           ///< Throughput in megabits/sec
  std::uint64_t bytesTransferred{0}; ///< Total bytes transferred
  double durationSec{0.0};           ///< Measurement duration (seconds)
  bool success{false};               ///< True if measurement succeeded

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- LoopbackBenchResult ----------------------------- */

/**
 * @brief Combined loopback benchmark result.
 */
struct LoopbackBenchResult {
  LatencyResult tcpLatency;       ///< TCP round-trip latency
  LatencyResult udpLatency;       ///< UDP round-trip latency
  ThroughputResult tcpThroughput; ///< TCP throughput
  ThroughputResult udpThroughput; ///< UDP throughput

  /// @brief Overall success (at least one test succeeded).
  [[nodiscard]] bool anySuccess() const noexcept;

  /// @brief All tests succeeded.
  [[nodiscard]] bool allSuccess() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- LoopbackBenchConfig ----------------------------- */

/**
 * @brief Configuration for loopback benchmark.
 */
struct LoopbackBenchConfig {
  std::chrono::milliseconds totalBudget{1000}; ///< Total time budget
  std::size_t latencyMessageSize{DEFAULT_LATENCY_MESSAGE_SIZE};
  std::size_t throughputBufferSize{DEFAULT_THROUGHPUT_BUFFER_SIZE};
  std::size_t maxLatencySamples{MAX_LATENCY_SAMPLES};
  bool runTcpLatency{true};
  bool runUdpLatency{true};
  bool runTcpThroughput{true};
  bool runUdpThroughput{true};
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Run complete loopback benchmark suite.
 * @param budget Maximum time to spend on all benchmarks.
 * @return Combined results from all benchmark tests.
 * @note NOT RT-safe: Spawns threads, socket I/O, internal allocation.
 *
 * Runs TCP and UDP latency and throughput tests on localhost (127.0.0.1).
 * Time budget is distributed across enabled tests. Individual tests
 * fail gracefully without affecting other tests.
 */
[[nodiscard]] LoopbackBenchResult runLoopbackBench(std::chrono::milliseconds budget) noexcept;

/**
 * @brief Run loopback benchmark with custom configuration.
 * @param config Benchmark configuration.
 * @return Combined results from enabled benchmark tests.
 * @note NOT RT-safe: Spawns threads, socket I/O, internal allocation.
 */
[[nodiscard]] LoopbackBenchResult runLoopbackBench(const LoopbackBenchConfig& config) noexcept;

/**
 * @brief Run TCP latency measurement only.
 * @param budget Time budget for this test.
 * @param messageSize Size of ping message (bytes).
 * @param maxSamples Maximum samples to collect.
 * @return Latency measurement result.
 * @note NOT RT-safe: Socket I/O, internal allocation.
 */
[[nodiscard]] LatencyResult
measureTcpLatency(std::chrono::milliseconds budget,
                  std::size_t messageSize = DEFAULT_LATENCY_MESSAGE_SIZE,
                  std::size_t maxSamples = MAX_LATENCY_SAMPLES) noexcept;

/**
 * @brief Run UDP latency measurement only.
 * @param budget Time budget for this test.
 * @param messageSize Size of ping message (bytes).
 * @param maxSamples Maximum samples to collect.
 * @return Latency measurement result.
 * @note NOT RT-safe: Socket I/O, internal allocation.
 */
[[nodiscard]] LatencyResult
measureUdpLatency(std::chrono::milliseconds budget,
                  std::size_t messageSize = DEFAULT_LATENCY_MESSAGE_SIZE,
                  std::size_t maxSamples = MAX_LATENCY_SAMPLES) noexcept;

/**
 * @brief Run TCP throughput measurement only.
 * @param budget Time budget for this test.
 * @param bufferSize Size of transfer buffer (bytes).
 * @return Throughput measurement result.
 * @note NOT RT-safe: Spawns thread, socket I/O.
 */
[[nodiscard]] ThroughputResult
measureTcpThroughput(std::chrono::milliseconds budget,
                     std::size_t bufferSize = DEFAULT_THROUGHPUT_BUFFER_SIZE) noexcept;

/**
 * @brief Run UDP throughput measurement only.
 * @param budget Time budget for this test.
 * @param bufferSize Size of transfer buffer (bytes, max ~64KB for UDP).
 * @return Throughput measurement result.
 * @note NOT RT-safe: Spawns thread, socket I/O.
 */
[[nodiscard]] ThroughputResult
measureUdpThroughput(std::chrono::milliseconds budget,
                     std::size_t bufferSize = DEFAULT_THROUGHPUT_BUFFER_SIZE) noexcept;

} // namespace network

} // namespace seeker

#endif // SEEKER_NETWORK_LOOPBACK_BENCH_HPP
