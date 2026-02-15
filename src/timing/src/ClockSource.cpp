/**
 * @file ClockSource.cpp
 * @brief Implementation of clocksource and timer resolution queries.
 */

#include "src/timing/inc/ClockSource.hpp"
#include "src/helpers/inc/Files.hpp"

#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace timing {

/* ----------------------------- File Helpers ----------------------------- */

namespace {

using seeker::helpers::files::readFileToBuffer;

/// Parse space-separated tokens from a string into fixed-size array.
template <std::size_t N, std::size_t M>
std::size_t parseTokens(const char* str, std::array<std::array<char, N>, M>& out) noexcept {
  std::size_t count = 0;
  const char* ptr = str;

  while (*ptr != '\0' && count < M) {
    // Skip whitespace
    while (*ptr != '\0' && std::isspace(static_cast<unsigned char>(*ptr))) {
      ++ptr;
    }
    if (*ptr == '\0') {
      break;
    }

    // Copy token
    std::size_t i = 0;
    while (*ptr != '\0' && !std::isspace(static_cast<unsigned char>(*ptr)) && i < N - 1) {
      out[count][i] = *ptr;
      ++i;
      ++ptr;
    }
    out[count][i] = '\0';

    if (i > 0) {
      ++count;
    }
  }

  return count;
}

/// Get clock resolution in nanoseconds for the given clock id.
std::int64_t clockResolutionNs(clockid_t clockId) noexcept {
  struct timespec ts{};
  if (::clock_getres(clockId, &ts) == 0) {
    constexpr std::int64_t NSEC_PER_SEC = 1'000'000'000LL;
    return static_cast<std::int64_t>(ts.tv_sec) * NSEC_PER_SEC +
           static_cast<std::int64_t>(ts.tv_nsec);
  }
  return 0;
}

/// Check if clock is available by attempting clock_getres.
bool clockAvailable(clockid_t clockId) noexcept {
  struct timespec ts{};
  return ::clock_getres(clockId, &ts) == 0;
}

/// Fill ClockResolution for a specific clock type.
ClockResolution getResolution(clockid_t clockId) noexcept {
  ClockResolution res;
  res.available = clockAvailable(clockId);
  if (res.available) {
    res.resolutionNs = clockResolutionNs(clockId);
  }
  return res;
}

} // namespace

/* ----------------------------- ClockResolution Methods ----------------------------- */

bool ClockResolution::isHighRes() const noexcept {
  // High-res if resolution is <= 1 microsecond
  return available && resolutionNs > 0 && resolutionNs <= 1000;
}

bool ClockResolution::isCoarse() const noexcept {
  // Coarse if resolution is > 1 millisecond
  return available && resolutionNs > 1'000'000;
}

/* ----------------------------- ClockSource Methods ----------------------------- */

bool ClockSource::isTsc() const noexcept { return std::strcmp(current.data(), "tsc") == 0; }

bool ClockSource::isHpet() const noexcept { return std::strcmp(current.data(), "hpet") == 0; }

bool ClockSource::isAcpiPm() const noexcept { return std::strcmp(current.data(), "acpi_pm") == 0; }

bool ClockSource::hasHighResTimers() const noexcept { return monotonic.isHighRes(); }

bool ClockSource::hasClockSource(const char* name) const noexcept {
  if (name == nullptr) {
    return false;
  }
  for (std::size_t i = 0; i < availableCount; ++i) {
    if (std::strcmp(available[i].data(), name) == 0) {
      return true;
    }
  }
  return false;
}

