#ifndef SEEKER_DEVICE_GPIO_INFO_HPP
#define SEEKER_DEVICE_GPIO_INFO_HPP
/**
 * @file GpioInfo.hpp
 * @brief GPIO chip enumeration and line information.
 * @note Linux-only. Uses libgpiod v2 character device interface.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides GPIO information for embedded/flight software:
 *  - Chip enumeration via /dev/gpiochip*
 *  - Line information and configuration
 *  - Consumer tracking (who has claimed lines)
 *  - Direction and drive mode status
 *  - RT safety considerations for GPIO diagnostics
 *
 * This module uses the modern character device interface (gpiochip), not
 * the deprecated sysfs interface (/sys/class/gpio).
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace device {

/* ----------------------------- Constants ----------------------------- */

/// Maximum GPIO chip name length.
inline constexpr std::size_t GPIO_NAME_SIZE = 64;

/// Maximum GPIO label/consumer length.
inline constexpr std::size_t GPIO_LABEL_SIZE = 64;

/// Maximum path length.
inline constexpr std::size_t GPIO_PATH_SIZE = 128;

/// Maximum number of GPIO chips to enumerate.
inline constexpr std::size_t MAX_GPIO_CHIPS = 32;

/// Maximum number of lines to query per chip for detailed info.
inline constexpr std::size_t MAX_GPIO_LINES_DETAILED = 128;

/* ----------------------------- GpioDirection ----------------------------- */

/**
 * @brief GPIO line direction.
 */
enum class GpioDirection : std::uint8_t {
  UNKNOWN = 0, ///< Direction unknown or unavailable
  INPUT,       ///< Line configured as input
  OUTPUT,      ///< Line configured as output
};

/// @brief Convert GpioDirection to string.
/// @param dir Direction.
/// @return String representation (e.g., "input").
[[nodiscard]] const char* toString(GpioDirection dir) noexcept;

/* ----------------------------- GpioDrive ----------------------------- */

/**
 * @brief GPIO output drive mode.
 */
enum class GpioDrive : std::uint8_t {
  UNKNOWN = 0, ///< Drive mode unknown
  PUSH_PULL,   ///< Push-pull (default)
  OPEN_DRAIN,  ///< Open drain (requires external pull-up)
  OPEN_SOURCE, ///< Open source (requires external pull-down)
};

/// @brief Convert GpioDrive to string.
/// @param drive Drive mode.
/// @return String representation (e.g., "push-pull").
[[nodiscard]] const char* toString(GpioDrive drive) noexcept;

/* ----------------------------- GpioBias ----------------------------- */

/**
 * @brief GPIO line bias configuration.
 */
enum class GpioBias : std::uint8_t {
  UNKNOWN = 0, ///< Bias unknown
  DISABLED,    ///< No internal bias
  PULL_UP,     ///< Internal pull-up enabled
  PULL_DOWN,   ///< Internal pull-down enabled
};

/// @brief Convert GpioBias to string.
/// @param bias Bias setting.
/// @return String representation (e.g., "pull-up").
[[nodiscard]] const char* toString(GpioBias bias) noexcept;

/* ----------------------------- GpioEdge ----------------------------- */

/**
 * @brief GPIO interrupt edge detection setting.
 */
enum class GpioEdge : std::uint8_t {
  NONE = 0, ///< No edge detection
  RISING,   ///< Rising edge only
  FALLING,  ///< Falling edge only
  BOTH,     ///< Both edges
};

/// @brief Convert GpioEdge to string.
/// @param edge Edge setting.
/// @return String representation (e.g., "rising").
[[nodiscard]] const char* toString(GpioEdge edge) noexcept;

/* ----------------------------- GpioLineFlags ----------------------------- */

/**
 * @brief GPIO line configuration flags.
 */
struct GpioLineFlags {
  bool used{false};      ///< Line is in use by a consumer
  bool activeLow{false}; ///< Active-low polarity
  GpioDirection direction{GpioDirection::UNKNOWN};
  GpioDrive drive{GpioDrive::UNKNOWN};
  GpioBias bias{GpioBias::UNKNOWN};
  GpioEdge edge{GpioEdge::NONE};

  /// @brief Check if any special configuration is active.
  [[nodiscard]] bool hasSpecialConfig() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- GpioLineInfo ----------------------------- */

/**
 * @brief Information for a single GPIO line.
 */
struct GpioLineInfo {
  std::uint32_t offset{0};                      ///< Line offset within chip (0-based)
  std::array<char, GPIO_NAME_SIZE> name{};      ///< Line name (may be empty)
  std::array<char, GPIO_LABEL_SIZE> consumer{}; ///< Consumer holding line
  GpioLineFlags flags{};

  /// @brief Check if line has a name assigned.
  [[nodiscard]] bool hasName() const noexcept;

  /// @brief Check if line is currently in use.
  [[nodiscard]] bool isUsed() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- GpioChipInfo ----------------------------- */

/**
 * @brief Information for a GPIO chip (controller).
 */
struct GpioChipInfo {
  std::array<char, GPIO_NAME_SIZE> name{};   ///< Chip name (e.g., "gpiochip0")
  std::array<char, GPIO_LABEL_SIZE> label{}; ///< Chip label (e.g., "pinctrl-bcm2835")
  std::array<char, GPIO_PATH_SIZE> path{};   ///< Device path (e.g., "/dev/gpiochip0")

  std::uint32_t numLines{0};   ///< Number of GPIO lines on this chip
  std::uint32_t linesUsed{0};  ///< Count of lines currently in use
  std::int32_t chipNumber{-1}; ///< Chip number (parsed from name)

