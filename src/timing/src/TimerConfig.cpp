/**
 * @file TimerConfig.cpp
 * @brief Implementation of timer configuration queries.
 */

#include "src/timing/inc/TimerConfig.hpp"
#include "src/helpers/inc/Files.hpp"

#include <fcntl.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace timing {

/* ----------------------------- File Helpers ----------------------------- */

namespace {

using seeker::helpers::files::readFileToBuffer;

/// Check if file exists using access().
bool fileExists(const char* path) noexcept { return ::access(path, F_OK) == 0; }

/// Parse CPU list string (e.g., "0,2-4,6") into bitset.
void parseCpuListToBitset(const char* cpuList, std::bitset<MAX_NOHZ_CPUS>& out,
                          std::size_t& count) noexcept {
  out.reset();
  count = 0;

  if (cpuList == nullptr || cpuList[0] == '\0') {
    return;
  }

  const char* ptr = cpuList;

  while (*ptr != '\0') {
    // Skip whitespace and commas
    while (*ptr == ' ' || *ptr == ',' || *ptr == '\t') {
      ++ptr;
    }
    if (*ptr == '\0') {
      break;
    }

    // Parse first number
    char* endPtr = nullptr;
    const long START = std::strtol(ptr, &endPtr, 10);
    if (endPtr == ptr || START < 0) {
      // Invalid number, skip to next comma or end
      while (*ptr != '\0' && *ptr != ',') {
        ++ptr;
      }
      continue;
    }
    ptr = endPtr;

    long end = START;

    // Check for range
    if (*ptr == '-') {
      ++ptr;
      end = std::strtol(ptr, &endPtr, 10);
      if (endPtr == ptr || end < START) {
        end = START;
      } else {
        ptr = endPtr;
      }
    }

    // Set CPUs in range
    for (long cpu = START; cpu <= end && static_cast<std::size_t>(cpu) < MAX_NOHZ_CPUS; ++cpu) {
      if (!out.test(static_cast<std::size_t>(cpu))) {
        out.set(static_cast<std::size_t>(cpu));
        ++count;
      }
    }
  }
}

/// Find parameter value in cmdline (e.g., "nohz_full=0-3" returns pointer to "0-3")
const char* findCmdlineParam(const char* cmdline, const char* param) noexcept {
  const std::size_t PARAM_LEN = std::strlen(param);
  const char* pos = cmdline;

  while ((pos = std::strstr(pos, param)) != nullptr) {
    // Verify it's a word boundary (start of string or preceded by space)
    if (pos != cmdline && *(pos - 1) != ' ') {
      pos += PARAM_LEN;
      continue;
    }
    // Return pointer to value after '='
    return pos + PARAM_LEN;
  }
  return nullptr;
}

/// Extract value until space or end of string into buffer
void extractValue(const char* start, char* out, std::size_t outSize) noexcept {
  if (outSize == 0) {
    return;
  }

  std::size_t i = 0;
  while (i < outSize - 1 && start[i] != '\0' && start[i] != ' ' && start[i] != '\n') {
    out[i] = start[i];
    ++i;
  }
  out[i] = '\0';
}

/// Check if high-res timers are active (CLOCK_MONOTONIC resolution <= 1us).
bool checkHighResTimers() noexcept {
  struct timespec ts{};
  if (::clock_getres(CLOCK_MONOTONIC, &ts) == 0) {
    const long RESOLUTION_NS = ts.tv_sec * 1'000'000'000L + ts.tv_nsec;
    return RESOLUTION_NS <= 1000; // <= 1 microsecond
  }
  return false;
}

} // namespace

/* ----------------------------- TimerConfig Methods ----------------------------- */

bool TimerConfig::hasMinimalSlack() const noexcept {
  // Minimal slack is 1ns (kernel minimum) or explicit 0 from failed query
  return slackQuerySucceeded && timerSlackNs <= 1;
}

bool TimerConfig::hasDefaultSlack() const noexcept {
  // Default is typically around 50us (50,000 ns)
  if (!slackQuerySucceeded) {
    return false;
  }
  // Allow some tolerance: 40us to 60us
  return timerSlackNs >= 40'000 && timerSlackNs <= 60'000;
}

bool TimerConfig::isNohzFullCpu(std::size_t cpuId) const noexcept {
  if (cpuId >= MAX_NOHZ_CPUS) {
    return false;
  }
  return nohzFullCpus.test(cpuId);
}

bool TimerConfig::isOptimalForRt() const noexcept {
  // Optimal: minimal slack + high-res timers + at least one nohz_full CPU
  return hasMinimalSlack() && highResTimersEnabled && nohzFullCount > 0;
}

int TimerConfig::rtScore() const noexcept {
  int score = 0;

  // Timer slack component (0-40 points)
  if (slackQuerySucceeded) {
    if (timerSlackNs <= 1) {
      score += 40; // Minimal slack (ideal)
    } else if (timerSlackNs <= 1000) {
      score += 30; // <= 1us (good)
    } else if (timerSlackNs <= 10'000) {
      score += 20; // <= 10us (acceptable)
    } else if (timerSlackNs <= 50'000) {
      score += 10; // Default range
    }
    // > 50us gets 0
  }

  // High-res timer component (0-30 points)
  if (highResTimersEnabled) {
    score += 30;
  }

  // NO_HZ/tickless component (0-20 points)
  if (nohzFullCount > 0) {
    score += 20;
  } else if (nohzIdleEnabled) {
    score += 10;
  }

  // PREEMPT_RT component (0-10 points)
  if (preemptRtEnabled) {
    score += 10;
  }

  // Cap at 100
  if (score > 100) {
    score = 100;
  }

  return score;
}

