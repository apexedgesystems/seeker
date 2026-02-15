/**
 * @file GpuTopology.cpp
 * @brief GPU topology collection via CUDA runtime and sysfs.
 * @note Primary support for NVIDIA via CUDA; fallback to sysfs for AMD/Intel.
 */

#include "src/gpu/inc/GpuTopology.hpp"

#include <array>      // std::array
#include <cstdlib>    // std::getenv
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

/// Sysfs path for DRM (Direct Rendering Manager) devices.
constexpr const char* DRM_PATH = "/sys/class/drm";

/// Sysfs path for PCI devices.
constexpr const char* PCI_PATH = "/sys/bus/pci/devices";

/* ----------------------------- File Helpers ----------------------------- */

/// Check if path exists.
inline bool pathExists(const fs::path& path) noexcept {
  std::error_code ec;
  return fs::exists(path, ec);
}

/// Read first line of a text file.
inline std::string readLine(const fs::path& path) noexcept {
  std::ifstream file(path);
  if (!file) {
    return {};
  }
  std::string line;
  std::getline(file, line);
  return line;
}

/* ----------------------------- CUDA Helpers ----------------------------- */

#if COMPAT_CUDA_AVAILABLE

/// Map SM major.minor to cores per SM.
inline int smToCores(int major, int minor) noexcept {
  const int KEY = (major << 4) | minor;
  // Known SM architectures
  static constexpr std::array<std::pair<int, int>, 21> TABLE{{
      {0x30, 192}, {0x32, 192}, {0x35, 192}, {0x37, 192}, // Kepler
      {0x50, 128}, {0x52, 128}, {0x53, 128},              // Maxwell
      {0x60, 64},  {0x61, 128}, {0x62, 128},              // Pascal
      {0x70, 64},  {0x72, 64},  {0x75, 64},               // Volta/Turing
      {0x80, 64},  {0x86, 128}, {0x87, 128}, {0x89, 128}, // Ampere
      {0x90, 128}, {0x92, 128},                           // Hopper
      {0xa0, 128}, {0xa2, 128},                           // Blackwell
  }};
  for (const auto& kv : TABLE) {
    if (kv.first == KEY) {
      return kv.second;
    }
  }
  // Conservative fallback
  return (major >= 9) ? 128 : 64;
}

