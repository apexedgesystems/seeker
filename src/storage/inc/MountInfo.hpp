#ifndef SEEKER_STORAGE_MOUNT_INFO_HPP
#define SEEKER_STORAGE_MOUNT_INFO_HPP
/**
 * @file MountInfo.hpp
 * @brief Mounted filesystem information from /proc/mounts.
 * @note Linux-only. Reads /proc/mounts for mount table.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides mount point to device mapping and filesystem options:
 *  - Device to mount point resolution
 *  - Filesystem type detection
 *  - Mount option inspection for RT-critical settings
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace storage {

/* ----------------------------- Constants ----------------------------- */

/// Maximum path length for mount points and devices.
inline constexpr std::size_t PATH_SIZE = 256;

/// Maximum filesystem type length.
inline constexpr std::size_t FSTYPE_SIZE = 32;

/// Maximum mount options string length.
inline constexpr std::size_t MOUNT_OPTIONS_SIZE = 512;

/// Maximum number of mounts to track.
inline constexpr std::size_t MAX_MOUNTS = 128;

/// Device name size (shared with BlockDeviceInfo).
inline constexpr std::size_t MOUNT_DEVICE_NAME_SIZE = 32;

/* ----------------------------- MountEntry ----------------------------- */

/**
 * @brief Information about a single mounted filesystem.
 *
 * Parsed from /proc/mounts line:
 *   device mountpoint fstype options dump pass
 */
struct MountEntry {
  std::array<char, PATH_SIZE> mountPoint{};           ///< Mount point path (e.g., "/", "/home")
  std::array<char, PATH_SIZE> device{};               ///< Device path (e.g., "/dev/nvme0n1p2")
  std::array<char, MOUNT_DEVICE_NAME_SIZE> devName{}; ///< Base device name (e.g., "nvme0n1")
  std::array<char, FSTYPE_SIZE> fsType{};             ///< Filesystem type (e.g., "ext4", "xfs")
  std::array<char, MOUNT_OPTIONS_SIZE> options{};     ///< Mount options string

  /// @brief Check if filesystem is mounted read-only.
  [[nodiscard]] bool isReadOnly() const noexcept;

  /// @brief Check if noatime is set (good for RT/performance).
  [[nodiscard]] bool hasNoAtime() const noexcept;

  /// @brief Check if nodiratime is set.
  [[nodiscard]] bool hasNoDirAtime() const noexcept;

  /// @brief Check if relatime is set.
  [[nodiscard]] bool hasRelAtime() const noexcept;

  /// @brief Check if nobarrier/barrier=0 is set (dangerous for data integrity).
  [[nodiscard]] bool hasNoBarrier() const noexcept;

  /// @brief Check if sync mount option is set.
  [[nodiscard]] bool isSync() const noexcept;

  /// @brief Check if this is a real block device (not pseudo-fs like proc, sys).
  [[nodiscard]] bool isBlockDevice() const noexcept;

  /// @brief Check if this is a network filesystem (nfs, cifs, etc.).
  [[nodiscard]] bool isNetworkFs() const noexcept;

  /// @brief Check if this is a tmpfs/ramfs.
  [[nodiscard]] bool isTmpFs() const noexcept;

  /// @brief Get the journaling mode for ext4 (data=ordered, data=journal, data=writeback).
  /// @return Journaling mode string or empty if not ext4 or not specified.
  [[nodiscard]] const char* ext4DataMode() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- MountTable ----------------------------- */

/**
 * @brief Collection of all mounted filesystems.
 */
struct MountTable {
  MountEntry mounts[MAX_MOUNTS]{}; ///< Mount entries
  std::size_t count{0};            ///< Valid entries in mounts[]

  /// @brief Find mount entry by mount point path.
  /// @param path Exact mount point (e.g., "/home").
  /// @return Pointer to entry or nullptr if not found.
  [[nodiscard]] const MountEntry* findByMountPoint(const char* path) const noexcept;

  /// @brief Find mount entry for a given path (longest prefix match).
  /// @param path Any path (e.g., "/home/user/file.txt").
  /// @return Pointer to entry for the containing mount, or nullptr.
  [[nodiscard]] const MountEntry* findForPath(const char* path) const noexcept;

  /// @brief Find mount entry by device name.
  /// @param devName Device name (e.g., "nvme0n1p2" or "/dev/nvme0n1p2").
  /// @return Pointer to entry or nullptr if not found.
  [[nodiscard]] const MountEntry* findByDevice(const char* devName) const noexcept;

  /// @brief Count real block device mounts (excludes pseudo-filesystems).
  [[nodiscard]] std::size_t countBlockDevices() const noexcept;

  /// @brief Human-readable summary of all mounts.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Read and parse the current mount table.
 * @return Populated MountTable with all mounted filesystems.
 * @note NOT RT-safe: File I/O with unbounded line parsing.
 *
 * Source: /proc/mounts
 */
[[nodiscard]] MountTable getMountTable() noexcept;

/**
 * @brief Get mount entry for a specific path.
 * @param path Any filesystem path.
 * @return Mount entry (empty if path not found).
 * @note RT-safe: Bounded file read, single-pass scan.
 *
 * Finds the mount with the longest matching prefix for the given path.
 */
[[nodiscard]] MountEntry getMountForPath(const char* path) noexcept;

} // namespace storage

} // namespace seeker

#endif // SEEKER_STORAGE_MOUNT_INFO_HPP