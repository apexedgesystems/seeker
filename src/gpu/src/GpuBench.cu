/**
 * @file GpuBench.cu
 * @brief GPU benchmarks: bandwidth, latency, allocation timing.
 * @note Requires CUDA. Feature-guarded by COMPAT_CUDA_AVAILABLE.
 */

#include "src/gpu/inc/GpuBench.cuh"

#include <algorithm> // std::max
#include <cstring>   // std::memset

#include <fmt/core.h>

#include "src/gpu/inc/compat_cuda_detect.hpp"

#if COMPAT_CUDA_AVAILABLE
#include <cuda_runtime.h>

/// Empty kernel for launch overhead measurement.
__global__ void emptyKernel() {}

#endif

namespace seeker {

namespace gpu {

namespace {

#if COMPAT_CUDA_AVAILABLE

/* ----------------------------- Benchmark Helpers ----------------------------- */

/// Measure bandwidth for a transfer type.
inline BandwidthResult measureBandwidth(void* dst, void* src, std::size_t size, cudaMemcpyKind kind,
                                        std::chrono::milliseconds budget) noexcept {

  BandwidthResult result{};
  result.transferSizeBytes = size;

  cudaEvent_t start{}, stop{};
  cudaEventCreate(&start);
  cudaEventCreate(&stop);

  const auto DEADLINE = std::chrono::steady_clock::now() + budget;
  double totalBytes = 0.0;
  double totalMs = 0.0;
  int iterations = 0;

  while (std::chrono::steady_clock::now() < DEADLINE) {
    cudaEventRecord(start);
    cudaMemcpyAsync(dst, src, size, kind);
    cudaEventRecord(stop);
    cudaEventSynchronize(stop);

    float ms = 0.0f;
    cudaEventElapsedTime(&ms, start, stop);

    totalBytes += static_cast<double>(size);
    totalMs += static_cast<double>(ms);
    ++iterations;
  }

  cudaEventDestroy(start);
  cudaEventDestroy(stop);

  if (iterations > 0 && totalMs > 0.0) {
    result.iterations = iterations;
    result.bandwidthMiBps = (totalBytes / (1024.0 * 1024.0)) / (totalMs / 1000.0);
    result.latencyUs = (totalMs * 1000.0) / static_cast<double>(iterations);
  }

  return result;
}

/// Measure kernel launch overhead.
inline double measureLaunchOverhead(int iterations) noexcept {
  cudaEvent_t start{}, stop{};
  cudaEventCreate(&start);
  cudaEventCreate(&stop);

  cudaEventRecord(start);
  for (int i = 0; i < iterations; ++i) {
    emptyKernel<<<1, 1>>>();
  }
  cudaEventRecord(stop);
  cudaEventSynchronize(stop);

  float ms = 0.0f;
  cudaEventElapsedTime(&ms, start, stop);

  cudaEventDestroy(start);
  cudaEventDestroy(stop);

  return (static_cast<double>(ms) * 1000.0) / static_cast<double>(iterations);
}

/// Measure allocation timing.
inline double measureDeviceAlloc(std::size_t size, int iterations) noexcept {
  cudaEvent_t start{}, stop{};
  cudaEventCreate(&start);
  cudaEventCreate(&stop);

  double totalMs = 0.0;

  for (int i = 0; i < iterations; ++i) {
    void* ptr = nullptr;

    cudaEventRecord(start);
    cudaError_t err = cudaMalloc(&ptr, size);
    cudaEventRecord(stop);
    cudaEventSynchronize(stop);

    if (err == cudaSuccess && ptr != nullptr) {
      float ms = 0.0f;
      cudaEventElapsedTime(&ms, start, stop);
      totalMs += static_cast<double>(ms);
      cudaFree(ptr);
    }
  }

  cudaEventDestroy(start);
  cudaEventDestroy(stop);

  return (totalMs * 1000.0) / static_cast<double>(iterations);
}

/// Measure pinned allocation timing.
inline double measurePinnedAlloc(std::size_t size, int iterations) noexcept {
  cudaEvent_t start{}, stop{};
  cudaEventCreate(&start);
  cudaEventCreate(&stop);

  double totalMs = 0.0;

  for (int i = 0; i < iterations; ++i) {
    void* ptr = nullptr;

    cudaEventRecord(start);
    cudaError_t err = cudaMallocHost(&ptr, size);
    cudaEventRecord(stop);
    cudaEventSynchronize(stop);

    if (err == cudaSuccess && ptr != nullptr) {
      float ms = 0.0f;
      cudaEventElapsedTime(&ms, start, stop);
      totalMs += static_cast<double>(ms);
      cudaFreeHost(ptr);
    }
  }

  cudaEventDestroy(start);
  cudaEventDestroy(stop);

  return (totalMs * 1000.0) / static_cast<double>(iterations);
}

/// Measure stream creation.
inline double measureStreamCreate(int iterations) noexcept {
  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < iterations; ++i) {
    cudaStream_t stream{};
    cudaStreamCreate(&stream);
    cudaStreamDestroy(stream);
  }

