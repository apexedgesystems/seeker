#ifndef SEEKER_CPU_STATS_HPP
#define SEEKER_CPU_STATS_HPP
/**
 * @file CpuStats.hpp
 * @brief CPU and memory statistics snapshot.
 * @note Linux-only. Sources: sysinfo(2), /proc/version, /proc/cpuinfo, /proc/meminfo.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <array>   // std::array
#include <cstdint> // std::uint64_t
#include <string>  // std::string

namespace seeker {

namespace cpu {

/// Maximum CPU model string length.
inline constexpr std::size_t CPU_MODEL_STRING_SIZE = 128;

/// Maximum kernel version string length.
inline constexpr std::size_t KERNEL_VERSION_STRING_SIZE = 256;

/* ----------------------------- Data Source Structs ----------------------------- */

/**
 * @brief Data from sysinfo(2) syscall.
 */
struct SysinfoData {
  std::uint64_t totalRamBytes{0};  ///< Total RAM (sysinfo.totalram * mem_unit)
  std::uint64_t freeRamBytes{0};   ///< Free RAM (sysinfo.freeram * mem_unit)
  std::uint64_t totalSwapBytes{0}; ///< Total swap; 0 when disabled
  std::uint64_t freeSwapBytes{0};  ///< Free swap
  std::uint64_t uptimeSeconds{0};  ///< Seconds since boot
  int processCount{0};             ///< Number of processes
  double load1{0.0};               ///< 1-minute load average
  double load5{0.0};               ///< 5-minute load average
  double load15{0.0};              ///< 15-minute load average
};

/**
 * @brief Data from /proc/version.
 */
struct KernelVersionData {
  std::array<char, KERNEL_VERSION_STRING_SIZE> version{}; ///< Kernel version string
};

/**
 * @brief Data from /proc/cpuinfo (first CPU entry).
 */
struct CpuInfoData {
  std::array<char, CPU_MODEL_STRING_SIZE> model{}; ///< CPU model name
  long frequencyMhz{0};                            ///< Rounded MHz; 0 if unavailable
};

/**
 * @brief Data from /proc/meminfo.
 */
struct MeminfoData {
  std::uint64_t availableBytes{0}; ///< MemAvailable; 0 if key absent
  bool hasAvailable{false};        ///< True if MemAvailable was present
};

/**
 * @brief Logical CPU count from get_nprocs(3).
 */
struct CpuCountData {
  int count{0}; ///< Logical CPU count (>= 1)
};

/* ----------------------------- Aggregate Snapshot ----------------------------- */

/**
 * @brief Aggregated CPU and memory statistics.
 */
struct CpuStats {
  CpuCountData cpuCount{};    ///< Logical CPU count
  KernelVersionData kernel{}; ///< Kernel version
  CpuInfoData cpuInfo{};      ///< CPU model and frequency
  SysinfoData sysinfo{};      ///< RAM, swap, uptime, load
  MeminfoData meminfo{};      ///< MemAvailable

  /// @brief Human-readable multi-line summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- Individual Readers ----------------------------- */

/**
 * @brief Read sysinfo(2) data.
 * @note RT-safe: Single syscall, no allocation.
 */
[[nodiscard]] SysinfoData readSysinfo() noexcept;

/**
 * @brief Read /proc/version.
 * @note RT-safe: Bounded file read, no allocation.
 */
[[nodiscard]] KernelVersionData readKernelVersion() noexcept;

/**
 * @brief Read /proc/cpuinfo (first CPU entry).
 * @note NOT RT-safe: File size scales with core count.
 */
[[nodiscard]] CpuInfoData readCpuInfo() noexcept;

/**
 * @brief Read MemAvailable from /proc/meminfo.
 * @note RT-safe: Bounded file read, no allocation.
 */
[[nodiscard]] MeminfoData readMeminfo() noexcept;

/**
 * @brief Read logical CPU count via get_nprocs(3).
 * @note RT-safe: Single library call.
 */
[[nodiscard]] CpuCountData readCpuCount() noexcept;

/* ----------------------------- Aggregate API ----------------------------- */

/**
 * @brief Gather all CPU and memory statistics.
 * @return Populated CpuStats snapshot.
 * @note NOT RT-safe: Performs multiple file reads.
 */
[[nodiscard]] CpuStats getCpuStats() noexcept;

} // namespace cpu

} // namespace seeker

#endif // SEEKER_CPU_STATS_HPP