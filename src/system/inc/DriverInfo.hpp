#ifndef SEEKER_SYSTEM_DRIVER_INFO_HPP
#define SEEKER_SYSTEM_DRIVER_INFO_HPP
/**
 * @file DriverInfo.hpp
 * @brief Kernel module inventory and driver assessment (Linux).
 * @note Linux-only. Reads /proc/modules, /sys/module/.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Use cases for RT systems:
 *  - Detect NVIDIA driver presence for GPU diagnostics
 *  - Check kernel taint status before deployment
 *  - Audit loaded modules for security/compliance
 *
 * Note: This module is NOT RT-safe due to unbounded directory scanning.
 * Use for diagnostics and startup checks only.
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <cstdint> // std::int32_t
#include <string>  // std::string

namespace seeker {

namespace system {

/* ----------------------------- Constants ----------------------------- */

/// Maximum number of drivers to track.
inline constexpr std::size_t MAX_DRIVER_ENTRIES = 512;

/// Buffer size for driver name.
inline constexpr std::size_t DRIVER_NAME_SIZE = 64;

/// Buffer size for driver version string.
inline constexpr std::size_t DRIVER_VERSION_SIZE = 64;

/// Buffer size for driver state string.
inline constexpr std::size_t DRIVER_STATE_SIZE = 16;

/// Maximum number of dependencies per driver.
inline constexpr std::size_t MAX_DRIVER_DEPS = 16;

/// Maximum number of assessment notes.
inline constexpr std::size_t MAX_ASSESSMENT_NOTES = 8;

/// Buffer size for assessment note.
inline constexpr std::size_t ASSESSMENT_NOTE_SIZE = 256;

/* ----------------------------- Driver Entry ----------------------------- */

/**
 * @brief Single loaded kernel module entry.
 *
 * Parsed from /proc/modules with metadata from /sys/module/\<name\>/.
 */
struct DriverEntry {
  /// Module name (e.g., "nvidia", "ixgbe", "nvme").
  std::array<char, DRIVER_NAME_SIZE> name{};

  /// Module version from /sys/module/\<name\>/version.
  std::array<char, DRIVER_VERSION_SIZE> version{};

  /// Source version from /sys/module/\<name\>/srcversion.
  std::array<char, DRIVER_VERSION_SIZE> srcVersion{};

  /// Module state (e.g., "Live", "Loading", "Unloading").
  std::array<char, DRIVER_STATE_SIZE> state{};

  /// Reference count (number of users).
  std::int32_t useCount{0};

  /// Module size in bytes.
  std::size_t sizeBytes{0};

  /// Dependencies (other modules this one depends on).
  std::array<char, DRIVER_NAME_SIZE> deps[MAX_DRIVER_DEPS]{};

  /// Number of valid entries in deps[].
  std::size_t depCount{0};

  /// @brief Check if this is a specific module.
  [[nodiscard]] bool isNamed(const char* targetName) const noexcept;

  /// @brief Human-readable single-line summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- Driver Inventory ----------------------------- */

/**
 * @brief Complete kernel module inventory.
 *
 * Contains all loaded modules up to MAX_DRIVER_ENTRIES, plus kernel taint status.
 */
struct DriverInventory {
  /// Loaded driver entries.
  DriverEntry entries[MAX_DRIVER_ENTRIES]{};

  /// Number of valid entries.
  std::size_t entryCount{0};

  /// Kernel taint mask from /proc/sys/kernel/tainted.
  std::int32_t taintMask{0};

  /// True if kernel is tainted (taintMask != 0).
  bool tainted{false};

  /// @brief Find driver by name.
  /// @param name Module name to find.
  /// @return Pointer to entry, or nullptr if not found.
  [[nodiscard]] const DriverEntry* find(const char* name) const noexcept;

  /// @brief Check if a module is loaded.
  /// @param name Module name to check.
  [[nodiscard]] bool isLoaded(const char* name) const noexcept;

  /// @brief Check if any NVIDIA module is loaded.
  [[nodiscard]] bool hasNvidiaDriver() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;

  /// @brief Brief summary (count and taint status only).
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toBriefSummary() const;
};

/* ----------------------------- Driver Assessment ----------------------------- */

/**
 * @brief High-level driver compatibility assessment.
 *
 * Provides RT-relevant driver information, particularly for GPU diagnostics.
 */
struct DriverAssessment {
  /// True if any NVIDIA module is loaded (nvidia, nvidia_uvm, nvidia_drm).
  bool nvidiaLoaded{false};

  /// True if NVML header was available at compile time.
  bool nvmlHeaderAvailable{false};

  /// True if NVML runtime is available (dlopen test).
  bool nvmlRuntimePresent{false};

  /// True if nouveau (open-source NVIDIA driver) is loaded.
  bool nouveauLoaded{false};

  /// True if Intel i915 graphics driver is loaded.
  bool i915Loaded{false};

  /// True if AMD amdgpu driver is loaded.
  bool amdgpuLoaded{false};

  /// Assessment notes (warnings, recommendations).
  std::array<char, ASSESSMENT_NOTE_SIZE> notes[MAX_ASSESSMENT_NOTES]{};

  /// Number of valid notes.
  std::size_t noteCount{0};

  /// @brief Add a note to the assessment.
  void addNote(const char* note) noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Collect loaded kernel module inventory.
 * @return Populated DriverInventory structure.
 * @note NOT RT-safe: Directory scanning, unbounded entries.
 *
 * Sources:
 *  - /proc/modules - Loaded modules list
 *  - /sys/module/\<name\>/version - Module version
 *  - /sys/module/\<name\>/srcversion - Source version
 *  - /proc/sys/kernel/tainted - Taint mask
 */
[[nodiscard]] DriverInventory getDriverInventory() noexcept;

/**
 * @brief Assess driver compatibility for RT/GPU workloads.
 * @param inv Driver inventory to assess.
 * @return DriverAssessment with compatibility info.
 * @note NOT RT-safe: May perform dlopen test.
 *
 * Checks:
 *  - NVIDIA driver presence and NVML availability
 *  - Graphics driver detection (nouveau, i915, amdgpu)
 *  - Kernel taint warnings
 */
[[nodiscard]] DriverAssessment assessDrivers(const DriverInventory& inv) noexcept;

/**
 * @brief Quick check if NVIDIA driver is loaded.
 * @return True if nvidia, nvidia_uvm, or nvidia_drm is in /proc/modules.
 * @note NOT RT-safe: File read.
 */
[[nodiscard]] bool isNvidiaDriverLoaded() noexcept;

/**
 * @brief Quick check if NVML runtime is available.
 * @return True if libnvidia-ml.so.1 can be loaded.
 * @note NOT RT-safe: dlopen test.
 */
[[nodiscard]] bool isNvmlRuntimeAvailable() noexcept;

} // namespace system

} // namespace seeker

#endif // SEEKER_SYSTEM_DRIVER_INFO_HPP