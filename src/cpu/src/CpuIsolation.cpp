/**
 * @file CpuIsolation.cpp
 * @brief Implementation of CPU isolation configuration queries.
 */

#include "src/cpu/inc/CpuIsolation.hpp"
#include "src/helpers/inc/Files.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstring>

namespace seeker {

namespace cpu {

using seeker::helpers::files::readFileToBuffer;

/* ----------------------------- File Helpers ----------------------------- */

namespace {

/// Find parameter value in cmdline (e.g., "rcu_nocbs=0-3" returns pointer to "0-3")
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

} // namespace

/* ----------------------------- CPU List Parser ----------------------------- */

CpuSet parseCpuList(const char* cpuList) noexcept {
  CpuSet result;
  if (cpuList == nullptr || cpuList[0] == '\0') {
    return result;
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
        // Invalid range, use single CPU
        end = START;
      } else {
        ptr = endPtr;
      }
    }

    // Set CPUs in range
    for (long cpu = START; cpu <= end && static_cast<std::size_t>(cpu) < MAX_CPUS; ++cpu) {
      result.set(static_cast<std::size_t>(cpu));
    }
  }

  return result;
}

/* ----------------------------- CpuIsolationConfig Methods ----------------------------- */

bool CpuIsolationConfig::isFullyIsolated(std::size_t cpuId) const noexcept {
  if (cpuId >= MAX_CPUS) {
    return false;
  }
  return isolcpus.test(cpuId) && nohzFull.test(cpuId) && rcuNocbs.test(cpuId);
}

bool CpuIsolationConfig::hasAnyIsolation() const noexcept {
  return !isolcpus.empty() || !nohzFull.empty() || !rcuNocbs.empty();
}

CpuSet CpuIsolationConfig::getFullyIsolatedCpus() const noexcept {
  CpuSet result;
  // Intersection of all three sets
  for (std::size_t i = 0; i < MAX_CPUS; ++i) {
    if (isolcpus.test(i) && nohzFull.test(i) && rcuNocbs.test(i)) {
      result.set(i);
    }
  }
  return result;
}

std::string CpuIsolationConfig::toString() const {
  std::string out;
  out.reserve(512);

  out += "CPU Isolation Configuration:\n";

  out += "  isolcpus:  ";
  if (isolcpus.empty()) {
    out += "(none)";
  } else {
    out += isolcpus.toString();
  }
  if (isolcpusManaged) {
    out += " [managed_irq]";
  }
  out += "\n";

  out += "  nohz_full: ";
  if (nohzFull.empty()) {
    out += "(none)";
  } else {
    out += nohzFull.toString();
  }
  if (nohzFullAll) {
    out += " [all]";
  }
  out += "\n";

  out += "  rcu_nocbs: ";
  if (rcuNocbs.empty()) {
    out += "(none)";
  } else {
    out += rcuNocbs.toString();
  }
  out += "\n";

  const CpuSet FULLY = getFullyIsolatedCpus();
  out += "  fully isolated: ";
  if (FULLY.empty()) {
    out += "(none)";
  } else {
    out += FULLY.toString();
  }
  out += "\n";

  return out;
}

/* ----------------------------- IsolationValidation Methods ----------------------------- */

bool IsolationValidation::isValid() const noexcept {
  return missingIsolcpus.empty() && missingNohzFull.empty() && missingRcuNocbs.empty();
}

std::string IsolationValidation::toString() const {
  std::string out;
  out.reserve(256);

  if (isValid()) {
    out = "Isolation validation: PASS (all requested CPUs fully isolated)\n";
    return out;
  }

  out = "Isolation validation: FAIL\n";

  if (!missingIsolcpus.empty()) {
    out += "  Missing isolcpus:  ";
    out += missingIsolcpus.toString();
    out += "\n";
  }

  if (!missingNohzFull.empty()) {
    out += "  Missing nohz_full: ";
    out += missingNohzFull.toString();
    out += "\n";
  }

  if (!missingRcuNocbs.empty()) {
    out += "  Missing rcu_nocbs: ";
    out += missingRcuNocbs.toString();
    out += "\n";
  }

  return out;
}

/* ----------------------------- Main API ----------------------------- */

CpuIsolationConfig getCpuIsolationConfig() noexcept {
  CpuIsolationConfig config;

  std::array<char, 256> buf{};

  // Read isolcpus from sysfs (authoritative runtime state)
  if (readFileToBuffer("/sys/devices/system/cpu/isolated", buf.data(), buf.size()) > 0) {
    config.isolcpus = parseCpuList(buf.data());
  }

  // Read nohz_full from sysfs
  if (readFileToBuffer("/sys/devices/system/cpu/nohz_full", buf.data(), buf.size()) > 0) {
    config.nohzFull = parseCpuList(buf.data());
  }

  // Read cmdline for rcu_nocbs and flags
  std::array<char, CMDLINE_MAX_SIZE> cmdline{};
  if (readFileToBuffer("/proc/cmdline", cmdline.data(), cmdline.size()) > 0) {
    // Parse rcu_nocbs
    const char* RCU_VAL = findCmdlineParam(cmdline.data(), "rcu_nocbs=");
    if (RCU_VAL != nullptr) {
      std::array<char, 256> rcuBuf{};
      extractValue(RCU_VAL, rcuBuf.data(), rcuBuf.size());
      config.rcuNocbs = parseCpuList(rcuBuf.data());
    }

    // Check for managed_irq flag in isolcpus
    const char* ISOLCPUS_VAL = findCmdlineParam(cmdline.data(), "isolcpus=");
    if (ISOLCPUS_VAL != nullptr) {
      std::array<char, 256> isolBuf{};
      extractValue(ISOLCPUS_VAL, isolBuf.data(), isolBuf.size());
      config.isolcpusManaged = (std::strstr(isolBuf.data(), "managed_irq") != nullptr);
    }

    // Check for nohz_full=all
    const char* NOHZ_VAL = findCmdlineParam(cmdline.data(), "nohz_full=");
    if (NOHZ_VAL != nullptr) {
      std::array<char, 64> nohzBuf{};
      extractValue(NOHZ_VAL, nohzBuf.data(), nohzBuf.size());
      config.nohzFullAll = (std::strcmp(nohzBuf.data(), "all") == 0);
    }
  }

  return config;
}

IsolationValidation validateIsolation(const CpuIsolationConfig& config,
                                      const CpuSet& rtCpus) noexcept {
  IsolationValidation result;

  for (std::size_t i = 0; i < MAX_CPUS; ++i) {
    if (!rtCpus.test(i)) {
      continue;
    }

    if (!config.isolcpus.test(i)) {
      result.missingIsolcpus.set(i);
    }
    if (!config.nohzFull.test(i)) {
      result.missingNohzFull.set(i);
    }
    if (!config.rcuNocbs.test(i)) {
      result.missingRcuNocbs.set(i);
    }
  }

  return result;
}

} // namespace cpu

} // namespace seeker