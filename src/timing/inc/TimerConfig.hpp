#ifndef SEEKER_TIMING_TIMER_CONFIG_HPP
#define SEEKER_TIMING_TIMER_CONFIG_HPP
/**
 * @file TimerConfig.hpp
 * @brief Timer slack, high-resolution timers, and tickless configuration (Linux).
 * @note Linux-only. Reads /proc/cmdline, /sys/devices/system/cpu/.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides timer configuration information critical for RT systems:
 *  - Process timer_slack (affects sleep precision)
 *  - High-resolution timer status
 *  - Tickless/NO_HZ configuration (reduces timer interrupts on RT cores)
 *
 * Timer slack causes sleep calls to be coalesced within the slack window,
 * which saves power but adds jitter. RT processes typically want slack = 1ns.
 */

#include <array>   // std::array
#include <bitset>  // std::bitset
#include <cstddef> // std::size_t
#include <cstdint> // std::uint64_t
#include <string>  // std::string

namespace seeker {

namespace timing {

/* ----------------------------- Constants ----------------------------- */

/// Maximum number of CPUs for NO_HZ tracking.
inline constexpr std::size_t MAX_NOHZ_CPUS = 256;

/// Default timer slack on most Linux systems (50 microseconds).
inline constexpr std::uint64_t DEFAULT_TIMER_SLACK_NS = 50'000;

/* ----------------------------- TimerConfig ----------------------------- */

/**
 * @brief Timer-related kernel and process configuration.
 *
 * Captures settings that affect timer behavior and sleep precision.
 * These are critical for achieving consistent latencies in RT systems.
 */
struct TimerConfig {
  // Process-level timer slack
  std::uint64_t timerSlackNs{0};   ///< Current process timer_slack_ns (0 = query failed)
  bool slackQuerySucceeded{false}; ///< True if prctl query succeeded

  // High-resolution timer status
  bool highResTimersEnabled{false}; ///< True if high-res timers active (from clock_getres)

  // Tickless/NO_HZ configuration (from kernel cmdline and sysfs)
  bool nohzFullEnabled{false};               ///< nohz_full= present in cmdline
  std::bitset<MAX_NOHZ_CPUS> nohzFullCpus{}; ///< CPUs with nohz_full
  std::size_t nohzFullCount{0};              ///< Number of nohz_full CPUs

  // Additional kernel parameters
  bool nohzIdleEnabled{false};  ///< nohz=on or default tickless idle
  bool preemptRtEnabled{false}; ///< PREEMPT_RT kernel detected

  /// @brief Check if timer slack is minimal (1ns or explicit zero).
  [[nodiscard]] bool hasMinimalSlack() const noexcept;

  /// @brief Check if timer slack is at default (around 50us).
  [[nodiscard]] bool hasDefaultSlack() const noexcept;

  /// @brief Check if CPU has nohz_full configured.
  [[nodiscard]] bool isNohzFullCpu(std::size_t cpuId) const noexcept;

  /// @brief Check if configuration is optimal for RT.
  /// Requires: minimal slack, high-res timers, nohz_full on at least one CPU.
  [[nodiscard]] bool isOptimalForRt() const noexcept;

  /// @brief Get RT suitability score (0-100).
  [[nodiscard]] int rtScore() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query current timer configuration.
 * @return Populated TimerConfig with current settings.
 * @note RT-safe: Bounded syscalls and file reads, fixed-size output.
 *
 * Sources:
 *  - prctl(PR_GET_TIMERSLACK) - Process timer slack
 *  - clock_getres(CLOCK_MONOTONIC) - High-res timer detection
 *  - /sys/devices/system/cpu/nohz_full - Tickless CPUs
 *  - /proc/cmdline - Kernel parameters (nohz_full=, etc.)
 *  - /sys/kernel/realtime - PREEMPT_RT detection
 */
[[nodiscard]] TimerConfig getTimerConfig() noexcept;

/**
 * @brief Get current process timer slack.
 * @return Timer slack in nanoseconds, 0 on query failure.
 * @note RT-safe: Single syscall.
 *
 * The timer slack value affects how sleep calls are coalesced.
 * A value of 1 means minimal coalescing (precise sleeps).
 * Default is typically 50,000 ns (50 microseconds).
 */
[[nodiscard]] std::uint64_t getTimerSlackNs() noexcept;

/**
 * @brief Set current process timer slack.
 * @param slackNs Desired timer slack in nanoseconds (1 = minimal).
 * @return True on success, false on failure.
 * @note RT-safe: Single syscall.
 *
 * For RT applications, call setTimerSlack(1) at startup to minimize
 * sleep jitter. This requires no special privileges.
 */
[[nodiscard]] bool setTimerSlackNs(std::uint64_t slackNs) noexcept;

/**
 * @brief Check if PREEMPT_RT kernel is running.
 * @return True if running on PREEMPT_RT kernel.
 * @note RT-safe: Single file check.
 */
[[nodiscard]] bool isPreemptRtKernel() noexcept;

} // namespace timing

} // namespace seeker

#endif // SEEKER_TIMING_TIMER_CONFIG_HPP
