#ifndef SEEKER_SYSTEM_VIRTUALIZATION_INFO_HPP
#define SEEKER_SYSTEM_VIRTUALIZATION_INFO_HPP
/**
 * @file VirtualizationInfo.hpp
 * @brief VM and container virtualization environment detection (Linux).
 * @note Linux-only. Reads DMI, CPUID, cgroups, and environment indicators.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Use cases for RT systems:
 *  - Detect virtualization overhead affecting latency
 *  - Warn when running in environments unsuitable for hard RT
 *  - Identify hypervisor for performance tuning guidance
 *  - Distinguish VM vs container virtualization
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <cstdint> // std::uint8_t
#include <string>  // std::string

namespace seeker {

namespace system {

/* ----------------------------- Constants ----------------------------- */

/// Buffer size for virtualization type/name strings.
inline constexpr std::size_t VIRT_TYPE_SIZE = 32;

/// Buffer size for hypervisor/runtime names.
inline constexpr std::size_t VIRT_NAME_SIZE = 64;

/// Buffer size for product/system identifiers.
inline constexpr std::size_t VIRT_PRODUCT_SIZE = 128;

/* ----------------------------- Enums ----------------------------- */

/**
 * @brief Virtualization technology classification.
 */
enum class VirtType : std::uint8_t {
  NONE = 0,  ///< Bare metal (no virtualization detected)
  VM,        ///< Full virtual machine (hypervisor)
  CONTAINER, ///< Container (shared kernel)
  UNKNOWN,   ///< Virtualization detected but type unknown
};

/**
 * @brief Convert VirtType to human-readable string.
 * @param type Virtualization type enum value.
 * @return Static string representation.
 * @note RT-safe: Returns pointer to static string.
 */
[[nodiscard]] const char* toString(VirtType type) noexcept;

/**
 * @brief Known hypervisor types.
 */
enum class Hypervisor : std::uint8_t {
  NONE = 0,       ///< No hypervisor (bare metal)
  KVM,            ///< KVM/QEMU
  VMWARE,         ///< VMware (ESXi, Workstation, etc.)
  VIRTUALBOX,     ///< Oracle VirtualBox
  HYPERV,         ///< Microsoft Hyper-V
  XEN,            ///< Xen hypervisor
  PARALLELS,      ///< Parallels Desktop
  BHYVE,          ///< FreeBSD bhyve
  QNX,            ///< QNX Hypervisor
  ACRN,           ///< ACRN Hypervisor
  POWERVM,        ///< IBM PowerVM
  ZVM,            ///< IBM z/VM
  AWS_NITRO,      ///< AWS Nitro
  GOOGLE_COMPUTE, ///< Google Compute Engine
  AZURE,          ///< Microsoft Azure
  OTHER,          ///< Other/unknown hypervisor
};

/**
 * @brief Convert Hypervisor to human-readable string.
 * @param hv Hypervisor enum value.
 * @return Static string representation.
 * @note RT-safe: Returns pointer to static string.
 */
[[nodiscard]] const char* toString(Hypervisor hv) noexcept;

/**
 * @brief Known container runtimes.
 */
enum class ContainerRuntime : std::uint8_t {
  NONE = 0,       ///< Not running in container
  DOCKER,         ///< Docker
  PODMAN,         ///< Podman
  LXC,            ///< LXC/LXD
  SYSTEMD_NSPAWN, ///< systemd-nspawn
  RKT,            ///< rkt (CoreOS)
  OPENVZ,         ///< OpenVZ
  WSL,            ///< Windows Subsystem for Linux
  OTHER,          ///< Other/unknown container
};

/**
 * @brief Convert ContainerRuntime to human-readable string.
 * @param rt Container runtime enum value.
 * @return Static string representation.
 * @note RT-safe: Returns pointer to static string.
 */
[[nodiscard]] const char* toString(ContainerRuntime rt) noexcept;

/* ----------------------------- VirtualizationInfo ----------------------------- */

