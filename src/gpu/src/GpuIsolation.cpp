/**
 * @file GpuIsolation.cpp
 * @brief GPU isolation status collection via NVML.
 * @note Queries MIG, MPS, compute mode, and process enumeration.
 */

#include "src/gpu/inc/GpuIsolation.hpp"

#include <array> // std::array

#include <fmt/core.h>

#include "src/gpu/inc/compat_nvml_detect.hpp"

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

/// Query isolation status for a device via NVML.
inline GpuIsolation queryNvmlIsolation(nvmlDevice_t device, int deviceIndex) noexcept {
  GpuIsolation isolation{};
  isolation.deviceIndex = deviceIndex;

  // Device name
  std::array<char, 256> name{};
  if (nvmlDeviceGetName(device, name.data(), static_cast<unsigned int>(name.size())) ==
      NVML_SUCCESS) {
    isolation.name = name.data();
  }

  // Compute mode
  nvmlComputeMode_t mode{};
  if (nvmlDeviceGetComputeMode(device, &mode) == NVML_SUCCESS) {
    switch (mode) {
    case NVML_COMPUTEMODE_DEFAULT:
      isolation.computeMode = GpuIsolation::ComputeMode::Default;
      break;
    case NVML_COMPUTEMODE_EXCLUSIVE_THREAD:
      isolation.computeMode = GpuIsolation::ComputeMode::ExclusiveThread;
      break;
    case NVML_COMPUTEMODE_PROHIBITED:
      isolation.computeMode = GpuIsolation::ComputeMode::Prohibited;
      break;
    case NVML_COMPUTEMODE_EXCLUSIVE_PROCESS:
      isolation.computeMode = GpuIsolation::ComputeMode::ExclusiveProcess;
      break;
    default:
      break;
    }
  }

  // MIG mode
  unsigned int currentMig = 0, pendingMig = 0;
  nvmlReturn_t migResult = nvmlDeviceGetMigMode(device, &currentMig, &pendingMig);
  if (migResult == NVML_SUCCESS) {
    isolation.migModeSupported = true;
    isolation.migModeEnabled = (currentMig == NVML_DEVICE_MIG_ENABLE);
  } else if (migResult == NVML_ERROR_NOT_SUPPORTED) {
    isolation.migModeSupported = false;
  }

  // MIG instances (if MIG enabled)
  if (isolation.migModeEnabled) {
    unsigned int count = 0;
    if (nvmlDeviceGetMaxMigDeviceCount(device, &count) == NVML_SUCCESS && count > 0) {
      for (unsigned int i = 0; i < count; ++i) {
        nvmlDevice_t migDevice{};
        if (nvmlDeviceGetMigDeviceHandleByIndex(device, i, &migDevice) == NVML_SUCCESS) {
          MigInstance instance{};
          instance.index = static_cast<int>(i);

          // Get MIG device info
          std::array<char, 256> migName{};
          if (nvmlDeviceGetName(migDevice, migName.data(),
                                static_cast<unsigned int>(migName.size())) == NVML_SUCCESS) {
            instance.name = migName.data();
          }

          // Get UUID
          std::array<char, 80> uuid{};
          if (nvmlDeviceGetUUID(migDevice, uuid.data(), static_cast<unsigned int>(uuid.size())) ==
              NVML_SUCCESS) {
            instance.uuid = uuid.data();
          }

          // Get memory
          nvmlMemory_t mem{};
          if (nvmlDeviceGetMemoryInfo(migDevice, &mem) == NVML_SUCCESS) {
            instance.memoryBytes = mem.total;
          }

          isolation.migInstances.push_back(std::move(instance));
        }
      }
    }
  }

  // Process enumeration - compute processes
  unsigned int infoCount = 32;
  std::array<nvmlProcessInfo_t, 32> processes{};
  if (nvmlDeviceGetComputeRunningProcesses(device, &infoCount, processes.data()) == NVML_SUCCESS) {
    isolation.computeProcessCount = static_cast<int>(infoCount);
    for (unsigned int i = 0; i < infoCount && i < processes.size(); ++i) {
      GpuProcess proc{};
      proc.pid = processes[i].pid;
      proc.usedMemoryBytes = processes[i].usedGpuMemory;
      proc.type = GpuProcess::Type::Compute;
      isolation.processes.push_back(std::move(proc));
    }
  }

  // Graphics processes
  infoCount = 32;
  if (nvmlDeviceGetGraphicsRunningProcesses(device, &infoCount, processes.data()) == NVML_SUCCESS) {
    isolation.graphicsProcessCount = static_cast<int>(infoCount);
    for (unsigned int i = 0; i < infoCount && i < processes.size(); ++i) {
      GpuProcess proc{};
      proc.pid = processes[i].pid;
      proc.usedMemoryBytes = processes[i].usedGpuMemory;
      proc.type = GpuProcess::Type::Graphics;
      isolation.processes.push_back(std::move(proc));
    }
  }

  return isolation;
}

