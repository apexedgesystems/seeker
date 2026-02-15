#ifndef SEEKER_STORAGE_BLOCK_DEVICE_INFO_HPP
#define SEEKER_STORAGE_BLOCK_DEVICE_INFO_HPP
/**
 * @file BlockDeviceInfo.hpp
 * @brief Block device hardware properties and capabilities.
 * @note Linux-only. Reads /sys/block/ for device information.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides device-level information for storage diagnostics:
 *  - Device type (NVMe, SATA SSD, HDD)
 *  - Sector sizes (logical, physical) for alignment checking
 *  - TRIM/discard support detection
 *  - Device capacity
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace storage {

/// Maximum device name length (e.g., "nvme0n1", "sda").
inline constexpr std::size_t DEVICE_NAME_SIZE = 32;

/// Maximum model/vendor string length.
inline constexpr std::size_t MODEL_STRING_SIZE = 64;

/// Maximum number of block devices to enumerate.
inline constexpr std::size_t MAX_BLOCK_DEVICES = 64;

/* ----------------------------- BlockDevice ----------------------------- */

/**
 * @brief Hardware properties for a single block device.
 *
 * Contains static device properties that don't change during operation.
 * Useful for:
 *  - Detecting SSD vs HDD for scheduler selection
 *  - Verifying I/O alignment for optimal performance
 *  - Checking TRIM support for SSD maintenance
 */
struct BlockDevice {
  std::array<char, DEVICE_NAME_SIZE> name{};    ///< Device name (e.g., "nvme0n1", "sda")
  std::array<char, MODEL_STRING_SIZE> model{};  ///< Device model string
  std::array<char, MODEL_STRING_SIZE> vendor{}; ///< Device vendor string

  std::uint64_t sizeBytes{0};         ///< Total device capacity in bytes
  std::uint32_t logicalBlockSize{0};  ///< Logical sector size (typically 512)
  std::uint32_t physicalBlockSize{0}; ///< Physical sector size (512 or 4096)
  std::uint32_t minIoSize{0};         ///< Minimum I/O size for optimal performance
  std::uint32_t optimalIoSize{0};     ///< Optimal I/O size (0 if unknown)

  bool rotational{false}; ///< true = HDD (spinning), false = SSD/NVMe
  bool removable{false};  ///< true = removable media (USB, etc.)
  bool hasTrim{false};    ///< TRIM/discard support available

  /// @brief Check if device is NVMe (name starts with "nvme").
  [[nodiscard]] bool isNvme() const noexcept;

  /// @brief Check if device is SSD (non-rotational, non-removable).
  [[nodiscard]] bool isSsd() const noexcept;

  /// @brief Check if device is HDD (rotational).
  [[nodiscard]] bool isHdd() const noexcept;

  /// @brief Check if physical block size is 4K-aligned (Advanced Format).
  [[nodiscard]] bool isAdvancedFormat() const noexcept;

  /// @brief Get human-readable device type string.
  /// @return "NVMe", "SSD", "HDD", or "Unknown".
  [[nodiscard]] const char* deviceType() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- BlockDeviceList ----------------------------- */

/**
 * @brief Collection of all block devices on the system.
 */
struct BlockDeviceList {
  BlockDevice devices[MAX_BLOCK_DEVICES]{}; ///< Device array
  std::size_t count{0};                     ///< Valid entries in devices[]

  /// @brief Find device by name.
  /// @param name Device name (e.g., "nvme0n1").
  /// @return Pointer to device or nullptr if not found.
  [[nodiscard]] const BlockDevice* find(const char* name) const noexcept;

  /// @brief Count devices by type.
  [[nodiscard]] std::size_t countNvme() const noexcept;
  [[nodiscard]] std::size_t countSsd() const noexcept;
  [[nodiscard]] std::size_t countHdd() const noexcept;

  /// @brief Human-readable summary of all devices.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Enumerate all block devices on the system.
 * @return List of block devices with their properties.
 * @note NOT RT-safe: Directory iteration over /sys/block/.
 *
 * Filters out:
 *  - Loop devices (loop0, loop1, ...)
 *  - RAM disks (ram0, ram1, ...)
 *  - Device mapper (dm-*)
 *
 * Sources:
 *  - /sys/block/ directory listing
 *  - /sys/block/\<dev\>/device/model
 *  - /sys/block/\<dev\>/device/vendor
 *  - /sys/block/\<dev\>/queue/ for I/O parameters
 */
[[nodiscard]] BlockDeviceList getBlockDevices() noexcept;

/**
 * @brief Get properties for a specific block device.
 * @param name Device name (e.g., "nvme0n1", "sda").
 * @return Device properties (zeroed if device not found).
 * @note RT-safe: Bounded file reads from /sys/block/\<name\>/.
 */
[[nodiscard]] BlockDevice getBlockDevice(const char* name) noexcept;

/**
 * @brief Format bytes as human-readable size.
 * @param bytes Size in bytes.
 * @return Formatted string (e.g., "500 GB", "1 TB").
 * @note NOT RT-safe: Allocates std::string.
 */
[[nodiscard]] std::string formatCapacity(std::uint64_t bytes);

} // namespace storage

} // namespace seeker

#endif // SEEKER_STORAGE_BLOCK_DEVICE_INFO_HPP