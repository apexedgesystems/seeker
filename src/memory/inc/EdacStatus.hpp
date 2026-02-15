#ifndef SEEKER_MEMORY_EDAC_STATUS_HPP
#define SEEKER_MEMORY_EDAC_STATUS_HPP
/**
 * @file EdacStatus.hpp
 * @brief ECC memory error detection via Linux EDAC subsystem.
 * @note Linux-only. Reads /sys/devices/system/edac/mc/.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Critical for radiation environments (spacecraft, high-altitude, accelerator).
 * Monitors correctable (CE) and uncorrectable (UE) memory errors via the
 * kernel EDAC (Error Detection And Correction) subsystem.
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <cstdint> // std::uint64_t, std::int64_t
#include <string>  // std::string

namespace seeker {

namespace memory {

/* ----------------------------- Constants ----------------------------- */

/// Maximum memory controllers tracked.
inline constexpr std::size_t EDAC_MAX_MC = 8;

/// Maximum chip-select rows tracked.
inline constexpr std::size_t EDAC_MAX_CSROW = 32;

/// Maximum DIMMs tracked.
inline constexpr std::size_t EDAC_MAX_DIMM = 32;

/// Maximum string length for EDAC labels.
inline constexpr std::size_t EDAC_LABEL_SIZE = 32;

/// Maximum string length for EDAC type strings.
inline constexpr std::size_t EDAC_TYPE_SIZE = 64;

/* ----------------------------- Types ----------------------------- */

/**
 * @brief Memory controller information from EDAC subsystem.
 *
 * Each memory controller (mc0, mc1, ...) manages one or more
 * memory channels and reports aggregate error counts.
 */
struct MemoryController {
  std::array<char, EDAC_LABEL_SIZE> name{};     ///< mc0, mc1, etc.
  std::array<char, EDAC_TYPE_SIZE> mcType{};    ///< EDAC driver type (e.g., ie31200)
  std::array<char, EDAC_LABEL_SIZE> edacMode{}; ///< SECDED, S4ECD4ED, etc.
  std::array<char, EDAC_TYPE_SIZE> memType{};   ///< DDR4, DDR5, etc.
  std::size_t sizeMb{0};                        ///< Total size in MB
  std::uint64_t ceCount{0};                     ///< Correctable errors (total)
  std::uint64_t ceNoInfoCount{0};               ///< CE with no location info
  std::uint64_t ueCount{0};                     ///< Uncorrectable errors (total)
  std::uint64_t ueNoInfoCount{0};               ///< UE with no location info
  std::size_t csrowCount{0};                    ///< Number of chip-select rows
  std::int32_t mcIndex{-1};                     ///< Memory controller index

  /// @brief Check if this controller has any errors.
  [[nodiscard]] bool hasErrors() const noexcept;

  /// @brief Check if this controller has uncorrectable errors.
  [[nodiscard]] bool hasCriticalErrors() const noexcept;
};

/**
 * @brief Chip-select row information.
 *
 * CSRows represent physical memory rows within a controller.
 * Error counts here help localize failing memory.
 */
struct CsRow {
  std::array<char, EDAC_LABEL_SIZE> label{};    ///< Row label
  std::uint32_t mcIndex{0};                     ///< Parent memory controller index
  std::uint32_t csrowIndex{0};                  ///< Row index within controller
  std::uint64_t ceCount{0};                     ///< Correctable errors
  std::uint64_t ueCount{0};                     ///< Uncorrectable errors
  std::size_t sizeMb{0};                        ///< Size in MB
  std::array<char, EDAC_LABEL_SIZE> memType{};  ///< Memory type
  std::array<char, EDAC_LABEL_SIZE> edacMode{}; ///< EDAC mode
};

/**
 * @brief DIMM information from EDAC subsystem.
 *
 * Modern EDAC drivers expose per-DIMM error counts
 * for more precise fault localization.
 */
struct DimmInfo {
  std::array<char, EDAC_LABEL_SIZE> label{};    ///< DIMM label
  std::array<char, EDAC_LABEL_SIZE> location{}; ///< Physical location (slot)
  std::uint32_t mcIndex{0};                     ///< Parent memory controller index
  std::uint32_t dimmIndex{0};                   ///< DIMM index within controller
  std::uint64_t ceCount{0};                     ///< Correctable errors
  std::uint64_t ueCount{0};                     ///< Uncorrectable errors
  std::size_t sizeMb{0};                        ///< Size in MB
  std::array<char, EDAC_LABEL_SIZE> memType{};  ///< Memory type
};

/**
 * @brief Complete EDAC status snapshot.
 *
 * Aggregates all memory controller, CSRow, and DIMM information
 * along with system-wide error totals.
 */
struct EdacStatus {
  /// Per-controller information
  std::array<MemoryController, EDAC_MAX_MC> controllers{};
  std::size_t mcCount{0};

  /// Chip-select row information
  std::array<CsRow, EDAC_MAX_CSROW> csrows{};
  std::size_t csrowCount{0};

  /// DIMM information
  std::array<DimmInfo, EDAC_MAX_DIMM> dimms{};
  std::size_t dimmCount{0};

  /// System-wide totals
  std::uint64_t totalCeCount{0}; ///< Total correctable errors across all MCs
  std::uint64_t totalUeCount{0}; ///< Total uncorrectable errors across all MCs

  /// Status flags
  bool edacSupported{false};       ///< EDAC subsystem present in kernel
  bool eccEnabled{false};          ///< ECC actually enabled (has memory controllers)
  std::uint64_t pollIntervalMs{0}; ///< EDAC polling interval in milliseconds

  /// Timestamps (Unix epoch, 0 if unavailable)
  std::int64_t lastCeTime{0}; ///< Timestamp of most recent CE
  std::int64_t lastUeTime{0}; ///< Timestamp of most recent UE

  /// @brief Check if any memory errors have occurred.
  [[nodiscard]] bool hasErrors() const noexcept;

  /// @brief Check if uncorrectable errors have occurred (critical).
  [[nodiscard]] bool hasCriticalErrors() const noexcept;

  /// @brief Find controller by index.
  /// @return Pointer to controller, or nullptr if not found.
  [[nodiscard]] const MemoryController* findController(std::int32_t mcIndex) const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;

  /// @brief JSON representation.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toJson() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Collect EDAC status from sysfs.
 * @return Populated EdacStatus; edacSupported=false if EDAC unavailable.
 * @note RT-safe: Bounded sysfs reads, fixed-size output.
 *
 * Sources:
 *  - /sys/devices/system/edac/mc/ - Memory controller presence
 *  - /sys/devices/system/edac/mc/mcN/ce_count - Correctable errors
 *  - /sys/devices/system/edac/mc/mcN/ue_count - Uncorrectable errors
 *  - /sys/devices/system/edac/mc/mcN/csrowN/ - Chip-select row info
 *  - /sys/devices/system/edac/mc/mcN/dimmN/ - DIMM info (if available)
 */
[[nodiscard]] EdacStatus getEdacStatus() noexcept;

/**
 * @brief Check if EDAC subsystem is available.
 * @return True if /sys/devices/system/edac/mc/ exists.
 * @note RT-safe: Single stat() call.
 */
[[nodiscard]] bool isEdacSupported() noexcept;

} // namespace memory

} // namespace seeker

#endif // SEEKER_MEMORY_EDAC_STATUS_HPP