/// Query single CUDA device.
inline GpuDevice queryCudaDevice(int deviceIndex) noexcept {
  GpuDevice dev{};
  dev.deviceIndex = deviceIndex;
  dev.vendor = GpuVendor::Nvidia;

  cudaDeviceProp prop{};
  if (cudaGetDeviceProperties(&prop, deviceIndex) != cudaSuccess) {
    return dev;
  }

  dev.name = prop.name;
  dev.smMajor = prop.major;
  dev.smMinor = prop.minor;
  dev.smCount = prop.multiProcessorCount;
  dev.coresPerSm = smToCores(prop.major, prop.minor);
  dev.cudaCores = dev.smCount * dev.coresPerSm;

  dev.warpSize = prop.warpSize;
  dev.maxThreadsPerBlock = prop.maxThreadsPerBlock;
  dev.maxThreadsPerSm = prop.maxThreadsPerMultiProcessor;
  dev.maxBlocksPerSm = prop.maxBlocksPerMultiProcessor;

  dev.regsPerBlock = prop.regsPerBlock;
  dev.regsPerSm = prop.regsPerMultiprocessor;
  dev.sharedMemPerBlock = prop.sharedMemPerBlock;
  dev.sharedMemPerSm = prop.sharedMemPerMultiprocessor;

  dev.totalMemoryBytes = prop.totalGlobalMem;
  dev.memoryBusWidth = prop.memoryBusWidth;
  dev.l2CacheBytes = prop.l2CacheSize;

  dev.pciDomain = prop.pciDomainID;
  dev.pciBus = prop.pciBusID;
  dev.pciDevice = prop.pciDeviceID;
  dev.pciFunction = 0; // CUDA doesn't expose function

  // Get PCI BDF string
  std::array<char, 32> bdf{};
  if (cudaDeviceGetPCIBusId(bdf.data(), static_cast<int>(bdf.size()), deviceIndex) == cudaSuccess) {
    dev.pciBdf = bdf.data();
  } else {
    dev.pciBdf = fmt::format("{:04x}:{:02x}:{:02x}.0", dev.pciDomain, dev.pciBus, dev.pciDevice);
  }

  // Get UUID if available via device attribute
  cudaDeviceProp uuidProp{};
  if (cudaGetDeviceProperties(&uuidProp, deviceIndex) == cudaSuccess) {
    // Format UUID as string
    std::array<char, 64> uuid{};
    std::snprintf(uuid.data(), uuid.size(),
                  "GPU-%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  static_cast<unsigned char>(uuidProp.uuid.bytes[0]),
                  static_cast<unsigned char>(uuidProp.uuid.bytes[1]),
                  static_cast<unsigned char>(uuidProp.uuid.bytes[2]),
                  static_cast<unsigned char>(uuidProp.uuid.bytes[3]),
                  static_cast<unsigned char>(uuidProp.uuid.bytes[4]),
                  static_cast<unsigned char>(uuidProp.uuid.bytes[5]),
                  static_cast<unsigned char>(uuidProp.uuid.bytes[6]),
                  static_cast<unsigned char>(uuidProp.uuid.bytes[7]),
                  static_cast<unsigned char>(uuidProp.uuid.bytes[8]),
                  static_cast<unsigned char>(uuidProp.uuid.bytes[9]),
                  static_cast<unsigned char>(uuidProp.uuid.bytes[10]),
                  static_cast<unsigned char>(uuidProp.uuid.bytes[11]),
                  static_cast<unsigned char>(uuidProp.uuid.bytes[12]),
                  static_cast<unsigned char>(uuidProp.uuid.bytes[13]),
                  static_cast<unsigned char>(uuidProp.uuid.bytes[14]),
                  static_cast<unsigned char>(uuidProp.uuid.bytes[15]));
    dev.uuid = uuid.data();
  }

  // Capabilities
  dev.unifiedAddressing = (prop.unifiedAddressing != 0);
  dev.managedMemory = (prop.managedMemory != 0);
  dev.concurrentKernels = (prop.concurrentKernels != 0);
  dev.asyncEngines = (prop.asyncEngineCount > 0);

  return dev;
}

#endif // COMPAT_CUDA_AVAILABLE

/* ----------------------------- Sysfs Helpers ----------------------------- */

/// Detect GPU vendor from PCI vendor ID.
inline GpuVendor detectVendor(const std::string& vendorId) noexcept {
  if (vendorId.find("10de") != std::string::npos) {
    return GpuVendor::Nvidia;
  }
  if (vendorId.find("1002") != std::string::npos) {
    return GpuVendor::Amd;
  }
  if (vendorId.find("8086") != std::string::npos) {
    return GpuVendor::Intel;
  }
  return GpuVendor::Unknown;
}

/// Query GPU via sysfs (for non-NVIDIA or fallback).
inline GpuDevice querySysfsDevice(const fs::path& drmPath, int index) noexcept {
  GpuDevice dev{};
  dev.deviceIndex = index;

  // Read device symlink to get PCI path
  std::error_code ec;
  const fs::path DEVICE_LINK = drmPath / "device";
  if (!fs::is_symlink(DEVICE_LINK, ec)) {
    return dev;
  }

  const fs::path PCI_PATH_RESOLVED = fs::read_symlink(DEVICE_LINK, ec);
  if (ec) {
    return dev;
  }

  // Extract BDF from path
  const std::string BDF = PCI_PATH_RESOLVED.filename().string();
  dev.pciBdf = BDF;

  // Read vendor
  const fs::path PCI_DEV = fs::path(PCI_PATH) / BDF;
  const std::string VENDOR = readLine(PCI_DEV / "vendor");
  dev.vendor = detectVendor(VENDOR);

  // Read memory (if available)
  const fs::path MEM_INFO = drmPath / "device" / "mem_info_vram_total";
  if (pathExists(MEM_INFO)) {
    std::ifstream file(MEM_INFO);
    if (file) {
      file >> dev.totalMemoryBytes;
    }
  }

  // Try to get device name
  const fs::path PRODUCT = PCI_DEV / "label";
  if (pathExists(PRODUCT)) {
    dev.name = readLine(PRODUCT);
  }

  return dev;
}

} // namespace

