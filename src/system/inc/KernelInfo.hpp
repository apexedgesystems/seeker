#ifndef SEEKER_SYSTEM_KERNEL_INFO_HPP
#define SEEKER_SYSTEM_KERNEL_INFO_HPP
/**
 * @file KernelInfo.hpp
 * @brief Kernel version, preemption model, and RT configuration (Linux).
 * @note Linux-only. Reads /proc/version, /proc/cmdline, /sys/kernel/.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Use cases for RT systems:
 *  - Detect RT-PREEMPT kernel at startup
 *  - Verify RT-relevant cmdline parameters
 *  - Check kernel taint status before production deployment
 */

#include <array>   // std::array
#include <cstddef> // std::size_t
#include <cstdint> // std::uint8_t
#include <string>  // std::string

namespace seeker {

namespace system {

/* ----------------------------- Constants ----------------------------- */

/// Buffer size for kernel release string (e.g., "6.1.0-rt5-amd64").
inline constexpr std::size_t KERNEL_RELEASE_SIZE = 128;

/// Buffer size for full kernel version string.
inline constexpr std::size_t KERNEL_VERSION_SIZE = 256;

/// Buffer size for preemption model string.
inline constexpr std::size_t PREEMPT_MODEL_SIZE = 32;

/* ----------------------------- Enums ----------------------------- */

/**
 * @brief Kernel preemption model classification.
 *
 * Determines the kernel's preemption behavior, which directly impacts
 * worst-case latency for RT applications.
 */
enum class PreemptModel : std::uint8_t {
  UNKNOWN = 0, ///< Could not determine preemption model
  NONE,        ///< No forced preemption (server/throughput mode)
  VOLUNTARY,   ///< Voluntary preemption points only
  PREEMPT,     ///< Full preemption (standard desktop kernel)
  PREEMPT_RT,  ///< PREEMPT_RT realtime kernel (lowest latency)
};

/**
 * @brief Convert PreemptModel to human-readable string.
 * @param model Preemption model enum value.
 * @return Static string representation.
 * @note RT-safe: Returns pointer to static string.
 */
[[nodiscard]] const char* toString(PreemptModel model) noexcept;

/* ----------------------------- Main Struct ----------------------------- */

/**
 * @brief Kernel configuration snapshot for RT systems.
 *
 * Captures kernel identification, preemption model, RT-relevant boot
 * parameters, and taint status. All string fields are fixed-size arrays
 * to enable RT-safe collection.
 */
struct KernelInfo {
  /* --- Kernel identification --- */

  /// Kernel release (e.g., "6.1.0-rt5-amd64").
  std::array<char, KERNEL_RELEASE_SIZE> release{};

  /// Full kernel version string from /proc/version.
  std::array<char, KERNEL_VERSION_SIZE> version{};

  /* --- Preemption model --- */

  /// Classified preemption model.
  PreemptModel preempt{PreemptModel::UNKNOWN};

  /// Raw preemption model string (e.g., "preempt", "voluntary", "none").
  std::array<char, PREEMPT_MODEL_SIZE> preemptStr{};

  /// True if RT-PREEMPT patch detected (CONFIG_PREEMPT_RT=y).
  bool rtPreemptPatched{false};

  /* --- RT-relevant cmdline flags --- */

  /// nohz_full= detected (tickless operation for RT cores).
  bool nohzFull{false};

  /// isolcpus= detected (CPU isolation from scheduler).
  bool isolCpus{false};

  /// rcu_nocbs= detected (RCU callback offloading).
  bool rcuNocbs{false};

  /// skew_tick= detected (jitter reduction for timer interrupts).
  bool skewTick{false};

  /// tsc=reliable detected (TSC trusted for timekeeping).
  bool tscReliable{false};

  /// intel_idle.max_cstate= or processor.max_cstate= detected.
  bool cstateLimit{false};

  /// idle=poll detected (busy-wait instead of halt).
  bool idlePoll{false};

  /* --- Kernel taint status --- */

  /// Kernel taint mask from /proc/sys/kernel/tainted.
  int taintMask{0};

  /// True if kernel is tainted (taintMask != 0).
  bool tainted{false};

  /* --- Query helpers --- */

  /// @brief Check if this is an RT-capable kernel.
  /// @return True if PREEMPT_RT or standard PREEMPT model.
  [[nodiscard]] bool isRtKernel() const noexcept;

  /// @brief Check if this is specifically a PREEMPT_RT kernel.
  /// @return True if PREEMPT_RT patch is active.
  [[nodiscard]] bool isPreemptRt() const noexcept;

  /// @brief Check if RT-relevant cmdline flags are present.
  /// @return True if any of nohzFull, isolCpus, or rcuNocbs is set.
  [[nodiscard]] bool hasRtCmdlineFlags() const noexcept;

  /// @brief Get preemption model as string.
  /// @return Pointer to preemptStr if non-empty, else toString(preempt).
  [[nodiscard]] const char* preemptModelStr() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Collect kernel information from /proc and /sys.
 * @return Populated KernelInfo structure.
 * @note RT-safe: Bounded file reads, fixed-size output, no allocation.
 *
 * Sources:
 *  - /proc/sys/kernel/osrelease - Kernel release string
 *  - /proc/version - Full version string (includes preempt info)
 *  - /sys/kernel/realtime - RT-PREEMPT indicator (if present)
 *  - /proc/cmdline - Boot command line parameters
 *  - /proc/sys/kernel/tainted - Taint mask
 */
[[nodiscard]] KernelInfo getKernelInfo() noexcept;

} // namespace system

} // namespace seeker

#endif // SEEKER_SYSTEM_KERNEL_INFO_HPP