/**
 * @brief Complete virtualization environment information.
 *
 * Detects whether the system is running on bare metal, in a VM, or in a
 * container, and identifies the specific virtualization technology.
 */
struct VirtualizationInfo {
  /* ----------------------------- Classification ----------------------------- */

  /// Primary virtualization type.
  VirtType type{VirtType::NONE};

  /// Detected hypervisor (if VM).
  Hypervisor hypervisor{Hypervisor::NONE};

  /// Detected container runtime (if container).
  ContainerRuntime containerRuntime{ContainerRuntime::NONE};

  /* ----------------------------- Identification Strings ----------------------------- */

  /// Hypervisor vendor/name string from CPUID or DMI.
  std::array<char, VIRT_NAME_SIZE> hypervisorName{};

  /// Container runtime name string.
  std::array<char, VIRT_NAME_SIZE> containerName{};

  /// System product name from DMI (helps identify cloud instances).
  std::array<char, VIRT_PRODUCT_SIZE> productName{};

  /// System manufacturer from DMI.
  std::array<char, VIRT_PRODUCT_SIZE> manufacturer{};

  /// BIOS vendor (often indicates virtualization).
  std::array<char, VIRT_NAME_SIZE> biosVendor{};

  /* ----------------------------- Detection Flags ----------------------------- */

  /// True if CPUID hypervisor bit is set.
  bool cpuidHypervisor{false};

  /// True if DMI indicates virtual hardware.
  bool dmiVirtual{false};

  /// True if container indicators found.
  bool containerIndicators{false};

  /// True if running in nested virtualization.
  bool nested{false};

  /// True if paravirtualization detected.
  bool paravirt{false};

  /* ----------------------------- RT Impact Assessment ----------------------------- */

  /// Confidence in detection (0-100).
  int confidence{0};

  /// Estimated RT suitability (0=poor, 100=optimal).
  int rtSuitability{0};

  /* ----------------------------- Query Helpers ----------------------------- */

  /// @brief Check if running on bare metal.
  [[nodiscard]] bool isBareMetal() const noexcept;

  /// @brief Check if running in a VM.
  [[nodiscard]] bool isVirtualMachine() const noexcept;

  /// @brief Check if running in a container.
  [[nodiscard]] bool isContainer() const noexcept;

  /// @brief Check if any virtualization is detected.
  [[nodiscard]] bool isVirtualized() const noexcept;

  /// @brief Check if running in a cloud environment.
  [[nodiscard]] bool isCloud() const noexcept;

  /// @brief Check if environment is suitable for RT workloads.
  /// @return True if bare metal or known RT-friendly virtualization.
  [[nodiscard]] bool isRtSuitable() const noexcept;

  /// @brief Get virtualization description string.
  /// @return Human-readable description of environment.
  /// @note RT-safe: Returns pointer to internal buffer or static string.
  [[nodiscard]] const char* description() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Detect virtualization environment.
 * @return Populated VirtualizationInfo structure.
 * @note RT-safe: Bounded reads from sysfs/procfs, no allocation.
 *
 * Detection methods (in order):
 *  1. CPUID hypervisor leaf (if x86)
 *  2. DMI/SMBIOS product and BIOS strings
 *  3. Container indicators (/proc/1/cgroup, /.dockerenv, etc.)
 *  4. Kernel command line hints
 *  5. Device tree (for ARM/embedded)
 */
[[nodiscard]] VirtualizationInfo getVirtualizationInfo() noexcept;

/**
 * @brief Quick check if system is virtualized.
 * @return True if any virtualization detected.
 * @note RT-safe: Bounded checks.
 */
[[nodiscard]] bool isVirtualized() noexcept;

/**
 * @brief Quick check if running in a container.
 * @return True if container environment detected.
 * @note RT-safe: Bounded checks.
 */
[[nodiscard]] bool isContainerized() noexcept;

} // namespace system

} // namespace seeker

#endif // SEEKER_SYSTEM_VIRTUALIZATION_INFO_HPP