  bool exists{false};     ///< Chip device exists
  bool accessible{false}; ///< Chip is accessible (read permission)

  /// @brief Check if chip is usable.
  [[nodiscard]] bool isUsable() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- GpioChipList ----------------------------- */

/**
 * @brief Collection of GPIO chip information.
 */
struct GpioChipList {
  GpioChipInfo chips[MAX_GPIO_CHIPS]{};
  std::size_t count{0};

  /// @brief Find chip by name.
  /// @param name Chip name (e.g., "gpiochip0").
  /// @return Pointer to chip info, or nullptr if not found.
  [[nodiscard]] const GpioChipInfo* find(const char* name) const noexcept;

  /// @brief Find chip by number.
  /// @param chipNum Chip number.
  /// @return Pointer to chip info, or nullptr if not found.
  [[nodiscard]] const GpioChipInfo* findByNumber(std::int32_t chipNum) const noexcept;

  /// @brief Check if list is empty.
  [[nodiscard]] bool empty() const noexcept;

  /// @brief Total GPIO lines across all chips.
  [[nodiscard]] std::uint32_t totalLines() const noexcept;

  /// @brief Total used lines across all chips.
  [[nodiscard]] std::uint32_t totalUsed() const noexcept;

  /// @brief Human-readable summary of all chips.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- GpioLineList ----------------------------- */

/**
 * @brief Collection of GPIO line information for a chip.
 */
struct GpioLineList {
  GpioLineInfo lines[MAX_GPIO_LINES_DETAILED]{};
  std::size_t count{0};
  std::int32_t chipNumber{-1}; ///< Source chip number

  /// @brief Find line by offset.
  /// @param offset Line offset.
  /// @return Pointer to line info, or nullptr if not found.
  [[nodiscard]] const GpioLineInfo* findByOffset(std::uint32_t offset) const noexcept;

  /// @brief Find line by name.
  /// @param name Line name.
  /// @return Pointer to line info, or nullptr if not found.
  [[nodiscard]] const GpioLineInfo* findByName(const char* name) const noexcept;

  /// @brief Check if list is empty.
  [[nodiscard]] bool empty() const noexcept;

  /// @brief Count lines that are in use.
  [[nodiscard]] std::size_t countUsed() const noexcept;

  /// @brief Count lines configured as inputs.
  [[nodiscard]] std::size_t countInputs() const noexcept;

  /// @brief Count lines configured as outputs.
  [[nodiscard]] std::size_t countOutputs() const noexcept;

  /// @brief Human-readable summary of all lines.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Get information for a specific GPIO chip.
 * @param chipNum Chip number (e.g., 0 for gpiochip0).
 * @return Populated GpioChipInfo, or default-initialized if not found.
 * @note RT-safe: Bounded ioctl calls.
 */
[[nodiscard]] GpioChipInfo getGpioChipInfo(std::int32_t chipNum) noexcept;

/**
 * @brief Get information for a GPIO chip by name.
 * @param name Chip name (e.g., "gpiochip0") or path ("/dev/gpiochip0").
 * @return Populated GpioChipInfo, or default-initialized if not found.
 * @note RT-safe: Bounded ioctl calls.
 */
[[nodiscard]] GpioChipInfo getGpioChipInfoByName(const char* name) noexcept;

/**
 * @brief Get information for a specific GPIO line.
 * @param chipNum Chip number.
 * @param lineOffset Line offset within chip.
 * @return Populated GpioLineInfo, or default-initialized if not found.
 * @note RT-safe: Bounded ioctl call.
 */
[[nodiscard]] GpioLineInfo getGpioLineInfo(std::int32_t chipNum, std::uint32_t lineOffset) noexcept;

/**
 * @brief Enumerate all GPIO lines for a chip.
 * @param chipNum Chip number.
 * @return List of line information (up to MAX_GPIO_LINES_DETAILED).
 * @note NOT RT-safe: May perform many ioctl calls.
 */
[[nodiscard]] GpioLineList getGpioLines(std::int32_t chipNum) noexcept;

/**
 * @brief Enumerate all GPIO chips on the system.
 * @return List of GPIO chip information.
 * @note NOT RT-safe: Directory enumeration over /dev/.
 */
[[nodiscard]] GpioChipList getAllGpioChips() noexcept;

/**
 * @brief Check if a GPIO chip exists.
 * @param chipNum Chip number.
 * @return true if /dev/gpiochipN exists.
 * @note RT-safe: Single stat call.
 */
[[nodiscard]] bool gpioChipExists(std::int32_t chipNum) noexcept;

/**
 * @brief Parse chip number from name.
 * @param name Chip name (e.g., "gpiochip0" or "/dev/gpiochip0").
 * @param outChipNum Output chip number.
 * @return true if parsed successfully.
 * @note RT-safe: String parsing only.
 */
[[nodiscard]] bool parseGpioChipNumber(const char* name, std::int32_t& outChipNum) noexcept;

/**
 * @brief Find which chip and offset corresponds to a global GPIO number.
 * @param gpioNum Global GPIO number (legacy numbering).
 * @param outChipNum Output chip number.
 * @param outOffset Output line offset.
 * @return true if found and mapped.
 * @note NOT RT-safe: May enumerate chips.
 *
 * Note: Global GPIO numbers are deprecated. Prefer chip+offset addressing.
 */
[[nodiscard]] bool findGpioLine(std::int32_t gpioNum, std::int32_t& outChipNum,
                                std::uint32_t& outOffset) noexcept;

} // namespace device

} // namespace seeker

#endif // SEEKER_DEVICE_GPIO_INFO_HPP
