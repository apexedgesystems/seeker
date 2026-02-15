#ifndef SEEKER_GPU_BENCH_CUH
#define SEEKER_GPU_BENCH_CUH
/**
 * @file GpuBench.cuh
 * @brief GPU benchmarks: transfer bandwidth, kernel launch latency, allocation timing.
 * @note Requires CUDA. Feature-guarded by COMPAT_CUDA_AVAILABLE.
 * @note NOT RT-safe: Benchmarks perform active measurements with varying latency.
 */

#include <chrono> // std::chrono::milliseconds
#include <string> // std::string
#include <vector> // std::vector

namespace seeker {

namespace gpu {

/* ----------------------------- BandwidthResult ----------------------------- */

/**
 * @brief Memory transfer bandwidth measurement.
 */
struct BandwidthResult {
  double bandwidthMiBps{0.0};       ///< Bandwidth in MiB/s
  double latencyUs{0.0};            ///< Average latency per transfer (us)
  int iterations{0};                ///< Number of iterations measured
  std::size_t transferSizeBytes{0}; ///< Bytes per transfer

  /// @brief Human-readable summary.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- GpuBenchResult ----------------------------- */

/**
 * @brief Complete GPU benchmark results.
 */
struct GpuBenchResult {
  int deviceIndex{-1}; ///< GPU ordinal (0-based)
  std::string name;    ///< Device name

  // Transfer benchmarks (pinned memory)
  BandwidthResult h2d; ///< Host-to-Device transfer
  BandwidthResult d2h; ///< Device-to-Host transfer
  BandwidthResult d2d; ///< Device-to-Device copy

  // Pageable transfer benchmarks
  BandwidthResult h2dPageable; ///< H2D with pageable host memory
  BandwidthResult d2hPageable; ///< D2H with pageable host memory

  // Kernel launch
  double launchOverheadUs{0.0}; ///< Empty kernel launch overhead (us)
  int launchIterations{0};      ///< Iterations for launch measurement

  // Memory allocation
  double deviceAllocUs{0.0}; ///< cudaMalloc average time (us)
  double pinnedAllocUs{0.0}; ///< cudaMallocHost average time (us)
  double deviceFreeUs{0.0};  ///< cudaFree average time (us)
  double pinnedFreeUs{0.0};  ///< cudaFreeHost average time (us)

  // Occupancy
  int maxActiveBlocksPerSm{0}; ///< Max active blocks (empty kernel)
  int maxActiveWarpsPerSm{0};  ///< Max active warps

  // Stream operations
  double streamCreateUs{0.0}; ///< Stream creation time (us)
  double streamSyncUs{0.0};   ///< Empty stream sync time (us)
  double eventCreateUs{0.0};  ///< Event creation time (us)

  // Benchmark metadata
  std::chrono::milliseconds budgetMs{0}; ///< Time budget used
  bool completed{false};                 ///< All benchmarks completed

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- BenchmarkOptions ----------------------------- */

/**
 * @brief Options for GPU benchmarks.
 */
struct BenchmarkOptions {
  std::chrono::milliseconds budget{1000};     ///< Total time budget
  std::size_t transferSize{64 * 1024 * 1024}; ///< Transfer size (bytes)
  int launchIterations{10000};                ///< Iterations for launch overhead
  int allocIterations{100};                   ///< Iterations for allocation timing

  bool skipPageable{false};   ///< Skip pageable memory benchmarks
  bool skipAllocation{false}; ///< Skip allocation benchmarks
  bool skipStreams{false};    ///< Skip stream/event benchmarks
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Run GPU benchmarks on a specific device.
 * @param deviceIndex GPU ordinal (0-based).
 * @param options Benchmark configuration.
 * @return Benchmark results; defaults if CUDA unavailable or failure.
 * @note NOT RT-safe: Performs active benchmarks with allocation.
 */
[[nodiscard]] GpuBenchResult runGpuBench(int deviceIndex,
                                         const BenchmarkOptions& options = {}) noexcept;

/**
 * @brief Run GPU benchmarks with time budget (simplified API).
 * @param deviceIndex GPU ordinal (0-based).
 * @param budget Time budget for all benchmarks.
 * @return Benchmark results.
 * @note NOT RT-safe: Performs active benchmarks.
 */
[[nodiscard]] GpuBenchResult runGpuBench(int deviceIndex,
                                         std::chrono::milliseconds budget) noexcept;

/**
 * @brief Run benchmarks on all GPUs.
 * @param options Benchmark configuration.
 * @return Vector of results for each GPU.
 * @note NOT RT-safe: Allocates, performs benchmarks.
 */
[[nodiscard]] std::vector<GpuBenchResult>
runAllGpuBench(const BenchmarkOptions& options = {}) noexcept;

} // namespace gpu

} // namespace seeker

#endif // SEEKER_GPU_BENCH_CUH
