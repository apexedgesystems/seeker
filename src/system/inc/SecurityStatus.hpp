#ifndef SEEKER_SYSTEM_SECURITY_STATUS_HPP
#define SEEKER_SYSTEM_SECURITY_STATUS_HPP
/**
 * @file SecurityStatus.hpp
 * @brief Linux Security Module (LSM) status detection.
 *
 * Design goals:
 *  - Detect SELinux, AppArmor, and other LSM states
 *  - RT-safe queries where possible
 *  - Support for embedded and containerized environments
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace system {

/* ----------------------------- Constants ----------------------------- */

/// Maximum length for LSM name strings.
inline constexpr std::size_t LSM_NAME_SIZE = 32;

/// Maximum length for security context strings.
inline constexpr std::size_t SECURITY_CONTEXT_SIZE = 256;

/// Maximum number of LSMs to track.
inline constexpr std::size_t MAX_LSMS = 8;

/* ----------------------------- SelinuxMode ----------------------------- */

/**
 * @brief SELinux enforcement mode.
 */
enum class SelinuxMode : std::uint8_t {
  NOT_PRESENT = 0, ///< SELinux not available
  DISABLED = 1,    ///< SELinux disabled in kernel
  PERMISSIVE = 2,  ///< SELinux logging but not enforcing
  ENFORCING = 3    ///< SELinux fully enforcing
};

/**
 * @brief Convert SELinux mode to string.
 * @param mode Mode to convert.
 * @return Human-readable string.
 * @note RT-safe: Returns static string.
 */
[[nodiscard]] const char* toString(SelinuxMode mode) noexcept;

/* ----------------------------- ApparmorMode ----------------------------- */

/**
 * @brief AppArmor enforcement mode.
 */
enum class ApparmorMode : std::uint8_t {
  NOT_PRESENT = 0, ///< AppArmor not available
  DISABLED = 1,    ///< AppArmor disabled
  ENABLED = 2      ///< AppArmor enabled (profiles may be in complain or enforce)
};

/**
 * @brief Convert AppArmor mode to string.
 * @param mode Mode to convert.
 * @return Human-readable string.
 * @note RT-safe: Returns static string.
 */
[[nodiscard]] const char* toString(ApparmorMode mode) noexcept;

/* ----------------------------- SelinuxStatus ----------------------------- */

/**
 * @brief SELinux subsystem status.
 */
struct SelinuxStatus {
  SelinuxMode mode = SelinuxMode::NOT_PRESENT; ///< Current enforcement mode
  bool mcsEnabled = false;                     ///< Multi-Category Security enabled
  bool mlsEnabled = false;                     ///< Multi-Level Security enabled
  bool booleansPending = false;                ///< Boolean changes pending commit

  std::array<char, LSM_NAME_SIZE> policyType{}; ///< Policy type (targeted, mls, etc.)

  std::array<char, SECURITY_CONTEXT_SIZE> currentContext{}; ///< Current process context

  std::uint32_t policyVersion = 0; ///< Policy version number
  std::uint32_t denialCount = 0;   ///< AVC denial count (if available)

  /**
   * @brief Check if SELinux is present and active.
   * @return true if SELinux is in permissive or enforcing mode.
   */
  [[nodiscard]] bool isActive() const noexcept;

  /**
   * @brief Check if SELinux is enforcing.
   * @return true if mode is ENFORCING.
   */
  [[nodiscard]] bool isEnforcing() const noexcept;

  /**
   * @brief Format as human-readable string.
   * @return Formatted status.
   * @note NOT RT-safe: Returns std::string.
   */
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- ApparmorStatus ----------------------------- */

/**
 * @brief AppArmor subsystem status.
 */
struct ApparmorStatus {
  ApparmorMode mode = ApparmorMode::NOT_PRESENT; ///< Current mode

  std::uint32_t profilesLoaded = 0;   ///< Number of loaded profiles
  std::uint32_t profilesEnforce = 0;  ///< Profiles in enforce mode
  std::uint32_t profilesComplain = 0; ///< Profiles in complain mode

  /**
   * @brief Check if AppArmor is present and enabled.
   * @return true if AppArmor is enabled.
   */
  [[nodiscard]] bool isActive() const noexcept;

  /**
   * @brief Format as human-readable string.
   * @return Formatted status.
   * @note NOT RT-safe: Returns std::string.
   */
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- LsmInfo ----------------------------- */

/**
 * @brief Information about a single Linux Security Module.
 */
struct LsmInfo {
  std::array<char, LSM_NAME_SIZE> name{}; ///< LSM name (selinux, apparmor, etc.)
  bool active = false;                    ///< Whether LSM is active

  /**
   * @brief Format as human-readable string.
   * @return Formatted info.
   * @note NOT RT-safe: Returns std::string.
   */
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- SecurityStatus ----------------------------- */

/**
 * @brief Complete security subsystem status.
 */
struct SecurityStatus {
  SelinuxStatus selinux{};   ///< SELinux status
  ApparmorStatus apparmor{}; ///< AppArmor status

  std::array<LsmInfo, MAX_LSMS> lsms{}; ///< All detected LSMs
  std::size_t lsmCount = 0;             ///< Number of detected LSMs

  bool seccompAvailable = false;  ///< Seccomp filtering available
  bool landLockAvailable = false; ///< Landlock LSM available
  bool yamaPtrace = false;        ///< Yama ptrace restrictions enabled

  /**
   * @brief Check if any LSM is actively enforcing.
   * @return true if SELinux enforcing or AppArmor profiles in enforce mode.
   */
  [[nodiscard]] bool hasEnforcement() const noexcept;

  /**
   * @brief Get comma-separated list of active LSM names.
   * @return LSM names or "none".
   * @note NOT RT-safe: Returns std::string.
   */
  [[nodiscard]] std::string activeLsmList() const;

  /**
   * @brief Format as human-readable string.
   * @return Formatted status.
   * @note NOT RT-safe: Returns std::string.
   */
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Get complete security subsystem status.
 * @return Populated SecurityStatus structure.
 * @note NOT RT-safe: May allocate for string operations.
 */
[[nodiscard]] SecurityStatus getSecurityStatus() noexcept;

/**
 * @brief Get SELinux status only.
 * @return Populated SelinuxStatus structure.
 * @note Partially RT-safe: Core queries are RT-safe.
 */
[[nodiscard]] SelinuxStatus getSelinuxStatus() noexcept;

/**
 * @brief Get AppArmor status only.
 * @return Populated ApparmorStatus structure.
 * @note Partially RT-safe: Core queries are RT-safe.
 */
[[nodiscard]] ApparmorStatus getApparmorStatus() noexcept;

/**
 * @brief Check if SELinux is available on this system.
 * @return true if SELinux filesystem is mounted.
 * @note RT-safe.
 */
[[nodiscard]] bool selinuxAvailable() noexcept;

/**
 * @brief Check if AppArmor is available on this system.
 * @return true if AppArmor filesystem is mounted.
 * @note RT-safe.
 */
[[nodiscard]] bool apparmorAvailable() noexcept;

} // namespace system

} // namespace seeker

#endif // SEEKER_SYSTEM_SECURITY_STATUS_HPP