/**
 * @file IoScheduler.cpp
 * @brief Implementation of I/O scheduler configuration queries.
 */

#include "src/storage/inc/IoScheduler.hpp"
#include "src/helpers/inc/Files.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace storage {

using seeker::helpers::files::readFileInt;
using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::strings::copyToBuffer;

namespace {

/* ----------------------------- Constants ----------------------------- */

constexpr const char* SYS_BLOCK = "/sys/block";
constexpr std::size_t PATH_BUF_SIZE = 512;
constexpr std::size_t READ_BUF_SIZE = 256;

/// Read sysfs block device queue attribute as int.
inline int readQueueInt(const char* device, const char* attr, int defaultVal = -1) noexcept {
  char path[PATH_BUF_SIZE];
  std::snprintf(path, sizeof(path), "%s/%s/queue/%s", SYS_BLOCK, device, attr);
  return readFileInt(path, defaultVal);
}

/// Read sysfs block device queue attribute into buffer.
inline std::size_t readQueueAttr(const char* device, const char* attr, char* buf,
                                 std::size_t bufSize) noexcept {
  char path[PATH_BUF_SIZE];
  std::snprintf(path, sizeof(path), "%s/%s/queue/%s", SYS_BLOCK, device, attr);
  return readFileToBuffer(path, buf, bufSize);
}

/// Copy string up to specified length.
inline void copyStringN(char* dest, std::size_t destSize, const char* src,
                        std::size_t srcLen) noexcept {
  if (destSize == 0) {
    return;
  }
  const std::size_t COPY_LEN = (srcLen < destSize - 1) ? srcLen : destSize - 1;
  std::memcpy(dest, src, COPY_LEN);
  dest[COPY_LEN] = '\0';
}

/**
 * Parse scheduler string from /sys/block/<dev>/queue/scheduler.
 * Format: "mq-deadline kyber [bfq] none"
 * Current scheduler is in brackets.
 *
 * @param schedStr The scheduler string from sysfs.
 * @param current Output buffer for current scheduler name.
 * @param currentSize Size of current buffer.
 * @param available Array of available scheduler names.
 * @param maxAvailable Maximum schedulers to store.
 * @return Number of available schedulers found.
 */
inline std::size_t parseSchedulerString(const char* schedStr, char* current,
                                        std::size_t currentSize,
                                        char (*available)[SCHEDULER_NAME_SIZE],
                                        std::size_t maxAvailable) noexcept {

  current[0] = '\0';
  std::size_t availCount = 0;

  if (schedStr == nullptr || schedStr[0] == '\0') {
    return 0;
  }

  const char* pos = schedStr;

  while (*pos != '\0' && availCount < maxAvailable) {
    // Skip whitespace
    while (*pos == ' ' || *pos == '\t') {
      ++pos;
    }

    if (*pos == '\0') {
      break;
    }

    // Check for [current] bracket
    const bool IS_CURRENT = (*pos == '[');
    if (IS_CURRENT) {
      ++pos;
    }

    // Find end of scheduler name
    const char* nameStart = pos;
    while (*pos != '\0' && *pos != ' ' && *pos != '\t' && *pos != ']') {
      ++pos;
    }

    const std::size_t NAME_LEN = static_cast<std::size_t>(pos - nameStart);
    if (NAME_LEN > 0) {
      // Store in available list
      copyStringN(available[availCount], SCHEDULER_NAME_SIZE, nameStart, NAME_LEN);
      ++availCount;

      // If this was current, copy to current buffer
      if (IS_CURRENT) {
        copyStringN(current, currentSize, nameStart, NAME_LEN);
      }
    }

    // Skip closing bracket if present
    if (*pos == ']') {
      ++pos;
    }
  }

  return availCount;
}

} // namespace

/* ----------------------------- IoSchedulerConfig Methods ----------------------------- */

bool IoSchedulerConfig::isNoneScheduler() const noexcept {
  return std::strcmp(current.data(), "none") == 0;
}

bool IoSchedulerConfig::isMqDeadline() const noexcept {
  return std::strcmp(current.data(), "mq-deadline") == 0;
}

bool IoSchedulerConfig::isRtFriendly() const noexcept {
  // "none" is best (NVMe), "mq-deadline" is good (provides latency bounds)
  return isNoneScheduler() || isMqDeadline();
}

bool IoSchedulerConfig::isReadAheadLow() const noexcept {
  // 128 KB or less is considered low for RT
  // 0 = disabled (best for RT random access)
  return readAheadKb >= 0 && readAheadKb <= 128;
}

