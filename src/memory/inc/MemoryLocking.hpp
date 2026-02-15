#ifndef SEEKER_MEMORY_LOCKING_HPP
#define SEEKER_MEMORY_LOCKING_HPP
/**
 * @file MemoryLocking.hpp
 * @brief Memory locking limits and capability status (Linux).
 * @note Linux-only. Reads /proc/self/limits and checks capabilities.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Use cases for RT systems:
 *  - Verify mlock limits before allocating RT buffers
 *  - Check CAP_IPC_LOCK for unlimited mlock
 *  - Validate mlockall() will succeed
 */

#include <cstddef> // std::size_t
#include <cstdint> // std::uint64_t
#include <string>  // std::string

namespace seeker {

namespace memory {

/* ----------------------------- Constants ----------------------------- */

/// Value indicating unlimited mlock.
inline constexpr std::uint64_t MLOCK_UNLIMITED = static_cast<std::uint64_t>(-1);

/* ----------------------------- MemoryLockingStatus ----------------------------- */

/**
 * @brief Memory locking limits and capability status.
 */
struct MemoryLockingStatus {
  /// Soft limit for locked memory (bytes). MLOCK_UNLIMITED if unlimited.
  std::uint64_t softLimitBytes{0};

  /// Hard limit for locked memory (bytes). MLOCK_UNLIMITED if unlimited.
  std::uint64_t hardLimitBytes{0};

  /// Currently locked memory by this process (bytes).
  std::uint64_t currentLockedBytes{0};

  /// True if CAP_IPC_LOCK capability is effective (allows unlimited mlock).
  bool hasCapIpcLock{false};

  /// True if running as root (uid 0).
  bool isRoot{false};

  /// @brief Check if mlock is effectively unlimited.
  /// @return True if unlimited via capability, root, or unlimited limit.
  [[nodiscard]] bool isUnlimited() const noexcept;

  /// @brief Check if requested size can be locked.
  /// @param bytes Number of bytes to potentially lock.
  /// @return True if within limits (considering current locked + requested).
  [[nodiscard]] bool canLock(std::uint64_t bytes) const noexcept;

  /// @brief Get remaining lockable bytes.
  /// @return Available bytes, or MLOCK_UNLIMITED if unlimited.
  [[nodiscard]] std::uint64_t availableBytes() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- MlockallStatus ----------------------------- */

/**
 * @brief Result of mlockall() capability check.
 */
struct MlockallStatus {
  bool canLockCurrent{false};    ///< MCL_CURRENT would succeed
  bool canLockFuture{false};     ///< MCL_FUTURE would succeed
  bool isCurrentlyLocked{false}; ///< mlockall() already active

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Collect memory locking status from /proc and capabilities.
 * @return Populated MemoryLockingStatus.
 * @note RT-safe: Bounded file reads, no allocation.
 *
 * Sources:
 *  - /proc/self/limits (Max locked memory)
 *  - /proc/self/status (VmLck for current locked)
 *  - capget(2) for CAP_IPC_LOCK
 *  - getuid(2) for root check
 */
[[nodiscard]] MemoryLockingStatus getMemoryLockingStatus() noexcept;

/**
 * @brief Check if mlockall() operations would succeed.
 * @return MlockallStatus with capability flags.
 * @note RT-safe: Based on getMemoryLockingStatus() result.
 *
 * Note: This is a heuristic check. Actual mlockall() may still fail
 * due to OOM conditions or cgroup limits not reflected here.
 */
[[nodiscard]] MlockallStatus getMlockallStatus() noexcept;

/**
 * @brief Check if CAP_IPC_LOCK capability is effective.
 * @return True if process has CAP_IPC_LOCK in effective set.
 * @note RT-safe: Single syscall.
 */
[[nodiscard]] bool hasCapIpcLock() noexcept;

} // namespace memory

} // namespace seeker

#endif // SEEKER_MEMORY_LOCKING_HPP