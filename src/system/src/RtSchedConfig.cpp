/**
 * @file RtSchedConfig.cpp
 * @brief Implementation of RT scheduling configuration collection.
 */

#include "src/system/inc/RtSchedConfig.hpp"
#include "src/helpers/inc/Files.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace system {

namespace {

using seeker::helpers::files::pathExists;
using seeker::helpers::files::readFileInt64;
using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::files::readFileUint64;

/// Read unsigned 32-bit integer from file.
std::uint32_t readUint32FromFile(const char* path, std::uint32_t defaultVal = 0) noexcept {
  std::array<char, 32> buf{};
  if (readFileToBuffer(path, buf.data(), buf.size()) == 0) {
    return defaultVal;
  }
  return static_cast<std::uint32_t>(std::strtoul(buf.data(), nullptr, 10));
}

/// Read boolean (0/1) from file.
bool readBoolFromFile(const char* path, bool defaultVal = false) noexcept {
  std::array<char, 8> buf{};
  if (readFileToBuffer(path, buf.data(), buf.size()) == 0) {
    return defaultVal;
  }
  return buf[0] == '1';
}

} // namespace

/* ----------------------------- RtBandwidth Methods ----------------------------- */

bool RtBandwidth::isUnlimited() const noexcept { return runtimeUs == RT_RUNTIME_UNLIMITED; }

double RtBandwidth::bandwidthPercent() const noexcept {
  if (runtimeUs == RT_RUNTIME_UNLIMITED || periodUs <= 0) {
    return 100.0;
  }
  if (runtimeUs <= 0) {
    return 0.0;
  }
  return (static_cast<double>(runtimeUs) / static_cast<double>(periodUs)) * 100.0;
}

bool RtBandwidth::isRtFriendly() const noexcept {
  return isUnlimited() || bandwidthPercent() >= 90.0;
}

std::int64_t RtBandwidth::quotaUs() const noexcept {
  if (runtimeUs == RT_RUNTIME_UNLIMITED) {
    return periodUs;
  }
  return runtimeUs;
}

std::string RtBandwidth::toString() const {
  if (!valid) {
    return "RT Bandwidth: not available";
  }

  if (isUnlimited()) {
    return fmt::format("RT Bandwidth: unlimited (period={}us)", periodUs);
  }

  return fmt::format("RT Bandwidth: {:.1f}% ({}us/{}us)", bandwidthPercent(), runtimeUs, periodUs);
}

/* ----------------------------- SchedTunables Methods ----------------------------- */

std::string SchedTunables::toString() const {
  if (!valid) {
    return "Scheduler Tunables: not available";
  }

  std::string out;
  out.reserve(256);

  out += "Scheduler Tunables:\n";
  out += fmt::format("  Min granularity:    {} ns\n", minGranularityNs);
  out += fmt::format("  Wakeup granularity: {} ns\n", wakeupGranularityNs);
  out += fmt::format("  Migration cost:     {} ns\n", migrationCostNs);
  out += fmt::format("  Latency:            {} ns\n", latencyNs);
  out += fmt::format("  Child runs first:   {}\n", childRunsFirst ? "yes" : "no");
  out += fmt::format("  Autogroup:          {}\n", autogroupEnabled ? "enabled" : "disabled");

  return out;
}

/* ----------------------------- RtSchedConfig Methods ----------------------------- */

bool RtSchedConfig::isRtFriendly() const noexcept {
  // RT-friendly if:
  // - Bandwidth is unlimited or >= 95%
  // - Autogroup is disabled (or not applicable)
  if (!bandwidth.isRtFriendly()) {
    return false;
  }

  // Autogroup can interfere with RT priority inheritance
  if (tunables.autogroupEnabled) {
    return false;
  }

  return true;
}

int RtSchedConfig::rtScore() const noexcept {
  int score = 50; // Base score

  // Bandwidth contribution (up to 30 points)
  if (bandwidth.isUnlimited()) {
    score += 30;
  } else {
    const double BW = bandwidth.bandwidthPercent();
    if (BW >= 99.0) {
      score += 28;
    } else if (BW >= 95.0) {
      score += 25;
    } else if (BW >= 90.0) {
      score += 20;
    } else if (BW >= 80.0) {
      score += 10;
    } else {
      score -= 10; // Low bandwidth is bad
    }
  }

  // Autogroup (up to 10 points)
  if (!tunables.autogroupEnabled) {
    score += 10;
  }

  // SCHED_DEADLINE support (5 points)
  if (hasSchedDeadline) {
    score += 5;
  }

  // RT group sched (mixed - can be good or bad)
  // No score change

  // Timer migration disabled is better for RT (5 points)
  if (!timerMigration) {
    score += 5;
  }

  // Clamp to 0-100
  if (score < 0)
    score = 0;
  if (score > 100)
    score = 100;

  return score;
}

bool RtSchedConfig::hasUnlimitedRt() const noexcept { return bandwidth.isUnlimited(); }

bool RtSchedConfig::hasAutogroupDisabled() const noexcept { return !tunables.autogroupEnabled; }

