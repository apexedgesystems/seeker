#ifndef SEEKER_SYSTEM_CAPABILITY_STATUS_HPP
#define SEEKER_SYSTEM_CAPABILITY_STATUS_HPP
/**
 * @file CapabilityStatus.hpp
 * @brief RT-relevant Linux capability status (Linux).
 * @note Linux-only. Uses capget(2) syscall.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Use cases for RT systems:
 *  - Check CAP_SYS_NICE before setting RT scheduling
 *  - Check CAP_IPC_LOCK before mlock/mlockall
 *  - Audit privileged capabilities at startup
 *
 * Note: Capabilities can be set per-binary with setcap(8):
 *   setcap cap_sys_nice,cap_ipc_lock+ep \<binary\>
 */

#include <cstdint> // std::uint64_t
#include <string>  // std::string

namespace seeker {

namespace system {

/* ----------------------------- Constants ----------------------------- */

// Linux capability bit positions (from linux/capability.h)
// These are stable ABI and won't change.

/// CAP_SYS_NICE bit position.
inline constexpr int CAP_SYS_NICE_BIT = 23;

/// CAP_IPC_LOCK bit position.
inline constexpr int CAP_IPC_LOCK_BIT = 14;

/// CAP_SYS_RAWIO bit position.
inline constexpr int CAP_SYS_RAWIO_BIT = 17;

/// CAP_SYS_RESOURCE bit position.
inline constexpr int CAP_SYS_RESOURCE_BIT = 24;

/// CAP_SYS_ADMIN bit position.
inline constexpr int CAP_SYS_ADMIN_BIT = 21;

/// CAP_NET_ADMIN bit position.
inline constexpr int CAP_NET_ADMIN_BIT = 12;

/// CAP_NET_RAW bit position.
inline constexpr int CAP_NET_RAW_BIT = 13;

/// CAP_SYS_PTRACE bit position.
inline constexpr int CAP_SYS_PTRACE_BIT = 19;

/* ----------------------------- Main Struct ----------------------------- */

/**
 * @brief Linux capability status snapshot for RT systems.
 *
 * Captures RT-relevant capabilities from the effective set.
 * These determine what privileged operations the process can perform.
 */
struct CapabilityStatus {
  /* --- RT-Relevant Capabilities --- */

  /// CAP_SYS_NICE: Set RT scheduling, nice values, CPU affinity.
  bool sysNice{false};

  /// CAP_IPC_LOCK: Lock memory (mlock, mlockall, SHM_LOCK).
  bool ipcLock{false};

  /// CAP_SYS_RAWIO: Direct I/O access (ioperm, iopl).
  bool sysRawio{false};

  /// CAP_SYS_RESOURCE: Override resource limits (rlimits).
  bool sysResource{false};

  /* --- Administrative Capabilities --- */

  /// CAP_SYS_ADMIN: General system administration (catch-all).
  bool sysAdmin{false};

  /// CAP_NET_ADMIN: Network configuration.
  bool netAdmin{false};

  /// CAP_NET_RAW: Raw socket access.
  bool netRaw{false};

  /// CAP_SYS_PTRACE: Trace/debug other processes.
  bool sysPtrace{false};

  /* --- Process State --- */

  /// True if running as root (euid == 0).
  bool isRoot{false};

  /* --- Raw Capability Masks (advanced use) --- */

  /// Effective capability set (first 64 bits).
  std::uint64_t effective{0};

  /// Permitted capability set (first 64 bits).
  std::uint64_t permitted{0};

  /// Inheritable capability set (first 64 bits).
  std::uint64_t inheritable{0};

  /* --- Convenience Checks --- */

  /// @brief Check if RT scheduling is allowed.
  /// @return True if CAP_SYS_NICE is set or running as root.
  [[nodiscard]] bool canUseRtScheduling() const noexcept;

  /// @brief Check if memory locking is allowed.
  /// @return True if CAP_IPC_LOCK is set or running as root.
  [[nodiscard]] bool canLockMemory() const noexcept;

  /// @brief Check if process has elevated privileges.
  /// @return True if root or has CAP_SYS_ADMIN.
  [[nodiscard]] bool isPrivileged() const noexcept;

  /// @brief Check for specific capability in effective set.
  /// @param capBit Capability bit position (0-63).
  /// @return True if capability is in effective set.
  [[nodiscard]] bool hasCapability(int capBit) const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;

  /// @brief Summary of RT-relevant capabilities only.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toRtSummary() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query current process capability status.
 * @return Populated CapabilityStatus structure.
 * @note RT-safe: Bounded syscalls (capget, geteuid), no allocation.
 *
 * Sources:
 *  - capget(2) for capability sets
 *  - geteuid(2) for root check
 */
[[nodiscard]] CapabilityStatus getCapabilityStatus() noexcept;

/**
 * @brief Check for a specific capability.
 * @param capBit Capability bit position (e.g., CAP_SYS_NICE_BIT).
 * @return True if capability is in effective set.
 * @note RT-safe: Single syscall.
 */
[[nodiscard]] bool hasCapability(int capBit) noexcept;

/**
 * @brief Check if running as root.
 * @return True if effective UID is 0.
 * @note RT-safe: Single syscall.
 */
[[nodiscard]] bool isRunningAsRoot() noexcept;

/**
 * @brief Get human-readable capability name.
 * @param capBit Capability bit position.
 * @return Static string name, or "CAP_UNKNOWN" for invalid values.
 * @note RT-safe: Returns pointer to static string.
 */
[[nodiscard]] const char* capabilityName(int capBit) noexcept;

} // namespace system

} // namespace seeker

#endif // SEEKER_SYSTEM_CAPABILITY_STATUS_HPP