std::string TimerConfig::toString() const {
  std::string out;
  out.reserve(512);

  out += "Timer Configuration:\n";

  // Timer slack
  out += "  Timer Slack: ";
  if (slackQuerySucceeded) {
    if (timerSlackNs == 1) {
      out += "1 ns [minimal]";
    } else if (timerSlackNs < 1000) {
      out += fmt::format("{} ns [low]", timerSlackNs);
    } else if (timerSlackNs < 1'000'000) {
      out += fmt::format("{:.1f} us", static_cast<double>(timerSlackNs) / 1000.0);
      if (hasDefaultSlack()) {
        out += " [default]";
      }
    } else {
      out += fmt::format("{:.1f} ms", static_cast<double>(timerSlackNs) / 1'000'000.0);
    }
  } else {
    out += "(query failed)";
  }
  out += "\n";

  // High-res timers
  out += fmt::format("  High-Res Timers: {}\n", highResTimersEnabled ? "enabled" : "disabled");

  // PREEMPT_RT
  out += fmt::format("  PREEMPT_RT: {}\n", preemptRtEnabled ? "yes" : "no");

  // NO_HZ configuration
  out += "  Tickless Mode:\n";
  out += fmt::format("    nohz_idle: {}\n", nohzIdleEnabled ? "enabled" : "disabled");
  out += "    nohz_full: ";
  if (nohzFullCount > 0) {
    out += fmt::format("{} CPUs (", nohzFullCount);
    bool first = true;
    for (std::size_t i = 0; i < MAX_NOHZ_CPUS; ++i) {
      if (nohzFullCpus.test(i)) {
        if (!first) {
          out += ",";
        }
        out += std::to_string(i);
        first = false;
      }
    }
    out += ")";
  } else {
    out += "(none)";
  }
  out += "\n";

  out += fmt::format("  RT Score: {}/100\n", rtScore());

  return out;
}

/* ----------------------------- API ----------------------------- */

TimerConfig getTimerConfig() noexcept {
  TimerConfig config;

  // Query timer slack via prctl
  const long SLACK = ::prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0);
  if (SLACK >= 0) {
    config.timerSlackNs = static_cast<std::uint64_t>(SLACK);
    config.slackQuerySucceeded = true;
  }

  // Check high-res timer status
  config.highResTimersEnabled = checkHighResTimers();

  // Check for PREEMPT_RT kernel
  config.preemptRtEnabled = isPreemptRtKernel();

  // Read nohz_full from sysfs
  std::array<char, 256> buf{};
  if (readFileToBuffer("/sys/devices/system/cpu/nohz_full", buf.data(), buf.size()) > 0) {
    config.nohzFullEnabled = true;
    parseCpuListToBitset(buf.data(), config.nohzFullCpus, config.nohzFullCount);
  }

  // Parse kernel cmdline for additional parameters
  std::array<char, 4096> cmdline{};
  if (readFileToBuffer("/proc/cmdline", cmdline.data(), cmdline.size()) > 0) {
    // Check for nohz= or nohz_full= parameters
    if (std::strstr(cmdline.data(), "nohz=on") != nullptr ||
        std::strstr(cmdline.data(), "nohz ") != nullptr) {
      config.nohzIdleEnabled = true;
    }

    // If sysfs didn't have nohz_full, try cmdline
    if (!config.nohzFullEnabled) {
      const char* NOHZ_VAL = findCmdlineParam(cmdline.data(), "nohz_full=");
      if (NOHZ_VAL != nullptr) {
        std::array<char, 128> nohzBuf{};
        extractValue(NOHZ_VAL, nohzBuf.data(), nohzBuf.size());
        if (nohzBuf[0] != '\0') {
          config.nohzFullEnabled = true;
          parseCpuListToBitset(nohzBuf.data(), config.nohzFullCpus, config.nohzFullCount);
        }
      }
    }
  }

  // Default: assume nohz_idle is enabled on modern kernels (3.10+)
  // unless explicitly disabled
  if (std::strstr(cmdline.data(), "nohz=off") == nullptr) {
    config.nohzIdleEnabled = true;
  }

  return config;
}

std::uint64_t getTimerSlackNs() noexcept {
  const long SLACK = ::prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0);
  if (SLACK >= 0) {
    return static_cast<std::uint64_t>(SLACK);
  }
  return 0;
}

bool setTimerSlackNs(std::uint64_t slackNs) noexcept {
  // prctl expects unsigned long, cap value appropriately
  const unsigned long SLACK = static_cast<unsigned long>(slackNs);
  return ::prctl(PR_SET_TIMERSLACK, SLACK, 0, 0, 0) == 0;
}

bool isPreemptRtKernel() noexcept {
  // Check for /sys/kernel/realtime (present on PREEMPT_RT kernels)
  if (fileExists("/sys/kernel/realtime")) {
    std::array<char, 8> buf{};
    if (readFileToBuffer("/sys/kernel/realtime", buf.data(), buf.size()) > 0) {
      return buf[0] == '1';
    }
  }

  // Alternative: check /proc/version for PREEMPT RT string
  std::array<char, 512> version{};
  if (readFileToBuffer("/proc/version", version.data(), version.size()) > 0) {
    if (std::strstr(version.data(), "PREEMPT RT") != nullptr ||
        std::strstr(version.data(), "PREEMPT_RT") != nullptr) {
      return true;
    }
  }

  return false;
}

} // namespace timing

} // namespace seeker