int ClockSource::rtScore() const noexcept {
  int score = 50; // Base score

  // Clocksource component (0-40 points)
  if (isTsc()) {
    score += 40; // TSC is ideal for RT
  } else if (isHpet()) {
    score += 20; // HPET is acceptable but higher latency
  } else if (isAcpiPm()) {
    score += 10; // acpi_pm is slow
  }
  // Unknown clocksource gets 0 bonus

  // High-res timer component (0-30 points)
  if (monotonic.available) {
    if (monotonic.resolutionNs <= 1) {
      score += 30; // 1ns resolution (ideal)
    } else if (monotonic.resolutionNs <= 1000) {
      score += 25; // <= 1us (good)
    } else if (monotonic.resolutionNs <= 10000) {
      score += 15; // <= 10us (acceptable)
    } else if (monotonic.resolutionNs <= 1'000'000) {
      score += 5; // <= 1ms (marginal)
    }
    // > 1ms gets 0 bonus
  }

  // MONOTONIC_RAW availability (0-10 points)
  if (monotonicRaw.available && monotonicRaw.isHighRes()) {
    score += 10;
  }

  // Cap at 100
  if (score > 100) {
    score = 100;
  }

  return score;
}

std::string ClockSource::toString() const {
  std::string out;
  out.reserve(512);

  out += "Clock Source Configuration:\n";

  // Current clocksource
  out += "  Current: ";
  if (current[0] != '\0') {
    out += current.data();
    if (isTsc()) {
      out += " [optimal]";
    } else if (isHpet()) {
      out += " [acceptable]";
    } else if (isAcpiPm()) {
      out += " [slow]";
    }
  } else {
    out += "(unknown)";
  }
  out += "\n";

  // Available clocksources
  out += "  Available: ";
  if (availableCount == 0) {
    out += "(none)";
  } else {
    for (std::size_t i = 0; i < availableCount; ++i) {
      if (i > 0) {
        out += ", ";
      }
      out += available[i].data();
    }
  }
  out += "\n";

  // Timer resolutions
  out += "  Resolutions:\n";

  auto formatRes = [](const ClockResolution& res, const char* name) -> std::string {
    if (!res.available) {
      return fmt::format("    {}: unavailable\n", name);
    }
    std::string tag;
    if (res.isHighRes()) {
      tag = " [high-res]";
    } else if (res.isCoarse()) {
      tag = " [coarse]";
    }
    return fmt::format("    {}: {} ns{}\n", name, res.resolutionNs, tag);
  };

  out += formatRes(monotonic, "MONOTONIC");
  out += formatRes(monotonicRaw, "MONOTONIC_RAW");
  out += formatRes(monotonicCoarse, "MONOTONIC_COARSE");
  out += formatRes(realtime, "REALTIME");
  out += formatRes(realtimeCoarse, "REALTIME_COARSE");
  out += formatRes(boottime, "BOOTTIME");

  out += fmt::format("  RT Score: {}/100\n", rtScore());

  return out;
}

/* ----------------------------- API ----------------------------- */

ClockSource getClockSource() noexcept {
  ClockSource cs;

  // Read current clocksource
  std::array<char, CLOCKSOURCE_NAME_SIZE> buf{};
  if (readFileToBuffer("/sys/devices/system/clocksource/clocksource0/current_clocksource",
                       buf.data(), buf.size()) > 0) {
    std::snprintf(cs.current.data(), CLOCKSOURCE_NAME_SIZE, "%s", buf.data());
  }

  // Read available clocksources
  std::array<char, 256> availBuf{};
  if (readFileToBuffer("/sys/devices/system/clocksource/clocksource0/available_clocksource",
                       availBuf.data(), availBuf.size()) > 0) {
    cs.availableCount = parseTokens(availBuf.data(), cs.available);
  }

  // Query timer resolutions for all clock types
  cs.monotonic = getResolution(CLOCK_MONOTONIC);
  cs.monotonicRaw = getResolution(CLOCK_MONOTONIC_RAW);
  cs.monotonicCoarse = getResolution(CLOCK_MONOTONIC_COARSE);
  cs.realtime = getResolution(CLOCK_REALTIME);
  cs.realtimeCoarse = getResolution(CLOCK_REALTIME_COARSE);
  cs.boottime = getResolution(CLOCK_BOOTTIME);

  return cs;
}

std::int64_t getClockResolutionNs(int clockId) noexcept {
  return clockResolutionNs(static_cast<clockid_t>(clockId));
}

} // namespace timing

} // namespace seeker
