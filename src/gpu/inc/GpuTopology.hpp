#ifndef SEEKER_GPU_TOPOLOGY_HPP
#define SEEKER_GPU_TOPOLOGY_HPP
/**
 * @file GpuTopology.hpp
 * @brief GPU topology snapshot: device enumeration, SM architecture, capabilities.
 * @note Linux-only. Primary support via CUDA runtime; fallback to sysfs for non-NVIDIA.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <cstddef> // std::size_t
#include <cstdint> // std::uint64_t
#include <string>  // std::string
#include <vector>  // std::vector

namespace seeker {

namespace gpu {

/* ----------------------------- Constants ----------------------------- */

/// Maximum GPU name string length.
inline constexpr std::size_t GPU_NAME_SIZE = 256;

/// Maximum UUID string length.
inline constexpr std::size_t GPU_UUID_SIZE = 48;

/// Maximum PCI BDF string length (e.g., "0000:65:00.0").
inline constexpr std::size_t PCI_BDF_SIZE = 16;

/* ----------------------------- GpuVendor ----------------------------- */

/**
 * @brief GPU vendor enumeration.
 */
enum class GpuVendor : int { Unknown = 0, Nvidia = 1, Amd = 2, Intel = 3 };

/**
 * @brief Convert vendor enum to string.
 * @note RT-safe: No allocation.
 */
[[nodiscard]] const char* toString(GpuVendor vendor) noexcept;

/* ----------------------------- GpuDevice ----------------------------- */

/**
 * @brief Single GPU device topology snapshot.
 */
struct GpuDevice {
  // Identity
  int deviceIndex{-1};                  ///< GPU ordinal (0-based)
  std::string name;                     ///< Device name (e.g., "NVIDIA GeForce RTX 4090")
  std::string uuid;                     ///< Unique device identifier
  GpuVendor vendor{GpuVendor::Unknown}; ///< GPU vendor

  // Architecture (NVIDIA-specific, 0 for others)
  int smMajor{0};    ///< Compute capability major version
  int smMinor{0};    ///< Compute capability minor version
  int smCount{0};    ///< Number of Streaming Multiprocessors
  int coresPerSm{0}; ///< CUDA cores per SM
  int cudaCores{0};  ///< Total CUDA cores (smCount * coresPerSm)

  // Execution limits
  int warpSize{0};           ///< Threads per warp (32 for NVIDIA)
  int maxThreadsPerBlock{0}; ///< Maximum threads per block
  int maxThreadsPerSm{0};    ///< Maximum threads per SM
  int maxBlocksPerSm{0};     ///< Maximum blocks per SM

  // Register/shared memory limits
  int regsPerBlock{0};              ///< Maximum registers per block
  int regsPerSm{0};                 ///< Maximum registers per SM
  std::size_t sharedMemPerBlock{0}; ///< Max shared memory per block (bytes)
  std::size_t sharedMemPerSm{0};    ///< Max shared memory per SM (bytes)

  // Memory topology
  std::uint64_t totalMemoryBytes{0}; ///< Total global memory (bytes)
  int memoryBusWidth{0};             ///< Memory bus width (bits)
  int l2CacheBytes{0};               ///< L2 cache size (bytes)

  // PCI topology
  std::string pciBdf; ///< PCI Bus:Device.Function (e.g., "0000:65:00.0")
  int pciDomain{0};   ///< PCI domain
  int pciBus{0};      ///< PCI bus number
  int pciDevice{0};   ///< PCI device number
  int pciFunction{0}; ///< PCI function number

  // Capabilities
  bool unifiedAddressing{false}; ///< Unified virtual addressing supported
  bool managedMemory{false};     ///< Managed memory supported
  bool concurrentKernels{false}; ///< Concurrent kernel execution
  bool asyncEngines{false};      ///< Async copy engines available

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;

  /// @brief Format compute capability as string (e.g., "8.9").
  /// @note RT-safe: Returns static buffer.
  [[nodiscard]] std::string computeCapability() const;
};

/* ----------------------------- GpuTopology ----------------------------- */

/**
 * @brief System-wide GPU topology snapshot.
 */
struct GpuTopology {
  int deviceCount{0};             ///< Number of GPUs detected
  int nvidiaCount{0};             ///< Number of NVIDIA GPUs
  int amdCount{0};                ///< Number of AMD GPUs
  int intelCount{0};              ///< Number of Intel GPUs
  std::vector<GpuDevice> devices; ///< Per-device topology

  /// @brief Check if any GPUs are available.
  [[nodiscard]] bool hasGpu() const noexcept { return deviceCount > 0; }

  /// @brief Check if CUDA is available.
  [[nodiscard]] bool hasCuda() const noexcept { return nvidiaCount > 0; }

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query single GPU device topology by index.
 * @param deviceIndex GPU ordinal (0-based).
 * @return Populated device info; defaults on failure or invalid index.
 * @note NOT RT-safe: May allocate, performs I/O.
 */
[[nodiscard]] GpuDevice getGpuDevice(int deviceIndex) noexcept;

/**
 * @brief Query all GPU devices on the system.
 * @return Populated topology; empty if no GPUs found.
 * @note NOT RT-safe: Allocates vectors, performs I/O.
 */
[[nodiscard]] GpuTopology getGpuTopology() noexcept;

} // namespace gpu

} // namespace seeker

#endif // SEEKER_GPU_TOPOLOGY_HPP
