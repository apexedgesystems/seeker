#ifndef SEEKER_GPU_ISOLATION_HPP
#define SEEKER_GPU_ISOLATION_HPP
/**
 * @file GpuIsolation.hpp
 * @brief GPU isolation: MIG, MPS, compute exclusivity, process enumeration.
 * @note Linux-only. Queries via NVML.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <cstdint> // std::uint32_t, std::uint64_t
#include <string>  // std::string
#include <vector>  // std::vector

namespace seeker {

namespace gpu {

/* ----------------------------- MigInstance ----------------------------- */

/**
 * @brief Multi-Instance GPU (MIG) instance descriptor.
 */
struct MigInstance {
  int index{-1};    ///< MIG instance index
  std::string name; ///< Instance profile name
  std::string uuid; ///< Instance UUID

  int smCount{0};               ///< SMs allocated to this instance
  std::uint64_t memoryBytes{0}; ///< Memory allocated (bytes)

  int computeInstanceCount{0}; ///< Number of compute instances

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- GpuProcess ----------------------------- */

/**
 * @brief Process using a GPU.
 */
struct GpuProcess {
  std::uint32_t pid{0};             ///< Process ID
  std::string name;                 ///< Process name (if available)
  std::uint64_t usedMemoryBytes{0}; ///< GPU memory used by process

  enum class Type : int {
    Unknown = 0,
    Compute = 1, ///< CUDA compute process
    Graphics = 2 ///< Graphics/rendering process
  };
  Type type{Type::Unknown}; ///< Process type

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- GpuIsolation ----------------------------- */

/**
 * @brief GPU isolation and multi-tenancy status.
 */
struct GpuIsolation {
  int deviceIndex{-1}; ///< GPU ordinal (0-based)
  std::string name;    ///< Device name

  // Compute mode (from GpuDriverStatus, included for convenience)
  enum class ComputeMode : int {
    Default = 0,         ///< Multiple contexts allowed
    ExclusiveThread = 1, ///< One context per thread
    Prohibited = 2,      ///< No CUDA contexts allowed
    ExclusiveProcess = 3 ///< One context per process (RT recommended)
  };
  ComputeMode computeMode{ComputeMode::Default};

  // MIG (Multi-Instance GPU)
  bool migModeSupported{false};          ///< MIG mode supported by hardware
  bool migModeEnabled{false};            ///< MIG mode currently enabled
  std::vector<MigInstance> migInstances; ///< Active MIG instances

  // MPS (Multi-Process Service)
  bool mpsSupported{false};    ///< MPS supported
  bool mpsServerActive{false}; ///< MPS server running

  // Process enumeration
  int computeProcessCount{0};        ///< Number of compute processes
  int graphicsProcessCount{0};       ///< Number of graphics processes
  std::vector<GpuProcess> processes; ///< List of processes using GPU

  /// @brief Check if GPU is exclusively owned.
  [[nodiscard]] bool isExclusive() const noexcept;

  /// @brief Check if configured for RT isolation.
  [[nodiscard]] bool isRtIsolated() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query isolation status for a specific GPU.
 * @param deviceIndex GPU ordinal (0-based).
 * @return Populated status; defaults on failure.
 * @note NOT RT-safe: Allocates vectors, may query process info.
 */
[[nodiscard]] GpuIsolation getGpuIsolation(int deviceIndex) noexcept;

/**
 * @brief Query isolation status for all GPUs.
 * @return Vector of status for each GPU.
 * @note NOT RT-safe: Allocates, performs I/O.
 */
[[nodiscard]] std::vector<GpuIsolation> getAllGpuIsolation() noexcept;

} // namespace gpu

} // namespace seeker

#endif // SEEKER_GPU_ISOLATION_HPP
