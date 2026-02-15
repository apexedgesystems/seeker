#ifndef SEEKER_SYSTEM_FILE_DESCRIPTOR_STATUS_HPP
#define SEEKER_SYSTEM_FILE_DESCRIPTOR_STATUS_HPP
/**
 * @file FileDescriptorStatus.hpp
 * @brief File descriptor usage and limit monitoring.
 *
 * Design goals:
 *  - RT-safe queries for FD counts
 *  - System-wide and per-process FD monitoring
 *  - Detection of FD exhaustion risks
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace system {

/* ----------------------------- Constants ----------------------------- */

/// Maximum length for file path strings.
inline constexpr std::size_t FD_PATH_SIZE = 512;

/// Maximum number of FD types to track.
inline constexpr std::size_t MAX_FD_TYPES = 16;

/* ----------------------------- FdType ----------------------------- */

/**
 * @brief Types of file descriptors.
 */
enum class FdType : std::uint8_t {
  UNKNOWN = 0,    ///< Unknown or unclassified
  REGULAR = 1,    ///< Regular file
  DIRECTORY = 2,  ///< Directory
  PIPE = 3,       ///< Pipe or FIFO
  SOCKET = 4,     ///< Network socket
  DEVICE = 5,     ///< Character or block device
  EVENTFD = 6,    ///< Event file descriptor
  TIMERFD = 7,    ///< Timer file descriptor
  SIGNALFD = 8,   ///< Signal file descriptor
  EPOLL = 9,      ///< Epoll instance
  INOTIFY = 10,   ///< Inotify instance
  ANON_INODE = 11 ///< Anonymous inode (generic)
};

/**
 * @brief Convert FD type to string.
 * @param type Type to convert.
 * @return Human-readable string.
 * @note RT-safe: Returns static string.
 */
[[nodiscard]] const char* toString(FdType type) noexcept;

/* ----------------------------- FdTypeCount ----------------------------- */

/**
 * @brief Count of file descriptors by type.
 */
struct FdTypeCount {
  FdType type = FdType::UNKNOWN; ///< FD type
  std::uint32_t count = 0;       ///< Number of FDs of this type

  /**
   * @brief Format as human-readable string.
   * @return Formatted count.
   * @note NOT RT-safe: Returns std::string.
   */
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- ProcessFdStatus ----------------------------- */

/**
 * @brief File descriptor status for current process.
 */
struct ProcessFdStatus {
  std::uint32_t openCount = 0; ///< Currently open FD count
  std::uint64_t softLimit = 0; ///< RLIMIT_NOFILE soft limit
  std::uint64_t hardLimit = 0; ///< RLIMIT_NOFILE hard limit

  std::array<FdTypeCount, MAX_FD_TYPES> byType{}; ///< Counts by FD type
  std::size_t typeCount = 0;                      ///< Number of types with non-zero count

  std::uint32_t highestFd = 0; ///< Highest FD number in use

  /**
   * @brief Get available FD headroom (soft limit - open).
   * @return Number of FDs that can still be opened.
   */
  [[nodiscard]] std::uint64_t available() const noexcept;

  /**
   * @brief Get utilization percentage.
   * @return Percentage of soft limit used (0-100).
   */
  [[nodiscard]] double utilizationPercent() const noexcept;

  /**
   * @brief Check if FD usage is critically high (>90% of soft limit).
   * @return true if nearing exhaustion.
   */
  [[nodiscard]] bool isCritical() const noexcept;

  /**
   * @brief Check if FD usage is elevated (>75% of soft limit).
   * @return true if usage is elevated.
   */
  [[nodiscard]] bool isElevated() const noexcept;

  /**
   * @brief Get count for a specific FD type.
   * @param type Type to query.
   * @return Count of FDs of that type, 0 if not tracked.
   */
  [[nodiscard]] std::uint32_t countByType(FdType type) const noexcept;

  /**
   * @brief Format as human-readable string.
   * @return Formatted status.
   * @note NOT RT-safe: Returns std::string.
   */
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- SystemFdStatus ----------------------------- */

/**
 * @brief System-wide file descriptor status.
 */
struct SystemFdStatus {
  std::uint64_t allocated = 0; ///< Currently allocated FDs system-wide
  std::uint64_t free = 0;      ///< Free FD slots in kernel
  std::uint64_t maximum = 0;   ///< System maximum (fs.file-max)

  std::uint64_t nrOpen = 0;   ///< Per-process max (fs.nr_open)
  std::uint64_t inodeMax = 0; ///< Maximum inodes (fs.inode-max, if available)

  /**
   * @brief Get system-wide available FDs.
   * @return maximum - allocated.
   */
  [[nodiscard]] std::uint64_t available() const noexcept;

  /**
   * @brief Get system-wide utilization percentage.
   * @return Percentage of maximum used (0-100).
   */
  [[nodiscard]] double utilizationPercent() const noexcept;

  /**
   * @brief Check if system FD usage is critically high.
   * @return true if nearing system-wide exhaustion.
   */
  [[nodiscard]] bool isCritical() const noexcept;

  /**
   * @brief Format as human-readable string.
   * @return Formatted status.
   * @note NOT RT-safe: Returns std::string.
   */
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- FileDescriptorStatus ----------------------------- */

/**
 * @brief Combined file descriptor status.
 */
struct FileDescriptorStatus {
  ProcessFdStatus process{}; ///< Current process FD status
  SystemFdStatus system{};   ///< System-wide FD status

  /**
   * @brief Check if any FD limit is critically close.
   * @return true if process or system is critical.
   */
  [[nodiscard]] bool anyCritical() const noexcept;

  /**
   * @brief Format as human-readable string.
   * @return Formatted status.
   * @note NOT RT-safe: Returns std::string.
   */
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Get complete file descriptor status.
 * @return Populated FileDescriptorStatus structure.
 * @note NOT RT-safe: Iterates /proc/self/fd directory.
 */
[[nodiscard]] FileDescriptorStatus getFileDescriptorStatus() noexcept;

/**
 * @brief Get current process FD status only.
 * @return Populated ProcessFdStatus structure.
 * @note NOT RT-safe: Iterates /proc/self/fd directory.
 */
[[nodiscard]] ProcessFdStatus getProcessFdStatus() noexcept;

/**
 * @brief Get system-wide FD status only.
 * @return Populated SystemFdStatus structure.
 * @note RT-safe: Reads procfs files only.
 */
[[nodiscard]] SystemFdStatus getSystemFdStatus() noexcept;

/**
 * @brief Get quick count of open FDs for current process.
 * @return Number of open file descriptors.
 * @note NOT RT-safe: Iterates directory.
 *
 * Lighter weight than getProcessFdStatus() when only count is needed.
 */
[[nodiscard]] std::uint32_t getOpenFdCount() noexcept;

/**
 * @brief Get RLIMIT_NOFILE soft limit for current process.
 * @return Soft limit value.
 * @note RT-safe: Uses getrlimit().
 */
[[nodiscard]] std::uint64_t getFdSoftLimit() noexcept;

/**
 * @brief Get RLIMIT_NOFILE hard limit for current process.
 * @return Hard limit value.
 * @note RT-safe: Uses getrlimit().
 */
[[nodiscard]] std::uint64_t getFdHardLimit() noexcept;

} // namespace system

} // namespace seeker

#endif // SEEKER_SYSTEM_FILE_DESCRIPTOR_STATUS_HPP