/**
 * @file GpuDriverStatus.cpp
 * @brief GPU driver status collection via NVML and CUDA runtime.
 * @note Queries driver version, persistence mode, compute mode.
 */

#include "src/gpu/inc/GpuDriverStatus.hpp"

#include <array>   // std::array
#include <cstdlib> // std::getenv

#include <fmt/core.h>

#include "src/gpu/inc/compat_cuda_detect.hpp"
#include "src/gpu/inc/compat_nvml_detect.hpp"

#if COMPAT_CUDA_AVAILABLE
#include <cuda_runtime.h>
#endif

namespace seeker {

namespace gpu {

namespace {

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

/// Query driver status for a device via NVML.
inline void queryNvmlDriver(nvmlDevice_t device, GpuDriverStatus& status) noexcept {
  // Device name
  std::array<char, 256> name{};
  if (nvmlDeviceGetName(device, name.data(), static_cast<unsigned int>(name.size())) ==
      NVML_SUCCESS) {
    status.name = name.data();
  }

  // Persistence mode
  nvmlEnableState_t persistence{};
  if (nvmlDeviceGetPersistenceMode(device, &persistence) == NVML_SUCCESS) {
    status.persistenceMode = (persistence == NVML_FEATURE_ENABLED);
  }

  // Compute mode
  nvmlComputeMode_t mode{};
  if (nvmlDeviceGetComputeMode(device, &mode) == NVML_SUCCESS) {
    switch (mode) {
    case NVML_COMPUTEMODE_DEFAULT:
      status.computeMode = ComputeMode::Default;
      break;
    case NVML_COMPUTEMODE_EXCLUSIVE_THREAD:
      status.computeMode = ComputeMode::ExclusiveThread;
      break;
    case NVML_COMPUTEMODE_PROHIBITED:
      status.computeMode = ComputeMode::Prohibited;
      break;
    case NVML_COMPUTEMODE_EXCLUSIVE_PROCESS:
      status.computeMode = ComputeMode::ExclusiveProcess;
      break;
    default:
      break;
    }
  }

  // Accounting mode
  nvmlEnableState_t accounting{};
  if (nvmlDeviceGetAccountingMode(device, &accounting) == NVML_SUCCESS) {
    status.accountingEnabled = (accounting == NVML_FEATURE_ENABLED);
  }

  // Inforom versions
  std::array<char, 64> version{};
  if (nvmlDeviceGetInforomImageVersion(device, version.data(),
                                       static_cast<unsigned int>(version.size())) == NVML_SUCCESS) {
    status.inforomImageVersion = version.data();
  }
  if (nvmlDeviceGetInforomVersion(device, NVML_INFOROM_OEM, version.data(),
                                  static_cast<unsigned int>(version.size())) == NVML_SUCCESS) {
    status.inforomOemVersion = version.data();
  }

  // VBIOS version
  if (nvmlDeviceGetVbiosVersion(device, version.data(),
                                static_cast<unsigned int>(version.size())) == NVML_SUCCESS) {
    status.vbiosVersion = version.data();
  }
}

#endif // COMPAT_NVML_AVAILABLE

} // namespace

/* ----------------------------- ComputeMode ----------------------------- */

const char* toString(ComputeMode mode) noexcept {
  switch (mode) {
  case ComputeMode::Default:
    return "Default";
  case ComputeMode::ExclusiveThread:
    return "ExclusiveThread";
  case ComputeMode::Prohibited:
    return "Prohibited";
  case ComputeMode::ExclusiveProcess:
    return "ExclusiveProcess";
  default:
    return "Unknown";
  }
}

/* ----------------------------- GpuDriverStatus ----------------------------- */

bool GpuDriverStatus::versionsCompatible() const noexcept {
  // Driver version should be >= runtime version
  if (cudaDriverVersion == 0 || cudaRuntimeVersion == 0) {
    return true; // Unknown, assume OK
  }
  return cudaDriverVersion >= cudaRuntimeVersion;
}

bool GpuDriverStatus::isRtReady() const noexcept {
  return persistenceMode && computeMode == ComputeMode::ExclusiveProcess;
}

std::string GpuDriverStatus::formatCudaVersion(int version) {
  if (version <= 0) {
    return "unknown";
  }
  const int MAJOR = version / 1000;
  const int MINOR = (version % 1000) / 10;
  return fmt::format("{}.{}", MAJOR, MINOR);
}

std::string GpuDriverStatus::toString() const {
  return fmt::format("[GPU {}] {} - driver: {}, CUDA: {}/{}, persistence: {}, compute: {}",
                     deviceIndex, name, driverVersion, formatCudaVersion(cudaDriverVersion),
                     formatCudaVersion(cudaRuntimeVersion), persistenceMode ? "on" : "off",
                     seeker::gpu::toString(computeMode));
}

/* ----------------------------- API ----------------------------- */

GpuDriverStatus getGpuDriverStatus(int deviceIndex) noexcept {
  GpuDriverStatus status{};
  status.deviceIndex = deviceIndex;

  // CUDA versions
#if COMPAT_CUDA_AVAILABLE
  cudaDriverGetVersion(&status.cudaDriverVersion);
  cudaRuntimeGetVersion(&status.cudaRuntimeVersion);
#endif

  // Environment
  const char* visible = std::getenv("CUDA_VISIBLE_DEVICES");
  if (visible != nullptr) {
    status.cudaVisibleDevices = visible;
  }

#if COMPAT_NVML_AVAILABLE
  NvmlSession session;
  if (!session.valid()) {
    return status;
  }

  // System-wide driver version
  std::array<char, 64> version{};
  if (nvmlSystemGetDriverVersion(version.data(), static_cast<unsigned int>(version.size())) ==
      NVML_SUCCESS) {
    status.driverVersion = version.data();
  }
  if (nvmlSystemGetNVMLVersion(version.data(), static_cast<unsigned int>(version.size())) ==
      NVML_SUCCESS) {
    status.nvmlVersion = version.data();
  }

  // Per-device info
  nvmlDevice_t device{};
  if (nvmlDeviceGetHandleByIndex_v2(static_cast<unsigned int>(deviceIndex), &device) ==
      NVML_SUCCESS) {
    queryNvmlDriver(device, status);
  }
#endif

  return status;
}

std::vector<GpuDriverStatus> getAllGpuDriverStatus() noexcept {
  std::vector<GpuDriverStatus> result;

#if COMPAT_NVML_AVAILABLE
  NvmlSession session;
  if (!session.valid()) {
    return result;
  }

  unsigned int count = 0;
  if (nvmlDeviceGetCount_v2(&count) != NVML_SUCCESS) {
    return result;
  }

  result.reserve(count);
  for (unsigned int i = 0; i < count; ++i) {
    result.push_back(getGpuDriverStatus(static_cast<int>(i)));
  }
#endif

  return result;
}

GpuDriverStatus getSystemGpuDriverInfo() noexcept {
  GpuDriverStatus status{};
  status.deviceIndex = -1;

#if COMPAT_CUDA_AVAILABLE
  cudaDriverGetVersion(&status.cudaDriverVersion);
  cudaRuntimeGetVersion(&status.cudaRuntimeVersion);
#endif

  const char* visible = std::getenv("CUDA_VISIBLE_DEVICES");
  if (visible != nullptr) {
    status.cudaVisibleDevices = visible;
  }

#if COMPAT_NVML_AVAILABLE
  NvmlSession session;
  if (session.valid()) {
    std::array<char, 64> version{};
    if (nvmlSystemGetDriverVersion(version.data(), static_cast<unsigned int>(version.size())) ==
        NVML_SUCCESS) {
      status.driverVersion = version.data();
    }
    if (nvmlSystemGetNVMLVersion(version.data(), static_cast<unsigned int>(version.size())) ==
        NVML_SUCCESS) {
      status.nvmlVersion = version.data();
    }
  }
#endif

  return status;
}

} // namespace gpu

} // namespace seeker
