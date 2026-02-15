#ifndef SEEKER_GPU_DRIVER_STATUS_HPP
#define SEEKER_GPU_DRIVER_STATUS_HPP
/**
 * @file GpuDriverStatus.hpp
 * @brief GPU driver status: versions, persistence mode, compute mode.
 * @note Linux-only. Queries via NVML and CUDA runtime.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <string> // std::string
#include <vector> // std::vector

namespace seeker {

namespace gpu {

/* ----------------------------- ComputeMode ----------------------------- */

/**
 * @brief CUDA compute mode for a GPU.
 */
enum class ComputeMode : int {
  Default = 0,         ///< Multiple contexts allowed
  ExclusiveThread = 1, ///< One context per thread (deprecated)
  Prohibited = 2,      ///< No CUDA contexts allowed
  ExclusiveProcess = 3 ///< One context per process (recommended for RT)
};

/**
 * @brief Convert compute mode to string.
 * @note RT-safe: Returns static string.
 */
[[nodiscard]] const char* toString(ComputeMode mode) noexcept;

/* ----------------------------- GpuDriverStatus ----------------------------- */

/**
 * @brief GPU driver and configuration status.
 */
struct GpuDriverStatus {
  int deviceIndex{-1}; ///< GPU ordinal (0-based)
  std::string name;    ///< Device name

  // Driver versions
  std::string driverVersion; ///< NVIDIA driver version (e.g., "535.104.05")
  int cudaDriverVersion{0};  ///< CUDA driver version (e.g., 12040 = 12.4)
  int cudaRuntimeVersion{0}; ///< CUDA runtime version
  std::string nvmlVersion;   ///< NVML library version

  // Configuration
  bool persistenceMode{false};                   ///< GPU stays initialized between uses
  ComputeMode computeMode{ComputeMode::Default}; ///< CUDA compute mode
  bool accountingEnabled{false};                 ///< Process accounting enabled

  // Environment
  std::string cudaVisibleDevices; ///< CUDA_VISIBLE_DEVICES value
  std::string driverModelCurrent; ///< Current driver model (WDDM/TCC on Windows, N/A on Linux)

  // Inforom versions
  std::string inforomImageVersion; ///< Inforom image version
  std::string inforomOemVersion;   ///< Inforom OEM object version
  std::string vbiosVersion;        ///< VBIOS version

  /// @brief Check if driver versions match (driver >= runtime).
  [[nodiscard]] bool versionsCompatible() const noexcept;

  /// @brief Check if configured for RT use (persistence + exclusive).
  [[nodiscard]] bool isRtReady() const noexcept;

  /// @brief Format CUDA version as string (e.g., "12.4").
  [[nodiscard]] static std::string formatCudaVersion(int version);

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query driver status for a specific GPU.
 * @param deviceIndex GPU ordinal (0-based).
 * @return Populated status; defaults on failure.
 * @note RT-safe for single device query (minimal allocation).
 */
[[nodiscard]] GpuDriverStatus getGpuDriverStatus(int deviceIndex) noexcept;

/**
 * @brief Query driver status for all GPUs.
 * @return Vector of status for each GPU.
 * @note NOT RT-safe: Allocates vector.
 */
[[nodiscard]] std::vector<GpuDriverStatus> getAllGpuDriverStatus() noexcept;

/**
 * @brief Get system-wide CUDA environment info.
 * @return Status with global info (driver version, CUDA_VISIBLE_DEVICES).
 * @note Device-specific fields will be -1/empty.
 */
[[nodiscard]] GpuDriverStatus getSystemGpuDriverInfo() noexcept;

} // namespace gpu

} // namespace seeker

#endif // SEEKER_GPU_DRIVER_STATUS_HPP
