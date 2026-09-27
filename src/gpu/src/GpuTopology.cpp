/**
 * @file GpuTopology.cpp
 * @brief GPU topology collection via CUDA runtime, NVML, and sysfs.
 * @note NVIDIA GPUs via the CUDA runtime when compiled as CUDA, else NVML; sysfs
 *       PCI display controllers for everything else.
 */

#include "src/gpu/inc/GpuTopology.hpp"

#include <algorithm>   // std::any_of, std::sort
#include <array>       // std::array
#include <cstdio>      // std::sscanf, std::snprintf
#include <cstdlib>     // std::strtoul
#include <filesystem>  // std::filesystem
#include <fstream>     // std::ifstream
#include <string_view> // std::string_view
#include <utility>     // std::pair

#include <fmt/core.h>

#include "src/gpu/inc/compat_cuda_detect.hpp"
#include "src/gpu/inc/compat_nvml_detect.hpp"
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

/// PCI base class for display controllers (VGA, XGA, 3D, other).
constexpr unsigned long PCI_CLASS_DISPLAY = 0x03;

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

/* ----------------------------- PCI Helpers ----------------------------- */

/**
 * @brief Normalize a PCI bus ID to sysfs form ("0000:01:00.0", lowercase).
 * @param busId Bus ID in any of "00000000:01:00.0", "0000:01:00.0", "01:00.0".
 * @param dev Device to receive BDF string and numeric components.
 * @return True if the bus ID parsed.
 */
inline bool setPciAddress(const char* busId, GpuDevice& dev) noexcept {
  unsigned int domain = 0;
  unsigned int bus = 0;
  unsigned int device = 0;
  unsigned int function = 0;
  if (std::sscanf(busId, "%x:%x:%x.%x", &domain, &bus, &device, &function) != 4) {
    domain = 0;
    if (std::sscanf(busId, "%x:%x.%x", &bus, &device, &function) != 3) {
      return false;
    }
  }

  dev.pciDomain = static_cast<int>(domain);
  dev.pciBus = static_cast<int>(bus);
  dev.pciDevice = static_cast<int>(device);
  dev.pciFunction = static_cast<int>(function);

  std::array<char, 32> bdf{};
  std::snprintf(bdf.data(), bdf.size(), "%04x:%02x:%02x.%x", domain & 0xFFFFU, bus & 0xFFU,
                device & 0x1FU, function & 0x7U);
  dev.pciBdf = bdf.data();
  return true;
}

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

/// Query single NVIDIA device via NVML. Fields NVML cannot provide stay zeroed.
inline GpuDevice queryNvmlDevice(nvmlDevice_t device, int deviceIndex) noexcept {
  GpuDevice dev{};
  dev.deviceIndex = deviceIndex;
  dev.vendor = GpuVendor::Nvidia;

  // Device name
  std::array<char, NVML_DEVICE_NAME_BUFFER_SIZE> name{};
  if (nvmlDeviceGetName(device, name.data(), static_cast<unsigned int>(name.size())) ==
      NVML_SUCCESS) {
    dev.name = name.data();
  }

  // UUID
  std::array<char, 96> uuid{};
  if (nvmlDeviceGetUUID(device, uuid.data(), static_cast<unsigned int>(uuid.size())) ==
      NVML_SUCCESS) {
    dev.uuid = uuid.data();
  }

  // Compute capability
  int major = 0;
  int minor = 0;
  if (nvmlDeviceGetCudaComputeCapability(device, &major, &minor) == NVML_SUCCESS) {
    dev.smMajor = major;
    dev.smMinor = minor;
  }

  // Total memory (integrated GPUs may report NOT_SUPPORTED)
  nvmlMemory_t mem{};
  if (nvmlDeviceGetMemoryInfo(device, &mem) == NVML_SUCCESS) {
    dev.totalMemoryBytes = mem.total;
  }

  // PCI address
  nvmlPciInfo_t pci{};
  if (nvmlDeviceGetPciInfo_v3(device, &pci) == NVML_SUCCESS) {
    if (!setPciAddress(pci.busId, dev)) {
      (void)setPciAddress(pci.busIdLegacy, dev);
    }
  }

  return dev;
}

#endif // COMPAT_NVML_AVAILABLE

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

/// Strip "0x" prefix from a sysfs hex ID ("0x10de" -> "10de").
inline std::string stripHexPrefix(const std::string& id) noexcept {
  if (id.size() > 2 && id[0] == '0' && (id[1] == 'x' || id[1] == 'X')) {
    return id.substr(2);
  }
  return id;
}

/**
 * @brief Resolve a DRM card to its PCI device directory.
 * @param drmPath DRM card path (e.g., /sys/class/drm/card0).
 * @return Canonical PCI device path; empty if the card is not a PCI display
 *         controller (platform/virtual devices such as evdi, vc4, host1x).
 */
inline fs::path resolvePciDisplayDevice(const fs::path& drmPath) noexcept {
  std::error_code ec;
  const fs::path DEVICE = fs::canonical(drmPath / "device", ec);
  if (ec) {
    return {};
  }

  const fs::path SUBSYSTEM = fs::canonical(DEVICE / "subsystem", ec);
  if (ec || SUBSYSTEM.filename() != "pci") {
    return {};
  }

  const std::string CLASS = readLine(DEVICE / "class");
  if (CLASS.empty()) {
    return {};
  }
  const unsigned long CLASS_CODE = std::strtoul(CLASS.c_str(), nullptr, 16);
  if ((CLASS_CODE >> 16) != PCI_CLASS_DISPLAY) {
    return {};
  }

  return DEVICE;
}

