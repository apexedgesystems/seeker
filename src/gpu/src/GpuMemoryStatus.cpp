/**
 * @file GpuMemoryStatus.cpp
 * @brief GPU memory status collection via CUDA runtime and NVML.
 * @note Queries memory capacity, ECC status, and retired page info.
 */

#include "src/gpu/inc/GpuMemoryStatus.hpp"

#include <fmt/core.h>

#include "src/gpu/inc/compat_cuda_detect.hpp"
#include "src/gpu/inc/compat_nvml_detect.hpp"

#if COMPAT_CUDA_AVAILABLE
#include <cuda_runtime.h>
#endif

namespace seeker {

namespace gpu {

namespace {

/* ----------------------------- NVML Helpers ----------------------------- */

#if COMPAT_NVML_AVAILABLE

/// RAII wrapper for NVML initialization.
class NvmlSession {
public:
  NvmlSession() noexcept : initialized_(nvmlInit_v2() == NVML_SUCCESS) {}
  ~NvmlSession() {
    if (initialized_)
      nvmlShutdown();
  }

  [[nodiscard]] bool valid() const noexcept { return initialized_; }

  NvmlSession(const NvmlSession&) = delete;
  NvmlSession& operator=(const NvmlSession&) = delete;

private:
  bool initialized_;
};

/// Query ECC and memory info via NVML.
inline void queryNvmlMemory(int deviceIndex, GpuMemoryStatus& status) noexcept {
  nvmlDevice_t device{};
  if (nvmlDeviceGetHandleByIndex_v2(static_cast<unsigned int>(deviceIndex), &device) !=
      NVML_SUCCESS) {
    return;
  }

  // ECC mode
  nvmlEnableState_t current{}, pending{};
  if (nvmlDeviceGetEccMode(device, &current, &pending) == NVML_SUCCESS) {
    status.eccSupported = true;
    status.eccEnabled = (current == NVML_FEATURE_ENABLED);
  }

  // ECC error counts
  unsigned long long count = 0;
  if (nvmlDeviceGetTotalEccErrors(device, NVML_MEMORY_ERROR_TYPE_CORRECTED, NVML_VOLATILE_ECC,
                                  &count) == NVML_SUCCESS) {
    status.eccErrors.correctedVolatile = count;
  }
  if (nvmlDeviceGetTotalEccErrors(device, NVML_MEMORY_ERROR_TYPE_UNCORRECTED, NVML_VOLATILE_ECC,
                                  &count) == NVML_SUCCESS) {
    status.eccErrors.uncorrectedVolatile = count;
  }
  if (nvmlDeviceGetTotalEccErrors(device, NVML_MEMORY_ERROR_TYPE_CORRECTED, NVML_AGGREGATE_ECC,
                                  &count) == NVML_SUCCESS) {
    status.eccErrors.correctedAggregate = count;
  }
  if (nvmlDeviceGetTotalEccErrors(device, NVML_MEMORY_ERROR_TYPE_UNCORRECTED, NVML_AGGREGATE_ECC,
                                  &count) == NVML_SUCCESS) {
    status.eccErrors.uncorrectedAggregate = count;
  }

  // Retired pages
  unsigned int pageCount = 0;
  if (nvmlDeviceGetRetiredPages(device, NVML_PAGE_RETIREMENT_CAUSE_MULTIPLE_SINGLE_BIT_ECC_ERRORS,
                                &pageCount, nullptr) == NVML_SUCCESS) {
    status.retiredPages.singleBitEcc = static_cast<int>(pageCount);
  }
  if (nvmlDeviceGetRetiredPages(device, NVML_PAGE_RETIREMENT_CAUSE_DOUBLE_BIT_ECC_ERROR, &pageCount,
                                nullptr) == NVML_SUCCESS) {
    status.retiredPages.doubleBitEcc = static_cast<int>(pageCount);
  }

  nvmlEnableState_t pending_retire{};
  if (nvmlDeviceGetRetiredPagesPendingStatus(device, &pending_retire) == NVML_SUCCESS) {
    status.retiredPages.pendingRetire = (pending_retire == NVML_FEATURE_ENABLED);
  }

  // BAR1 memory
  nvmlBAR1Memory_t bar1{};
  if (nvmlDeviceGetBAR1MemoryInfo(device, &bar1) == NVML_SUCCESS) {
    status.bar1Total = bar1.bar1Total;
    status.bar1Used = bar1.bar1Used;
  }

  // Memory clock
  unsigned int clock = 0;
  if (nvmlDeviceGetClockInfo(device, NVML_CLOCK_MEM, &clock) == NVML_SUCCESS) {
    status.memoryClockMHz = static_cast<int>(clock);
  }
  if (nvmlDeviceGetMaxClockInfo(device, NVML_CLOCK_MEM, &clock) == NVML_SUCCESS) {
    status.memoryClockMaxMHz = static_cast<int>(clock);
  }
}

#endif // COMPAT_NVML_AVAILABLE

} // namespace

/* ----------------------------- EccErrorCounts ----------------------------- */

std::string EccErrorCounts::toString() const {
  return fmt::format("corrected: {}/{} (volatile/aggregate), uncorrected: {}/{}", correctedVolatile,
                     correctedAggregate, uncorrectedVolatile, uncorrectedAggregate);
}

/* ----------------------------- RetiredPages ----------------------------- */

std::string RetiredPages::toString() const {
  std::string out = fmt::format("SBE: {}, DBE: {}", singleBitEcc, doubleBitEcc);
  if (pendingRetire) {
    out += " [retire pending]";
  }
  if (pendingRemapping) {
    out += " [remapping pending]";
  }
  return out;
}

/* ----------------------------- GpuMemoryStatus ----------------------------- */

double GpuMemoryStatus::utilizationPercent() const noexcept {
  if (totalBytes == 0) {
    return 0.0;
  }
  return 100.0 * static_cast<double>(usedBytes) / static_cast<double>(totalBytes);
}

bool GpuMemoryStatus::isHealthy() const noexcept {
  return !eccErrors.hasUncorrected() && retiredPages.total() == 0;
}

std::string GpuMemoryStatus::toString() const {
  return fmt::format("GPU {}: {}/{} MiB ({:.1f}% used), ECC: {}, errors: {}, retired: {}",
                     deviceIndex, usedBytes / (1024 * 1024), totalBytes / (1024 * 1024),
                     utilizationPercent(), eccEnabled ? "on" : "off", eccErrors.toString(),
                     retiredPages.toString());
}

/* ----------------------------- API ----------------------------- */

GpuMemoryStatus getGpuMemoryStatus(int deviceIndex) noexcept {
  GpuMemoryStatus status{};
  status.deviceIndex = deviceIndex;

#if COMPAT_CUDA_AVAILABLE
  if (cudaSetDevice(deviceIndex) != cudaSuccess) {
    return status;
  }

  // Get memory info
  std::size_t freeBytes = 0, totalBytes = 0;
  if (cudaMemGetInfo(&freeBytes, &totalBytes) == cudaSuccess) {
    status.totalBytes = totalBytes;
    status.freeBytes = freeBytes;
    status.usedBytes = totalBytes - freeBytes;
  }

  // Get memory bus width
  cudaDeviceProp prop{};
  if (cudaGetDeviceProperties(&prop, deviceIndex) == cudaSuccess) {
    status.memoryBusWidth = prop.memoryBusWidth;
  }
#endif

#if COMPAT_NVML_AVAILABLE
  NvmlSession session;
  if (session.valid()) {
    queryNvmlMemory(deviceIndex, status);
  }
#endif

  return status;
}

std::vector<GpuMemoryStatus> getAllGpuMemoryStatus() noexcept {
  std::vector<GpuMemoryStatus> result;

#if COMPAT_CUDA_AVAILABLE
  int count = 0;
  if (cudaGetDeviceCount(&count) == cudaSuccess && count > 0) {
    result.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      result.push_back(getGpuMemoryStatus(i));
    }
  }
#endif

  return result;
}

} // namespace gpu

} // namespace seeker