  auto end = std::chrono::steady_clock::now();
  double us = std::chrono::duration<double, std::micro>(end - start).count();
  return us / static_cast<double>(iterations);
}

#endif // COMPAT_CUDA_AVAILABLE

} // namespace

/* ----------------------------- BandwidthResult ----------------------------- */

std::string BandwidthResult::toString() const {
  return fmt::format("{:.1f} MiB/s ({} iters, {:.1f} us/transfer)", bandwidthMiBps, iterations,
                     latencyUs);
}

/* ----------------------------- GpuBenchResult ----------------------------- */

std::string GpuBenchResult::toString() const {
  return fmt::format("[GPU {}] {}\n"
                     "  H2D pinned:   {}\n"
                     "  D2H pinned:   {}\n"
                     "  D2D:          {}\n"
                     "  H2D pageable: {}\n"
                     "  D2H pageable: {}\n"
                     "  Launch:       {:.2f} us\n"
                     "  DeviceAlloc:  {:.2f} us\n"
                     "  PinnedAlloc:  {:.2f} us\n"
                     "  StreamCreate: {:.2f} us\n"
                     "  MaxBlocks/SM: {}",
                     deviceIndex, name, h2d.toString(), d2h.toString(), d2d.toString(),
                     h2dPageable.toString(), d2hPageable.toString(), launchOverheadUs,
                     deviceAllocUs, pinnedAllocUs, streamCreateUs, maxActiveBlocksPerSm);
}

/* ----------------------------- API ----------------------------- */

GpuBenchResult runGpuBench(int deviceIndex, const BenchmarkOptions& options) noexcept {
  GpuBenchResult result{};
  result.deviceIndex = deviceIndex;
  result.budgetMs = options.budget;

#if !COMPAT_CUDA_AVAILABLE
  (void)options;
  return result;
#else
  if (cudaSetDevice(deviceIndex) != cudaSuccess) {
    return result;
  }

  // Get device name
  cudaDeviceProp prop{};
  if (cudaGetDeviceProperties(&prop, deviceIndex) == cudaSuccess) {
    result.name = prop.name;
  }

  const std::size_t SIZE = options.transferSize;

  // Allocate buffers
  void* deviceBuf = nullptr;
  void* pinnedBuf = nullptr;
  void* pageableBuf = nullptr;

  if (cudaMalloc(&deviceBuf, SIZE) != cudaSuccess) {
    return result;
  }

  if (cudaMallocHost(&pinnedBuf, SIZE) != cudaSuccess) {
    cudaFree(deviceBuf);
    return result;
  }

  pageableBuf = std::malloc(SIZE);
  if (pageableBuf == nullptr) {
    cudaFreeHost(pinnedBuf);
    cudaFree(deviceBuf);
    return result;
  }

  // Initialize buffers
  std::memset(pinnedBuf, 0x7F, SIZE);
  std::memset(pageableBuf, 0x7F, SIZE);

  // Calculate budget per test
  const auto PER_TEST_BUDGET = options.budget / 6;

  // Pinned memory benchmarks
  result.h2d =
      measureBandwidth(deviceBuf, pinnedBuf, SIZE, cudaMemcpyHostToDevice, PER_TEST_BUDGET);
  result.d2h =
      measureBandwidth(pinnedBuf, deviceBuf, SIZE, cudaMemcpyDeviceToHost, PER_TEST_BUDGET);
  result.d2d =
      measureBandwidth(deviceBuf, deviceBuf, SIZE, cudaMemcpyDeviceToDevice, PER_TEST_BUDGET);

  // Pageable memory benchmarks
  if (!options.skipPageable) {
    result.h2dPageable =
        measureBandwidth(deviceBuf, pageableBuf, SIZE, cudaMemcpyHostToDevice, PER_TEST_BUDGET);
    result.d2hPageable =
        measureBandwidth(pageableBuf, deviceBuf, SIZE, cudaMemcpyDeviceToHost, PER_TEST_BUDGET);
  }

  // Launch overhead
  result.launchOverheadUs = measureLaunchOverhead(options.launchIterations);
  result.launchIterations = options.launchIterations;

  // Allocation benchmarks
  if (!options.skipAllocation) {
    result.deviceAllocUs = measureDeviceAlloc(SIZE, options.allocIterations);
    result.pinnedAllocUs = measurePinnedAlloc(SIZE, options.allocIterations);
  }

  // Stream benchmarks
  if (!options.skipStreams) {
    result.streamCreateUs = measureStreamCreate(options.allocIterations);
  }

  // Occupancy
  int blocks = 0;
  cudaOccupancyMaxActiveBlocksPerMultiprocessor(&blocks, emptyKernel, 128, 0);
  result.maxActiveBlocksPerSm = blocks;
  result.maxActiveWarpsPerSm = blocks * (128 / prop.warpSize);

  // Cleanup
  std::free(pageableBuf);
  cudaFreeHost(pinnedBuf);
  cudaFree(deviceBuf);

  result.completed = true;
  return result;
#endif
}

GpuBenchResult runGpuBench(int deviceIndex, std::chrono::milliseconds budget) noexcept {
  BenchmarkOptions options{};
  options.budget = budget;
  return runGpuBench(deviceIndex, options);
}

std::vector<GpuBenchResult> runAllGpuBench(const BenchmarkOptions& options) noexcept {
  std::vector<GpuBenchResult> results;

#if COMPAT_CUDA_AVAILABLE
  int count = 0;
  if (cudaGetDeviceCount(&count) == cudaSuccess && count > 0) {
    results.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      results.push_back(runGpuBench(i, options));
    }
  }
#else
  (void)options;
#endif

  return results;
}

} // namespace gpu

} // namespace seeker
