#ifndef SEEKER_GPU_PCIE_STATUS_HPP
#define SEEKER_GPU_PCIE_STATUS_HPP
/**
 * @file PcieStatus.hpp
 * @brief PCIe link status for GPU devices.
 * @note Linux-only. Queries via sysfs (/sys/bus/pci/devices/).
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <cstdint> // std::uint64_t
#include <string>  // std::string
#include <vector>  // std::vector

namespace seeker {

namespace gpu {

/* ----------------------------- PcieGeneration ----------------------------- */

/**
 * @brief PCIe generation enumeration.
 */
enum class PcieGeneration : int {
  Unknown = 0,
  Gen1 = 1, ///< 2.5 GT/s
  Gen2 = 2, ///< 5.0 GT/s
  Gen3 = 3, ///< 8.0 GT/s
  Gen4 = 4, ///< 16.0 GT/s
  Gen5 = 5, ///< 32.0 GT/s
  Gen6 = 6  ///< 64.0 GT/s
};

/**
 * @brief Get theoretical bandwidth per lane for a PCIe generation (MB/s).
 * @note RT-safe: Pure computation.
 */
[[nodiscard]] int pcieBandwidthPerLaneMBps(PcieGeneration gen) noexcept;

/**
 * @brief Parse PCIe speed string to generation.
 * @param speed Speed string (e.g., "8.0 GT/s", "16 GT/s").
 * @note RT-safe: No allocation.
 */
[[nodiscard]] PcieGeneration parsePcieGeneration(const std::string& speed) noexcept;

/* ----------------------------- PcieStatus ----------------------------- */

/**
 * @brief PCIe link status for a GPU device.
 */
struct PcieStatus {
  int deviceIndex{-1}; ///< GPU ordinal (0-based)

  // PCI address
  std::string bdf; ///< Bus:Device.Function (e.g., "0000:65:00.0")
  int domain{0};   ///< PCI domain
  int bus{0};      ///< PCI bus number
  int device{0};   ///< PCI device number
  int function{0}; ///< PCI function number

  // Current link status
  int currentWidth{0};                                ///< Current lane width (e.g., 16)
  std::string currentSpeed;                           ///< Current speed string (e.g., "16.0 GT/s")
  PcieGeneration currentGen{PcieGeneration::Unknown}; ///< Current generation

  // Maximum capability
  int maxWidth{0};                                ///< Maximum lane width
  std::string maxSpeed;                           ///< Maximum speed string
  PcieGeneration maxGen{PcieGeneration::Unknown}; ///< Maximum generation

  // NUMA topology
  int numaNode{-1}; ///< Associated NUMA node (-1 if unknown)

  // Error counters (if available)
  std::uint64_t replayCount{0};    ///< PCIe replay counter
  std::uint64_t replayRollover{0}; ///< Replay rollover counter

  // Throughput (if NVML available, in KB/s)
  int txThroughputKBps{0}; ///< TX throughput
  int rxThroughputKBps{0}; ///< RX throughput

  /// @brief Check if link is running at maximum capability.
  [[nodiscard]] bool isAtMaxLink() const noexcept;

  /// @brief Calculate theoretical max bandwidth (MB/s).
  [[nodiscard]] int theoreticalBandwidthMBps() const noexcept;

  /// @brief Calculate current bandwidth (MB/s).
  [[nodiscard]] int currentBandwidthMBps() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query PCIe status for a GPU by CUDA device index.
 * @param deviceIndex GPU ordinal (0-based).
 * @return Populated status; defaults on failure.
 * @note RT-safe for single device query (minimal allocation).
 */
[[nodiscard]] PcieStatus getPcieStatus(int deviceIndex) noexcept;

/**
 * @brief Query PCIe status by PCI BDF string.
 * @param bdf PCI address (e.g., "0000:65:00.0").
 * @return Populated status; defaults on failure.
 * @note RT-safe (minimal allocation).
 */
[[nodiscard]] PcieStatus getPcieStatusByBdf(const std::string& bdf) noexcept;

/**
 * @brief Query PCIe status for all GPUs.
 * @return Vector of status for each GPU.
 * @note NOT RT-safe: Allocates vector.
 */
[[nodiscard]] std::vector<PcieStatus> getAllPcieStatus() noexcept;

} // namespace gpu

} // namespace seeker

#endif // SEEKER_GPU_PCIE_STATUS_HPP