/* ----------------------------- GpuVendor ----------------------------- */

const char* toString(GpuVendor vendor) noexcept {
  switch (vendor) {
  case GpuVendor::Nvidia:
    return "NVIDIA";
  case GpuVendor::Amd:
    return "AMD";
  case GpuVendor::Intel:
    return "Intel";
  default:
    return "Unknown";
  }
}

/* ----------------------------- GpuDevice ----------------------------- */

std::string GpuDevice::toString() const {
  return fmt::format("[GPU {}] {} ({}) SM {}.{}: {} SMs, {} cores, {} MiB, PCIe {}", deviceIndex,
                     name, seeker::gpu::toString(vendor), smMajor, smMinor, smCount, cudaCores,
                     totalMemoryBytes / (1024 * 1024), pciBdf);
}

std::string GpuDevice::computeCapability() const { return fmt::format("{}.{}", smMajor, smMinor); }

/* ----------------------------- GpuTopology ----------------------------- */

std::string GpuTopology::toString() const {
  std::string out = fmt::format("GPUs: {} (NVIDIA: {}, AMD: {}, Intel: {})\n", deviceCount,
                                nvidiaCount, amdCount, intelCount);
  for (const auto& dev : devices) {
    out += "  " + dev.toString() + "\n";
  }
  return out;
}

/* ----------------------------- API ----------------------------- */

GpuDevice getGpuDevice(int deviceIndex) noexcept {
#if COMPAT_CUDA_AVAILABLE
  int count = 0;
  if (cudaGetDeviceCount(&count) == cudaSuccess && deviceIndex >= 0 && deviceIndex < count) {
    return queryCudaDevice(deviceIndex);
  }
#endif

  // Fallback to sysfs
  const fs::path DRM_DIR{DRM_PATH};
  if (!pathExists(DRM_DIR)) {
    GpuDevice dev{};
    dev.deviceIndex = deviceIndex;
    return dev;
  }

  std::error_code ec;
  int idx = 0;
  for (const auto& entry : fs::directory_iterator(DRM_DIR, ec)) {
    const std::string NAME = entry.path().filename().string();
    if (NAME.find("card") == 0 && NAME.find('-') == std::string::npos) {
      if (idx == deviceIndex) {
        return querySysfsDevice(entry.path(), deviceIndex);
      }
      ++idx;
    }
  }

  GpuDevice dev{};
  dev.deviceIndex = deviceIndex;
  return dev;
}

GpuTopology getGpuTopology() noexcept {
  GpuTopology topo{};

#if COMPAT_CUDA_AVAILABLE
  int count = 0;
  if (cudaGetDeviceCount(&count) == cudaSuccess && count > 0) {
    topo.devices.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      GpuDevice dev = queryCudaDevice(i);
      if (dev.vendor == GpuVendor::Nvidia) {
        ++topo.nvidiaCount;
      }
      topo.devices.push_back(std::move(dev));
    }
    topo.deviceCount = count;
    return topo;
  }
#endif

  // Fallback: enumerate via sysfs
  const fs::path DRM_DIR{DRM_PATH};
  if (!pathExists(DRM_DIR)) {
    return topo;
  }

  std::error_code ec;
  int idx = 0;
  for (const auto& entry : fs::directory_iterator(DRM_DIR, ec)) {
    const std::string NAME = entry.path().filename().string();
    // Match "card0", "card1", but not "card0-HDMI-A-1"
    if (NAME.find("card") == 0 && NAME.find('-') == std::string::npos) {
      GpuDevice dev = querySysfsDevice(entry.path(), idx);
      switch (dev.vendor) {
      case GpuVendor::Nvidia:
        ++topo.nvidiaCount;
        break;
      case GpuVendor::Amd:
        ++topo.amdCount;
        break;
      case GpuVendor::Intel:
        ++topo.intelCount;
        break;
      default:
        break;
      }
      topo.devices.push_back(std::move(dev));
      ++idx;
    }
  }

  topo.deviceCount = static_cast<int>(topo.devices.size());
  return topo;
}

} // namespace gpu

} // namespace seeker
