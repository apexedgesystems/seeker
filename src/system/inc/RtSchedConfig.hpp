#ifndef SEEKER_SYSTEM_RT_SCHED_CONFIG_HPP
#define SEEKER_SYSTEM_RT_SCHED_CONFIG_HPP
/**
 * @file RtSchedConfig.hpp
 * @brief RT scheduling kernel configuration and tunables (Linux).
 * @note Linux-only. Reads /proc/sys/kernel/sched_* and related sysctls.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Use cases for RT systems:
 *  - Verify RT bandwidth throttling configuration
 *  - Check scheduler tunables affecting RT latency
 *  - Validate kernel config for production RT deployment
 *  - Detect RT throttling issues before they cause problems
 */

#include <cstddef> // std::size_t
#include <cstdint> // std::int64_t
#include <string>  // std::string

namespace seeker {

namespace system {

/* ----------------------------- Constants ----------------------------- */

/// Default RT period in microseconds (1 second).
inline constexpr std::int64_t DEFAULT_RT_PERIOD_US = 1000000;

/// Default RT runtime in microseconds (950ms = 95% of period).
inline constexpr std::int64_t DEFAULT_RT_RUNTIME_US = 950000;

/// RT runtime value indicating unlimited (-1).
inline constexpr std::int64_t RT_RUNTIME_UNLIMITED = -1;

/// Buffer size for scheduler name strings.
inline constexpr std::size_t SCHED_NAME_SIZE = 32;

/* ----------------------------- RtBandwidth ----------------------------- */

/**
 * @brief RT bandwidth throttling configuration.
 *
 * Controls how much CPU time RT tasks can consume to prevent them
 * from starving non-RT tasks. Critical for RT system configuration.
 */
struct RtBandwidth {
  /// RT period in microseconds (sched_rt_period_us).
  std::int64_t periodUs{DEFAULT_RT_PERIOD_US};

  /// RT runtime in microseconds per period (sched_rt_runtime_us).
  /// -1 means unlimited (no throttling).
  std::int64_t runtimeUs{DEFAULT_RT_RUNTIME_US};

  /// True if successfully read from kernel.
  bool valid{false};

  /* ----------------------------- Query Helpers ----------------------------- */

  /// @brief Check if RT throttling is disabled (runtime == -1).
  [[nodiscard]] bool isUnlimited() const noexcept;

  /// @brief Get RT bandwidth as percentage (0-100).
  /// @return Percentage of CPU time available to RT tasks.
  [[nodiscard]] double bandwidthPercent() const noexcept;

  /// @brief Check if bandwidth allows reasonable RT operation.
  /// @return True if >= 90% or unlimited.
  [[nodiscard]] bool isRtFriendly() const noexcept;

  /// @brief Get remaining quota per period in microseconds.
  /// @return Runtime quota (or period if unlimited).
  [[nodiscard]] std::int64_t quotaUs() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- SchedTunables ----------------------------- */

/**
 * @brief General scheduler tunables affecting latency.
 *
 * These kernel parameters affect scheduling latency even for RT tasks.
 */
struct SchedTunables {
  /// Minimum granularity for CFS (nanoseconds).
  std::uint64_t minGranularityNs{0};

  /// Wakeup granularity for CFS (nanoseconds).
  std::uint64_t wakeupGranularityNs{0};

  /// Migration cost (nanoseconds).
  std::uint64_t migrationCostNs{0};

  /// Latency target for CFS (nanoseconds).
  std::uint64_t latencyNs{0};

  /// Number of run queues.
  std::uint32_t nrMigrate{0};

  /// Child runs first after fork.
  bool childRunsFirst{false};

  /// Autogroup enabled (affects RT isolation).
  bool autogroupEnabled{false};

  /// True if successfully read from kernel.
  bool valid{false};

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- RtSchedConfig ----------------------------- */

/**
 * @brief Complete RT scheduling configuration snapshot.
 *
 * Aggregates RT bandwidth, scheduler tunables, and other kernel
 * parameters affecting real-time scheduling behavior.
 */
struct RtSchedConfig {
  /// RT bandwidth throttling configuration.
  RtBandwidth bandwidth{};

  /// General scheduler tunables.
  SchedTunables tunables{};

  /* ----------------------------- Kernel Config ----------------------------- */

  /// Kernel has CONFIG_RT_GROUP_SCHED (RT cgroup support).
  bool hasRtGroupSched{false};

  /// Kernel has CONFIG_CFS_BANDWIDTH (CFS bandwidth control).
  bool hasCfsBandwidth{false};

  /// Kernel has SCHED_DEADLINE support.
  bool hasSchedDeadline{false};

  /// Timer migration enabled (can affect RT latency).
  bool timerMigration{false};

  /* ----------------------------- RT Statistics ----------------------------- */

  /// Number of RT tasks currently runnable (from /proc/sched_debug).
  std::uint32_t rtTasksRunnable{0};

  /// Number of times RT throttling has occurred (if available).
  std::uint64_t rtThrottleCount{0};

  /* ----------------------------- Query Helpers ----------------------------- */

  /// @brief Check if configuration is suitable for RT workloads.
  /// @return True if bandwidth is RT-friendly and no problematic settings.
  [[nodiscard]] bool isRtFriendly() const noexcept;

  /// @brief RT readiness score (0-100).
  /// @return Score where 100 = optimal RT config.
  [[nodiscard]] int rtScore() const noexcept;

  /// @brief Check if RT throttling is disabled.
  [[nodiscard]] bool hasUnlimitedRt() const noexcept;

  /// @brief Check if autogroup is disabled (better for RT isolation).
  [[nodiscard]] bool hasAutogroupDisabled() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Query RT scheduling configuration.
 * @return Populated RtSchedConfig structure.
 * @note RT-safe: Bounded sysfs/procfs reads, no allocation.
 *
 * Sources:
 *  - /proc/sys/kernel/sched_rt_period_us
 *  - /proc/sys/kernel/sched_rt_runtime_us
 *  - /proc/sys/kernel/sched_* (various tunables)
 *  - /sys/kernel/debug/sched/ (if available)
 */
[[nodiscard]] RtSchedConfig getRtSchedConfig() noexcept;

/**
 * @brief Query RT bandwidth configuration only.
 * @return Populated RtBandwidth structure.
 * @note RT-safe: Two file reads.
 */
[[nodiscard]] RtBandwidth getRtBandwidth() noexcept;

/**
 * @brief Query scheduler tunables.
 * @return Populated SchedTunables structure.
 * @note RT-safe: Bounded file reads.
 */
[[nodiscard]] SchedTunables getSchedTunables() noexcept;

/**
 * @brief Check if RT throttling is currently disabled.
 * @return True if sched_rt_runtime_us == -1.
 * @note RT-safe: Single file read.
 */
[[nodiscard]] bool isRtThrottlingDisabled() noexcept;

/**
 * @brief Get RT bandwidth percentage.
 * @return Percentage (0-100) or 100 if unlimited.
 * @note RT-safe: Two file reads.
 */
[[nodiscard]] double getRtBandwidthPercent() noexcept;

} // namespace system

} // namespace seeker

#endif // SEEKER_SYSTEM_RT_SCHED_CONFIG_HPP