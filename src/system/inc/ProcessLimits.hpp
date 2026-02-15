#ifndef SEEKER_SYSTEM_PROCESS_LIMITS_HPP
#define SEEKER_SYSTEM_PROCESS_LIMITS_HPP
/**
 * @file ProcessLimits.hpp
 * @brief RT-relevant process resource limits (Linux).
 * @note Linux-only. Uses getrlimit(2) syscall.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Use cases for RT systems:
 *  - Verify RLIMIT_RTPRIO before setting RT scheduling
 *  - Check RLIMIT_MEMLOCK before mlock/mlockall
 *  - Audit all process limits at startup
 */

#include <cstddef> // std::size_t
#include <cstdint> // std::uint64_t, std::int64_t
#include <string>  // std::string

namespace seeker {

namespace system {

/* ----------------------------- Constants ----------------------------- */

/// Value indicating unlimited resource limit.
inline constexpr std::uint64_t RLIMIT_UNLIMITED_VALUE = static_cast<std::uint64_t>(-1);

/* ----------------------------- Single Limit Struct ----------------------------- */

/**
 * @brief Single resource limit value pair (soft/hard).
 *
 * Represents both the current effective limit (soft) and the maximum
 * possible limit (hard) that can be set without privileges.
 */
struct RlimitValue {
  std::uint64_t soft{0}; ///< Current (soft) limit
  std::uint64_t hard{0}; ///< Maximum (hard) limit
  bool unlimited{false}; ///< True if soft limit is RLIM_INFINITY

  /// @brief Check if soft limit can be increased to target value.
  /// @param value Desired limit value.
  /// @return True if value <= hard limit.
  [[nodiscard]] bool canIncreaseTo(std::uint64_t value) const noexcept;

  /// @brief Check if limit allows at least the specified value.
  /// @param value Required minimum value.
  /// @return True if soft >= value or unlimited.
  [[nodiscard]] bool hasAtLeast(std::uint64_t value) const noexcept;
};

/* ----------------------------- Main Struct ----------------------------- */

/**
 * @brief RT-relevant process resource limits snapshot.
 *
 * Captures all rlimits that impact RT system behavior. Use this to validate
 * that an RT application has sufficient privileges before attempting
 * RT scheduling or memory locking.
 */
struct ProcessLimits {
  /* --- RT Scheduling Limits --- */

  /// RLIMIT_RTPRIO: Maximum real-time priority (0 = cannot use RT scheduler).
  RlimitValue rtprio{};

  /// RLIMIT_RTTIME: Maximum RT CPU time in microseconds before SIGXCPU.
  RlimitValue rttime{};

  /// RLIMIT_NICE: Nice value range (affects nice(2) and setpriority(2)).
  RlimitValue nice{};

  /* --- Memory Limits --- */

  /// RLIMIT_MEMLOCK: Maximum locked memory in bytes.
  RlimitValue memlock{};

  /// RLIMIT_AS: Maximum address space (virtual memory) in bytes.
  RlimitValue addressSpace{};

  /// RLIMIT_DATA: Maximum data segment size in bytes.
  RlimitValue dataSegment{};

  /// RLIMIT_STACK: Maximum stack size in bytes.
  RlimitValue stack{};

  /* --- File/Process Limits --- */

  /// RLIMIT_NOFILE: Maximum open file descriptors.
  RlimitValue nofile{};

  /// RLIMIT_NPROC: Maximum number of processes/threads.
  RlimitValue nproc{};

  /// RLIMIT_CORE: Maximum core dump size in bytes (0 = no core dumps).
  RlimitValue core{};

  /// RLIMIT_MSGQUEUE: Maximum bytes in POSIX message queues.
  RlimitValue msgqueue{};

  /* --- Convenience Accessors --- */

  /// @brief Get maximum RT priority from soft limit.
  /// @return Max RT priority (0-99), or 0 if not allowed.
  [[nodiscard]] int rtprioMax() const noexcept;

  /// @brief Check if memlock is effectively unlimited.
  /// @return True if unlimited or >= 16 EiB (practical unlimited).
  [[nodiscard]] bool hasUnlimitedMemlock() const noexcept;

  /// @brief Check if given RT priority is allowed.
  /// @param priority Desired SCHED_FIFO/SCHED_RR priority (1-99).
  /// @return True if priority <= rtprio soft limit.
  [[nodiscard]] bool canUseRtPriority(int priority) const noexcept;

  /// @brief Check if RT scheduling can be used at all.
  /// @return True if rtprio soft limit > 0.
  [[nodiscard]] bool canUseRtScheduling() const noexcept;

  /// @brief Check if requested memory can be locked.
  /// @param bytes Bytes to lock.
  /// @return True if within memlock limit.
  [[nodiscard]] bool canLockMemory(std::uint64_t bytes) const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;

  /// @brief Summary of RT-relevant limits only.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toRtSummary() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Collect all RT-relevant process resource limits.
 * @return Populated ProcessLimits structure.
 * @note RT-safe: Bounded syscalls (getrlimit), no allocation.
 *
 * Sources:
 *  - getrlimit(2) for each RLIMIT_* constant
 */
[[nodiscard]] ProcessLimits getProcessLimits() noexcept;

/**
 * @brief Query a single resource limit.
 * @param resource RLIMIT_* constant (e.g., RLIMIT_RTPRIO).
 * @return RlimitValue with soft/hard limits.
 * @note RT-safe: Single syscall.
 */
[[nodiscard]] RlimitValue getRlimit(int resource) noexcept;

/**
 * @brief Format limit value for display.
 * @param value Limit value in bytes or count.
 * @param isBytes True if value represents bytes (use IEC units).
 * @return Formatted string (e.g., "64 MiB", "unlimited", "1024").
 * @note NOT RT-safe: Allocates for string building.
 */
[[nodiscard]] std::string formatLimit(std::uint64_t value, bool isBytes = false);

} // namespace system

} // namespace seeker

#endif // SEEKER_SYSTEM_PROCESS_LIMITS_HPP