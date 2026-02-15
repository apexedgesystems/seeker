/**
 * @file PcieStatus.cpp
 * @brief PCIe link status collection via sysfs.
 * @note Reads /sys/bus/pci/devices/ for link width, speed, and NUMA node.
 */

#include "src/gpu/inc/PcieStatus.hpp"

#include <cstdlib>    // std::atoi
#include <filesystem> // std::filesystem
#include <fstream>    // std::ifstream

#include <fmt/core.h>

#include "src/gpu/inc/compat_cuda_detect.hpp"
#if COMPAT_CUDA_AVAILABLE
#include <cuda_runtime.h>
#endif

namespace fs = std::filesystem;

namespace seeker {

namespace gpu {

namespace {

/* ----------------------------- Constants ----------------------------- */

constexpr const char* PCI_PATH = "/sys/bus/pci/devices";

/* ----------------------------- File Helpers ----------------------------- */

inline bool pathExists(const fs::path& path) noexcept {
  std::error_code ec;
  return fs::exists(path, ec);
}

inline std::string readLine(const fs::path& path) noexcept {
  std::ifstream file(path);
  if (!file) {
    return {};
  }
  std::string line;
  std::getline(file, line);
  return line;
}

inline int readInt(const fs::path& path, int defaultVal = 0) noexcept {
  std::ifstream file(path);
  if (!file) {
    return defaultVal;
  }
  int value = defaultVal;
  file >> value;
  return file ? value : defaultVal;
}

/* ----------------------------- PCI Helpers ----------------------------- */

/// Parse BDF string to components.
inline void parseBdf(const std::string& bdf, int& domain, int& bus, int& device, int& function) {
  domain = bus = device = function = 0;

  // Format: "0000:65:00.0" or "65:00.0"
  std::size_t colonPos = bdf.find(':');
  if (colonPos == std::string::npos) {
    return;
  }

  std::size_t secondColon = bdf.find(':', colonPos + 1);
  if (secondColon != std::string::npos) {
    // Has domain
    domain = static_cast<int>(std::strtol(bdf.c_str(), nullptr, 16));
    bus = static_cast<int>(std::strtol(bdf.c_str() + colonPos + 1, nullptr, 16));
    colonPos = secondColon;
  } else {
    bus = static_cast<int>(std::strtol(bdf.c_str(), nullptr, 16));
  }

  std::size_t dotPos = bdf.find('.', colonPos);
  if (dotPos != std::string::npos) {
    device = static_cast<int>(std::strtol(bdf.c_str() + colonPos + 1, nullptr, 16));
    function = static_cast<int>(std::strtol(bdf.c_str() + dotPos + 1, nullptr, 16));
  }
}

/// Query PCIe status from sysfs.
inline PcieStatus querySysfsPcie(const std::string& bdf) noexcept {
  PcieStatus status{};
  status.bdf = bdf;

  parseBdf(bdf, status.domain, status.bus, status.device, status.function);

  const fs::path DEV_PATH = fs::path(PCI_PATH) / bdf;
  if (!pathExists(DEV_PATH)) {
    return status;
  }

  // Current link status
  status.currentWidth = readInt(DEV_PATH / "current_link_width", 0);
  status.currentSpeed = readLine(DEV_PATH / "current_link_speed");
  status.currentGen = parsePcieGeneration(status.currentSpeed);

  // Maximum capability
  status.maxWidth = readInt(DEV_PATH / "max_link_width", 0);
  status.maxSpeed = readLine(DEV_PATH / "max_link_speed");
  status.maxGen = parsePcieGeneration(status.maxSpeed);

  // NUMA node
  status.numaNode = readInt(DEV_PATH / "numa_node", -1);

  return status;
}

#if COMPAT_CUDA_AVAILABLE
/// Get BDF for a CUDA device.
inline std::string getCudaBdf(int deviceIndex) noexcept {
  std::array<char, 32> bdf{};
  if (cudaDeviceGetPCIBusId(bdf.data(), static_cast<int>(bdf.size()), deviceIndex) == cudaSuccess) {
    return bdf.data();
  }
  return {};
}
#endif

} // namespace

/* ----------------------------- PcieGeneration Helpers ----------------------------- */

int pcieBandwidthPerLaneMBps(PcieGeneration gen) noexcept {
  switch (gen) {
  case PcieGeneration::Gen1:
    return 250; // 2.5 GT/s, 8b/10b
  case PcieGeneration::Gen2:
    return 500; // 5.0 GT/s, 8b/10b
  case PcieGeneration::Gen3:
    return 985; // 8.0 GT/s, 128b/130b
  case PcieGeneration::Gen4:
    return 1969; // 16.0 GT/s, 128b/130b
  case PcieGeneration::Gen5:
    return 3938; // 32.0 GT/s, 128b/130b
  case PcieGeneration::Gen6:
    return 7563; // 64.0 GT/s, 242b/256b (PAM4)
  default:
    return 0;
  }
}

PcieGeneration parsePcieGeneration(const std::string& speed) noexcept {
  if (speed.empty()) {
    return PcieGeneration::Unknown;
  }

  // Parse GT/s value from string like "16.0 GT/s" or "8 GT/s"
  double gts = 0.0;
  std::size_t pos = 0;
  try {
    gts = std::stod(speed, &pos);
  } catch (...) {
    return PcieGeneration::Unknown;
  }

  if (gts >= 60.0)
    return PcieGeneration::Gen6;
  if (gts >= 30.0)
    return PcieGeneration::Gen5;
  if (gts >= 14.0)
    return PcieGeneration::Gen4;
  if (gts >= 7.0)
    return PcieGeneration::Gen3;
  if (gts >= 4.0)
    return PcieGeneration::Gen2;
  if (gts >= 2.0)
    return PcieGeneration::Gen1;

  return PcieGeneration::Unknown;
}

/* ----------------------------- PcieStatus ----------------------------- */

bool PcieStatus::isAtMaxLink() const noexcept {
  return currentWidth == maxWidth && currentGen == maxGen;
}

int PcieStatus::theoreticalBandwidthMBps() const noexcept {
  return pcieBandwidthPerLaneMBps(maxGen) * maxWidth;
}

int PcieStatus::currentBandwidthMBps() const noexcept {
  return pcieBandwidthPerLaneMBps(currentGen) * currentWidth;
}

std::string PcieStatus::toString() const {
  return fmt::format("{}: x{} @ {} (max: x{} @ {}), NUMA {}", bdf.empty() ? "unknown" : bdf,
                     currentWidth, currentSpeed.empty() ? "?" : currentSpeed, maxWidth,
                     maxSpeed.empty() ? "?" : maxSpeed, numaNode);
}

/* ----------------------------- API ----------------------------- */

PcieStatus getPcieStatus(int deviceIndex) noexcept {
  PcieStatus status{};
  status.deviceIndex = deviceIndex;

#if COMPAT_CUDA_AVAILABLE
  const std::string BDF = getCudaBdf(deviceIndex);
  if (!BDF.empty()) {
    status = querySysfsPcie(BDF);
    status.deviceIndex = deviceIndex;
    return status;
  }
#endif

  (void)deviceIndex;
  return status;
}

PcieStatus getPcieStatusByBdf(const std::string& bdf) noexcept { return querySysfsPcie(bdf); }

std::vector<PcieStatus> getAllPcieStatus() noexcept {
  std::vector<PcieStatus> result;

#if COMPAT_CUDA_AVAILABLE
  int count = 0;
  if (cudaGetDeviceCount(&count) == cudaSuccess && count > 0) {
    result.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      result.push_back(getPcieStatus(i));
    }
  }
#endif

  return result;
}

} // namespace gpu

} // namespace seeker