/// Read kernel driver name from a sysfs device uevent ("DRIVER=i915" -> "i915").
inline std::string readDriverName(const fs::path& devicePath) noexcept {
  std::ifstream file(devicePath / "uevent");
  if (!file) {
    return {};
  }
  constexpr std::string_view PREFIX = "DRIVER=";
  std::string line;
  while (std::getline(file, line)) {
    if (line.compare(0, PREFIX.size(), PREFIX) == 0) {
      return line.substr(PREFIX.size());
    }
  }
  return {};
}

/// Query GPU via sysfs PCI device directory (for non-NVIDIA or fallback).
inline GpuDevice querySysfsDevice(const fs::path& pciDevice, int index) noexcept {
  GpuDevice dev{};
  dev.deviceIndex = index;

  // BDF from canonical PCI path
  if (!setPciAddress(pciDevice.filename().c_str(), dev)) {
    dev.pciBdf = pciDevice.filename().string();
  }

  // Vendor
  const std::string VENDOR = readLine(pciDevice / "vendor");
  dev.vendor = detectVendor(VENDOR);

  // Memory (amdgpu exposes VRAM size)
  const fs::path MEM_INFO = pciDevice / "mem_info_vram_total";
  if (pathExists(MEM_INFO)) {
    std::ifstream file(MEM_INFO);
    if (file) {
      file >> dev.totalMemoryBytes;
    }
  }

  // Name: firmware label if present, else "<vendor> GPU [vvvv:dddd] (driver)"
  const fs::path LABEL = pciDevice / "label";
  if (pathExists(LABEL)) {
    dev.name = readLine(LABEL);
  }
  if (dev.name.empty()) {
    const std::string DEVICE_ID = stripHexPrefix(readLine(pciDevice / "device"));
    const std::string DRIVER = readDriverName(pciDevice);
    dev.name = fmt::format("{} GPU [{}:{}]", seeker::gpu::toString(dev.vendor),
                           stripHexPrefix(VENDOR), DEVICE_ID);
    if (!DRIVER.empty()) {
      dev.name += fmt::format(" ({})", DRIVER);
    }
  }

  return dev;
}

/// Extract numeric suffix of a DRM card name ("card12" -> 12); -1 if not a card.
inline int drmCardNumber(const std::string& name) noexcept {
  constexpr std::string_view PREFIX = "card";
  if (name.size() <= PREFIX.size() || name.compare(0, PREFIX.size(), PREFIX) != 0) {
    return -1;
  }
  int value = 0;
  for (std::size_t i = PREFIX.size(); i < name.size(); ++i) {
    // Rejects connectors such as "card0-HDMI-A-1"
    if (name[i] < '0' || name[i] > '9') {
      return -1;
    }
    value = value * 10 + (name[i] - '0');
  }
  return value;
}

/**
 * @brief Append PCI display controllers from sysfs, skipping known BDFs.
 * @param topo Topology to append to (deviceIndex continues from devices.size()).
 * @note Devices are ordered by DRM card number for deterministic indices.
 */
inline void appendSysfsDevices(GpuTopology& topo) noexcept {
  const fs::path DRM_DIR{DRM_PATH};
  if (!pathExists(DRM_DIR)) {
    return;
  }

  std::vector<std::pair<int, fs::path>> cards;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(DRM_DIR, ec)) {
    const int CARD = drmCardNumber(entry.path().filename().string());
    if (CARD >= 0) {
      cards.emplace_back(CARD, entry.path());
    }
  }
  std::sort(cards.begin(), cards.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  for (const auto& card : cards) {
    const fs::path PCI_DEV = resolvePciDisplayDevice(card.second);
    if (PCI_DEV.empty()) {
      continue;
    }

    GpuDevice dev = querySysfsDevice(PCI_DEV, static_cast<int>(topo.devices.size()));

    // Skip devices already enumerated (NVML) or multiple cards on one function
    const bool DUPLICATE =
        std::any_of(topo.devices.begin(), topo.devices.end(),
                    [&dev](const GpuDevice& d) { return d.pciBdf == dev.pciBdf; });
    if (DUPLICATE) {
      continue;
    }

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
  }
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

  if (deviceIndex < 0) {
    return GpuDevice{};
  }

#if COMPAT_NVML_AVAILABLE
  {
    NvmlSession session;
    nvmlDevice_t device{};
    if (session.valid() && nvmlDeviceGetHandleByIndex_v2(static_cast<unsigned int>(deviceIndex),
                                                         &device) == NVML_SUCCESS) {
      return queryNvmlDevice(device, deviceIndex);
    }
  }
#endif

  // Sysfs devices are indexed after NVIDIA devices; enumerate to keep indices consistent
  GpuTopology topo = getGpuTopology();
  if (deviceIndex < topo.deviceCount) {
    return std::move(topo.devices[static_cast<std::size_t>(deviceIndex)]);
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

#if COMPAT_NVML_AVAILABLE
  {
    // NVIDIA devices first so indices match NVML ordinals used by other modules
    NvmlSession session;
    unsigned int count = 0;
    if (session.valid() && nvmlDeviceGetCount_v2(&count) == NVML_SUCCESS) {
      topo.devices.reserve(count);
      for (unsigned int i = 0; i < count; ++i) {
        nvmlDevice_t device{};
        if (nvmlDeviceGetHandleByIndex_v2(i, &device) != NVML_SUCCESS) {
          continue;
        }
        topo.devices.push_back(queryNvmlDevice(device, static_cast<int>(topo.devices.size())));
        ++topo.nvidiaCount;
      }
    }
  }
#endif

  // Remaining PCI display controllers (AMD/Intel, or NVIDIA when NVML is unavailable)
  appendSysfsDevices(topo);

  topo.deviceCount = static_cast<int>(topo.devices.size());
  return topo;
}

} // namespace gpu

} // namespace seeker
