#ifndef SEEKER_GPU_MEMORY_STATUS_HPP
#define SEEKER_GPU_MEMORY_STATUS_HPP
/**
 * @file GpuMemoryStatus.hpp
 * @brief GPU memory status: capacity, usage, ECC, retired pages.
 * @note Linux-only. Queries via CUDA runtime and NVML.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <cstdint> // std::uint64_t
#include <string>  // std::string
#include <vector>  // std::vector

namespace seeker {

namespace gpu {

/* ----------------------------- EccErrorCounts ----------------------------- */

/**
 * @brief ECC error counters for a GPU.
 */
struct EccErrorCounts {
  std::uint64_t correctedVolatile{0};    ///< Corrected errors since boot
  std::uint64_t uncorrectedVolatile{0};  ///< Uncorrected errors since boot
  std::uint64_t correctedAggregate{0};   ///< Corrected errors lifetime
  std::uint64_t uncorrectedAggregate{0}; ///< Uncorrected errors lifetime

  /// @brief Check if any uncorrected errors occurred.
  [[nodiscard]] bool hasUncorrected() const noexcept {
    return uncorrectedVolatile > 0 || uncorrectedAggregate > 0;
  }

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- RetiredPages ----------------------------- */

/**
 * @brief Retired page information for a GPU.
 */
struct RetiredPages {
  int singleBitEcc{0};          ///< Pages retired due to single-bit ECC errors
  int doubleBitEcc{0};          ///< Pages retired due to double-bit ECC errors
  bool pendingRetire{false};    ///< Retirement pending (requires reboot)
  bool pendingRemapping{false}; ///< Row remapping pending

  /// @brief Total retired pages.
  [[nodiscard]] int total() const noexcept { return singleBitEcc + doubleBitEcc; }

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- GpuMemoryStatus ----------------------------- */

/**
 * @brief GPU memory status snapshot.
 */
struct GpuMemoryStatus {
  int deviceIndex{-1}; ///< GPU ordinal (0-based)

  // Capacity
  std::uint64_t totalBytes{0}; ///< Total global memory (bytes)
  std::uint64_t freeBytes{0};  ///< Free global memory (bytes)
  std::uint64_t usedBytes{0};  ///< Used global memory (bytes)

  // Memory topology
  int memoryBusWidth{0};    ///< Memory bus width (bits)
  int memoryClockMHz{0};    ///< Current memory clock (MHz)
  int memoryClockMaxMHz{0}; ///< Maximum memory clock (MHz)

  // ECC status
  bool eccSupported{false};  ///< ECC memory supported
  bool eccEnabled{false};    ///< ECC currently enabled
  EccErrorCounts eccErrors;  ///< ECC error counters
  RetiredPages retiredPages; ///< Retired page info

  // BAR (Base Address Register) info
  std::uint64_t bar1Total{0}; ///< BAR1 aperture size (bytes)
  std::uint64_t bar1Used{0};  ///< BAR1 used (bytes)

  /// @brief Calculate memory utilization percentage.
  [[nodiscard]] double utilizationPercent() const noexcept;

  /// @brief Check if memory is healthy (no uncorrected errors).
  [[nodiscard]] bool isHealthy() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query memory status for a specific GPU.
 * @param deviceIndex GPU ordinal (0-based).
 * @return Populated status; defaults on failure.
 * @note NOT RT-safe: May allocate, performs I/O.
 */
[[nodiscard]] GpuMemoryStatus getGpuMemoryStatus(int deviceIndex) noexcept;

/**
 * @brief Query memory status for all GPUs.
 * @return Vector of status for each GPU.
 * @note NOT RT-safe: Allocates, performs I/O.
 */
[[nodiscard]] std::vector<GpuMemoryStatus> getAllGpuMemoryStatus() noexcept;

} // namespace gpu

} // namespace seeker

#endif // SEEKER_GPU_MEMORY_STATUS_HPP
