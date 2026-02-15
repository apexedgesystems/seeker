#ifndef SEEKER_SYSTEM_CONTAINER_LIMITS_HPP
#define SEEKER_SYSTEM_CONTAINER_LIMITS_HPP
/**
 * @file ContainerLimits.hpp
 * @brief Container detection and cgroup v1/v2 limits (Linux).
 * @note Linux-only. Reads /sys/fs/cgroup/, /proc/self/cgroup, marker files.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Use cases for RT systems:
 *  - Detect if running in a container at startup
 *  - Check cgroup CPU/memory limits affecting RT behavior
 *  - Identify cgroup version for appropriate tuning
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <cstdint> // std::int64_t, std::uint8_t
#include <string>  // std::string

namespace seeker {

namespace system {

/* ----------------------------- Constants ----------------------------- */

/// Buffer size for cpuset CPU list (e.g., "0-3,8-11").
inline constexpr std::size_t CPUSET_STRING_SIZE = 128;

/// Buffer size for container ID.
inline constexpr std::size_t CONTAINER_ID_SIZE = 80;

/// Buffer size for container runtime name.
inline constexpr std::size_t CONTAINER_RUNTIME_SIZE = 32;

/// Sentinel value for unlimited/unknown limits.
inline constexpr std::int64_t LIMIT_UNLIMITED = -1;

/* ----------------------------- Enums ----------------------------- */

/**
 * @brief cgroup version detected on the system.
 */
enum class CgroupVersion : std::uint8_t {
  UNKNOWN = 0, ///< Could not determine cgroup version
  V1,          ///< cgroup v1 (legacy)
  V2,          ///< cgroup v2 (unified)
  HYBRID,      ///< Both v1 and v2 present (mixed mode)
};

/**
 * @brief Convert CgroupVersion to human-readable string.
 * @param version Cgroup version enum value.
 * @return Static string representation.
 * @note RT-safe: Returns pointer to static string.
 */
[[nodiscard]] const char* toString(CgroupVersion version) noexcept;

/* ----------------------------- Main Struct ----------------------------- */

/**
 * @brief Container detection and cgroup limits snapshot.
 *
 * Captures container presence, cgroup version, and resource limits.
 * All string fields are fixed-size arrays to enable RT-safe collection.
 */
struct ContainerLimits {
  /* --- Container Detection --- */

  /// True if container environment detected.
  bool detected{false};

  /// Container ID (first 64 hex chars, if available).
  std::array<char, CONTAINER_ID_SIZE> containerId{};

  /// Container runtime name (docker, podman, containerd, etc.).
  std::array<char, CONTAINER_RUNTIME_SIZE> runtime{};

  /* --- cgroup Info --- */

  /// Detected cgroup version.
  CgroupVersion cgroupVersion{CgroupVersion::UNKNOWN};

  /* --- CPU Limits --- */

  /// CPU quota in microseconds; LIMIT_UNLIMITED if unlimited.
  std::int64_t cpuQuotaUs{LIMIT_UNLIMITED};

  /// CPU period in microseconds; LIMIT_UNLIMITED if unknown.
  std::int64_t cpuPeriodUs{LIMIT_UNLIMITED};

  /// Allowed CPUs (e.g., "0-3,8-11"); empty if unset.
  std::array<char, CPUSET_STRING_SIZE> cpusetCpus{};

  /* --- Memory Limits --- */

  /// Maximum memory in bytes; LIMIT_UNLIMITED if unlimited.
  std::int64_t memMaxBytes{LIMIT_UNLIMITED};

  /// Current memory usage in bytes; LIMIT_UNLIMITED if unknown.
  std::int64_t memCurrentBytes{LIMIT_UNLIMITED};

  /// Maximum swap in bytes; LIMIT_UNLIMITED if unlimited/unsupported.
  std::int64_t swapMaxBytes{LIMIT_UNLIMITED};

  /* --- PID Limits --- */

  /// Maximum PIDs; LIMIT_UNLIMITED if unlimited.
  std::int64_t pidsMax{LIMIT_UNLIMITED};

  /// Current PID count; LIMIT_UNLIMITED if unknown.
  std::int64_t pidsCurrent{LIMIT_UNLIMITED};

  /* --- Computed Values --- */

  /// @brief Get CPU quota as percentage of one CPU.
  /// @return Quota percentage (e.g., 200.0 = 2 CPUs), or 0 if unlimited.
  [[nodiscard]] double cpuQuotaPercent() const noexcept;

  /// @brief Check if CPU quota is limited.
  [[nodiscard]] bool hasCpuLimit() const noexcept;

  /// @brief Check if memory is limited.
  [[nodiscard]] bool hasMemoryLimit() const noexcept;

  /// @brief Check if PID count is limited.
  [[nodiscard]] bool hasPidLimit() const noexcept;

  /// @brief Check if cpuset restricts available CPUs.
  [[nodiscard]] bool hasCpusetLimit() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Collect container limits from cgroup filesystem.
 * @return Populated ContainerLimits structure.
 * @note RT-safe: Bounded file reads, fixed-size output, no allocation.
 *
 * Sources:
 *  - /.dockerenv, /run/.containerenv - Container markers
 *  - /proc/1/cgroup, /proc/self/cgroup - cgroup membership
 *  - /sys/fs/cgroup/cgroup.controllers - cgroup v2 indicator
 *  - /sys/fs/cgroup/cpu.max, memory.max, etc. - cgroup v2 limits
 *  - /sys/fs/cgroup/cpu/cpu.cfs_quota_us, etc. - cgroup v1 limits
 */
[[nodiscard]] ContainerLimits getContainerLimits() noexcept;

/**
 * @brief Simple container detection without full limit collection.
 * @return True if running in a container.
 * @note RT-safe: File existence checks only.
 *
 * Checks:
 *  - /.dockerenv (Docker)
 *  - /run/.containerenv (Podman)
 *  - /proc/1/cgroup contains container hints
 */
[[nodiscard]] bool isRunningInContainer() noexcept;

/**
 * @brief Detect cgroup version on the system.
 * @return CgroupVersion enum value.
 * @note RT-safe: File existence checks only.
 */
[[nodiscard]] CgroupVersion detectCgroupVersion() noexcept;

} // namespace system

} // namespace seeker

#endif // SEEKER_SYSTEM_CONTAINER_LIMITS_HPP