#endif // COMPAT_NVML_AVAILABLE

} // namespace

/* ----------------------------- MigInstance ----------------------------- */

std::string MigInstance::toString() const {
  return fmt::format("MIG[{}] {}: {} MiB, {} compute instances", index, name,
                     memoryBytes / (1024 * 1024), computeInstanceCount);
}

/* ----------------------------- GpuProcess ----------------------------- */

std::string GpuProcess::toString() const {
  const char* typeStr = (type == Type::Compute)    ? "compute"
                        : (type == Type::Graphics) ? "graphics"
                                                   : "unknown";
  return fmt::format("PID {} ({}): {} MiB", pid, typeStr, usedMemoryBytes / (1024 * 1024));
}

/* ----------------------------- GpuIsolation ----------------------------- */

bool GpuIsolation::isExclusive() const noexcept {
  return computeMode == ComputeMode::ExclusiveProcess ||
         computeMode == ComputeMode::ExclusiveThread;
}

bool GpuIsolation::isRtIsolated() const noexcept {
  // RT isolation: exclusive mode, no MIG (or properly configured MIG), single process
  if (!isExclusive()) {
    return false;
  }
  if (migModeEnabled && migInstances.empty()) {
    return false; // MIG enabled but no instances configured
  }
  return computeProcessCount <= 1;
}

std::string GpuIsolation::toString() const {
  std::string modeStr;
  switch (computeMode) {
  case ComputeMode::Default:
    modeStr = "default";
    break;
  case ComputeMode::ExclusiveThread:
    modeStr = "exclusive_thread";
    break;
  case ComputeMode::Prohibited:
    modeStr = "prohibited";
    break;
  case ComputeMode::ExclusiveProcess:
    modeStr = "exclusive_process";
    break;
  }

  std::string out = fmt::format(
      "[GPU {}] {} - mode: {}, MIG: {}, processes: {} compute + {} graphics", deviceIndex, name,
      modeStr, migModeEnabled ? "enabled" : "disabled", computeProcessCount, graphicsProcessCount);

  if (!migInstances.empty()) {
    out += fmt::format(" ({} MIG instances)", migInstances.size());
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

GpuIsolation getGpuIsolation(int deviceIndex) noexcept {
  GpuIsolation isolation{};
  isolation.deviceIndex = deviceIndex;

#if COMPAT_NVML_AVAILABLE
  NvmlSession session;
  if (!session.valid()) {
    return isolation;
  }

  nvmlDevice_t device{};
  if (nvmlDeviceGetHandleByIndex_v2(static_cast<unsigned int>(deviceIndex), &device) !=
      NVML_SUCCESS) {
    return isolation;
  }

  isolation = queryNvmlIsolation(device, deviceIndex);
#endif

  return isolation;
}

std::vector<GpuIsolation> getAllGpuIsolation() noexcept {
  std::vector<GpuIsolation> result;

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
    nvmlDevice_t device{};
    if (nvmlDeviceGetHandleByIndex_v2(i, &device) == NVML_SUCCESS) {
      result.push_back(queryNvmlIsolation(device, static_cast<int>(i)));
    }
  }
#endif

  return result;
}

} // namespace gpu

} // namespace seeker