bool IoSchedulerConfig::hasScheduler(const char* name) const noexcept {
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

int IoSchedulerConfig::rtScore() const noexcept {
  int score = 0;

  // Scheduler score (0-50 points)
  if (isNoneScheduler()) {
    score += 50; // Best: no kernel scheduling overhead
  } else if (isMqDeadline()) {
    score += 40; // Good: bounded latency
  } else if (std::strcmp(current.data(), "kyber") == 0) {
    score += 25; // Moderate: latency-focused but more overhead
  } else if (std::strcmp(current.data(), "bfq") == 0) {
    score += 10; // Fair queuing, high overhead
  }
  // Unknown schedulers get 0

  // Read-ahead score (0-20 points)
  if (readAheadKb == 0) {
    score += 20; // Disabled: best for RT
  } else if (readAheadKb > 0 && readAheadKb <= 128) {
    score += 15; // Low: acceptable
  } else if (readAheadKb > 0 && readAheadKb <= 512) {
    score += 5; // Medium: not ideal
  }
  // High read-ahead gets 0

  // Merge policy score (0-15 points)
  if (noMerges == 2) {
    score += 15; // Try-nomerge: best for latency
  } else if (noMerges == 1) {
    score += 10; // Nomerge: good but may waste bandwidth
  } else if (noMerges == 0) {
    score += 5; // Merge: throughput over latency
  }

  // Queue depth score (0-15 points)
  // Lower queue depth = more predictable latency
  if (nrRequests > 0 && nrRequests <= 32) {
    score += 15;
  } else if (nrRequests > 0 && nrRequests <= 128) {
    score += 10;
  } else if (nrRequests > 0 && nrRequests <= 256) {
    score += 5;
  }

  return score;
}

std::string IoSchedulerConfig::toString() const {
  std::string out;
  out += fmt::format("{}: scheduler={}", device.data(), current.data());

  if (availableCount > 1) {
    out += " (avail: ";
    for (std::size_t i = 0; i < availableCount; ++i) {
      if (i > 0) {
        out += ", ";
      }
      out += available[i].data();
    }
    out += ")";
  }

  out += fmt::format(" nr_requests={}", nrRequests);
  out += fmt::format(" read_ahead_kb={}", readAheadKb);

  if (maxSectorsKb > 0) {
    out += fmt::format(" max_sectors_kb={}", maxSectorsKb);
  }

  return out;
}

std::string IoSchedulerConfig::rtAssessment() const {
  std::string out;
  const int SCORE = rtScore();

  out += fmt::format("RT Score: {}/100\n", SCORE);

  // Scheduler assessment
  out += "  Scheduler: ";
  if (isNoneScheduler()) {
    out += "GOOD (none - minimal overhead)\n";
  } else if (isMqDeadline()) {
    out += "GOOD (mq-deadline - bounded latency)\n";
  } else if (std::strcmp(current.data(), "kyber") == 0) {
    out += "WARN (kyber - latency-focused but overhead)\n";
  } else if (std::strcmp(current.data(), "bfq") == 0) {
    out += "WARN (bfq - fair but high overhead)\n";
  } else {
    out += fmt::format("UNKNOWN ({})\n", current.data());
  }

  // Read-ahead assessment
  out += "  Read-ahead: ";
  if (readAheadKb == 0) {
    out += "GOOD (disabled)\n";
  } else if (readAheadKb > 0 && readAheadKb <= 128) {
    out += fmt::format("OK ({} KB)\n", readAheadKb);
  } else if (readAheadKb > 0) {
    out += fmt::format("WARN ({} KB - consider lowering)\n", readAheadKb);
  } else {
    out += "UNKNOWN\n";
  }

  // Queue depth assessment
  out += "  Queue depth: ";
  if (nrRequests > 0 && nrRequests <= 32) {
    out += fmt::format("GOOD ({} - low latency)\n", nrRequests);
  } else if (nrRequests > 0 && nrRequests <= 128) {
    out += fmt::format("OK ({} - moderate)\n", nrRequests);
  } else if (nrRequests > 0) {
    out += fmt::format("WARN ({} - consider lowering for RT)\n", nrRequests);
  } else {
    out += "UNKNOWN\n";
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

IoSchedulerConfig getIoSchedulerConfig(const char* device) noexcept {
  IoSchedulerConfig config{};
  if (device == nullptr || device[0] == '\0') {
    return config;
  }

  copyToBuffer(config.device.data(), SCHED_DEVICE_NAME_SIZE, device);

  char buf[READ_BUF_SIZE];

  // Parse scheduler (format: "mq-deadline kyber [bfq] none")
  if (readQueueAttr(device, "scheduler", buf, sizeof(buf)) > 0) {
    char availArray[MAX_SCHEDULERS][SCHEDULER_NAME_SIZE];
    config.availableCount = parseSchedulerString(buf, config.current.data(), SCHEDULER_NAME_SIZE,
                                                 availArray, MAX_SCHEDULERS);

    for (std::size_t i = 0; i < config.availableCount; ++i) {
      copyToBuffer(config.available[i].data(), SCHEDULER_NAME_SIZE, availArray[i]);
    }
  }

  // Queue parameters
  config.nrRequests = readQueueInt(device, "nr_requests");
  config.readAheadKb = readQueueInt(device, "read_ahead_kb");
  config.maxSectorsKb = readQueueInt(device, "max_sectors_kb");
  config.rqAffinity = readQueueInt(device, "rq_affinity");
  config.noMerges = readQueueInt(device, "nomerges");
  config.iostatsEnabled = (readQueueInt(device, "iostats", 0) != 0);
  config.addRandom = (readQueueInt(device, "add_random", 0) != 0);

  return config;
}

} // namespace storage

} // namespace seeker