std::string RtSchedConfig::toString() const {
  std::string out;
  out.reserve(512);

  out += "RT Scheduling Configuration:\n\n";

  // Bandwidth section
  out += "RT Bandwidth:\n";
  if (bandwidth.valid) {
    out += fmt::format("  Period:      {} us\n", bandwidth.periodUs);
    if (bandwidth.isUnlimited()) {
      out += "  Runtime:     unlimited (throttling disabled)\n";
    } else {
      out += fmt::format("  Runtime:     {} us ({:.1f}%)\n", bandwidth.runtimeUs,
                         bandwidth.bandwidthPercent());
    }
    out += fmt::format("  RT-friendly: {}\n", bandwidth.isRtFriendly() ? "yes" : "no");
  } else {
    out += "  (not available)\n";
  }

  out += "\nScheduler Tunables:\n";
  if (tunables.valid) {
    out += fmt::format("  Min granularity:    {} us\n", tunables.minGranularityNs / 1000);
    out += fmt::format("  Wakeup granularity: {} us\n", tunables.wakeupGranularityNs / 1000);
    out += fmt::format("  Migration cost:     {} us\n", tunables.migrationCostNs / 1000);
    out += fmt::format("  CFS latency:        {} us\n", tunables.latencyNs / 1000);
    out += fmt::format("  Autogroup:          {}\n",
                       tunables.autogroupEnabled ? "enabled" : "disabled");
  } else {
    out += "  (not available)\n";
  }

  out += "\nKernel Features:\n";
  out += fmt::format("  RT group sched:   {}\n", hasRtGroupSched ? "yes" : "no");
  out += fmt::format("  CFS bandwidth:    {}\n", hasCfsBandwidth ? "yes" : "no");
  out += fmt::format("  SCHED_DEADLINE:   {}\n", hasSchedDeadline ? "yes" : "no");
  out += fmt::format("  Timer migration:  {}\n", timerMigration ? "enabled" : "disabled");

  out += fmt::format("\nRT Score: {}/100 ({})\n", rtScore(),
                     isRtFriendly() ? "RT-friendly" : "review recommended");

  return out;
}

/* ----------------------------- API ----------------------------- */

RtBandwidth getRtBandwidth() noexcept {
  RtBandwidth bw{};

  // Read RT period
  const std::int64_t PERIOD = readFileInt64("/proc/sys/kernel/sched_rt_period_us", -1);
  if (PERIOD < 0) {
    return bw; // File not available
  }
  bw.periodUs = PERIOD;

  // Read RT runtime
  bw.runtimeUs = readFileInt64("/proc/sys/kernel/sched_rt_runtime_us", DEFAULT_RT_RUNTIME_US);

  bw.valid = true;
  return bw;
}

SchedTunables getSchedTunables() noexcept {
  SchedTunables tunables{};

  // These files should exist on any Linux system with CFS
  tunables.minGranularityNs = readFileUint64("/proc/sys/kernel/sched_min_granularity_ns", 0);
  tunables.wakeupGranularityNs = readFileUint64("/proc/sys/kernel/sched_wakeup_granularity_ns", 0);
  tunables.migrationCostNs = readFileUint64("/proc/sys/kernel/sched_migration_cost_ns", 0);
  tunables.latencyNs = readFileUint64("/proc/sys/kernel/sched_latency_ns", 0);

  tunables.nrMigrate = readUint32FromFile("/proc/sys/kernel/sched_nr_migrate", 0);

  tunables.childRunsFirst = readBoolFromFile("/proc/sys/kernel/sched_child_runs_first", false);
  tunables.autogroupEnabled = readBoolFromFile("/proc/sys/kernel/sched_autogroup_enabled", false);

  // Consider valid if we could read at least one tunable
  tunables.valid = (tunables.minGranularityNs > 0 || tunables.latencyNs > 0);

  return tunables;
}

bool isRtThrottlingDisabled() noexcept {
  const std::int64_t RUNTIME = readFileInt64("/proc/sys/kernel/sched_rt_runtime_us", 0);
  return RUNTIME == RT_RUNTIME_UNLIMITED;
}

double getRtBandwidthPercent() noexcept {
  const RtBandwidth BW = getRtBandwidth();
  return BW.bandwidthPercent();
}

RtSchedConfig getRtSchedConfig() noexcept {
  RtSchedConfig config{};

  // Get bandwidth config
  config.bandwidth = getRtBandwidth();

  // Get scheduler tunables
  config.tunables = getSchedTunables();

  // Detect kernel features by checking for related sysfs/procfs entries

  // RT group sched: if we can read rt_runtime from a cgroup, it's enabled
  config.hasRtGroupSched = pathExists("/sys/fs/cgroup/cpu/cpu.rt_runtime_us") ||
                           pathExists("/sys/fs/cgroup/cpu,cpuacct/cpu.rt_runtime_us");

  // CFS bandwidth: check for cfs_period_us
  config.hasCfsBandwidth = pathExists("/sys/fs/cgroup/cpu/cpu.cfs_period_us") ||
                           pathExists("/sys/fs/cgroup/cpu,cpuacct/cpu.cfs_period_us");

  // SCHED_DEADLINE: check /proc/sys/kernel/sched_deadline_*
  config.hasSchedDeadline = pathExists("/proc/sys/kernel/sched_deadline_period_max_us") ||
                            pathExists("/proc/sys/kernel/sched_dl_period_max");

  // Timer migration
  config.timerMigration = readBoolFromFile("/proc/sys/kernel/timer_migration", true);

  // RT statistics (best effort - may require debug fs)
  // These are often not available without root/debug
  config.rtTasksRunnable = 0;
  config.rtThrottleCount = 0;

  // Try to read from /proc/sched_debug if available
  std::array<char, 4096> debugBuf{};
  if (readFileToBuffer("/proc/sched_debug", debugBuf.data(), debugBuf.size()) > 0) {
    // Parse RT throttle count if present
    const char* THROTTLE_POS = std::strstr(debugBuf.data(), "rt_throttled:");
    if (THROTTLE_POS != nullptr) {
      config.rtThrottleCount = std::strtoull(THROTTLE_POS + 13, nullptr, 10);
    }
  }

  return config;
}

} // namespace system

} // namespace seeker