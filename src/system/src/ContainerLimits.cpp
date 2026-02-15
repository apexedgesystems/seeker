/**
 * @file ContainerLimits.cpp
 * @brief Implementation of container detection and cgroup limit queries.
 */

#include "src/system/inc/ContainerLimits.hpp"
#include "src/helpers/inc/Files.hpp"

#include <fcntl.h>  // open, O_RDONLY
#include <unistd.h> // read, close

#include <cstdlib> // strtoll
#include <cstring> // strstr, strncpy, strlen, strcmp

#include <fmt/core.h>

namespace seeker {

namespace system {

namespace {

using seeker::helpers::files::readFileToBuffer;

/// Check if file exists.
bool fileExists(const char* path) noexcept {
  const int FD = ::open(path, O_RDONLY | O_CLOEXEC);
  if (FD >= 0) {
    ::close(FD);
    return true;
  }
  return false;
}

/// Parse signed long long from string, return default on failure.
std::int64_t parseLongLong(const char* str, std::int64_t def) noexcept {
  if (str == nullptr || str[0] == '\0') {
    return def;
  }

  // Handle "max" as unlimited
  if (std::strcmp(str, "max") == 0) {
    return LIMIT_UNLIMITED;
  }

  char* endPtr = nullptr;
  const long long VAL = std::strtoll(str, &endPtr, 10);
  if (endPtr == str) {
    return def;
  }
  return static_cast<std::int64_t>(VAL);
}

/// Copy string safely with null termination.
template <std::size_t N> void safeCopy(std::array<char, N>& dest, const char* src) noexcept {
  if (src == nullptr) {
    dest[0] = '\0';
    return;
  }
  std::size_t i = 0;
  while (i < N - 1 && src[i] != '\0') {
    dest[i] = src[i];
    ++i;
  }
  dest[i] = '\0';
}

/* ----------------------------- Container Detection ----------------------------- */

/// Detect container runtime from cgroup content.
const char* detectRuntime(const char* cgroupContent) noexcept {
  if (cgroupContent == nullptr) {
    return "";
  }

  if (std::strstr(cgroupContent, "docker") != nullptr) {
    return "docker";
  }
  if (std::strstr(cgroupContent, "podman") != nullptr) {
    return "podman";
  }
  if (std::strstr(cgroupContent, "containerd") != nullptr) {
    return "containerd";
  }
  if (std::strstr(cgroupContent, "cri-o") != nullptr ||
      std::strstr(cgroupContent, "crio") != nullptr) {
    return "cri-o";
  }
  if (std::strstr(cgroupContent, "kubepods") != nullptr) {
    return "kubernetes";
  }
  if (std::strstr(cgroupContent, "lxc") != nullptr) {
    return "lxc";
  }

  return "unknown";
}

/// Extract container ID from cgroup path (typically 64 hex chars).
void extractContainerId(const char* cgroupContent, char* idOut, std::size_t idSize) noexcept {
  if (cgroupContent == nullptr || idSize == 0) {
    if (idSize > 0) {
      idOut[0] = '\0';
    }
    return;
  }

  // Look for 64-character hex string (container ID pattern)
  const char* ptr = cgroupContent;
  while (*ptr != '\0') {
    // Check if we have a hex character sequence
    std::size_t hexLen = 0;
    const char* start = ptr;

    while ((*ptr >= '0' && *ptr <= '9') || (*ptr >= 'a' && *ptr <= 'f') ||
           (*ptr >= 'A' && *ptr <= 'F')) {
      ++hexLen;
      ++ptr;
    }

    // Container IDs are typically 64 chars, but we accept 12+ (short form)
    if (hexLen >= 12) {
      std::size_t copyLen = hexLen;
      if (copyLen >= idSize) {
        copyLen = idSize - 1;
      }
      std::memcpy(idOut, start, copyLen);
      idOut[copyLen] = '\0';
      return;
    }

    if (*ptr != '\0') {
      ++ptr;
    }
  }

  idOut[0] = '\0';
}

/* ----------------------------- cgroup v2 Parsing ----------------------------- */

/// Parse cgroup v2 cpu.max (format: "quota period" or "max period").
void parseCgroupV2CpuMax(const char* content, std::int64_t& quotaUs,
                         std::int64_t& periodUs) noexcept {
  if (content == nullptr || content[0] == '\0') {
    return;
  }

  // Parse quota
  const char* ptr = content;
  if (std::strncmp(ptr, "max", 3) == 0) {
    quotaUs = LIMIT_UNLIMITED;
    ptr += 3;
  } else {
    char* endPtr = nullptr;
    quotaUs = static_cast<std::int64_t>(std::strtoll(ptr, &endPtr, 10));
    ptr = endPtr;
  }

  // Skip whitespace
  while (*ptr == ' ' || *ptr == '\t') {
    ++ptr;
  }

  // Parse period
  if (*ptr != '\0') {
    periodUs = static_cast<std::int64_t>(std::strtoll(ptr, nullptr, 10));
  }
}

} // namespace

/* ----------------------------- CgroupVersion toString ----------------------------- */

const char* toString(CgroupVersion version) noexcept {
  switch (version) {
  case CgroupVersion::V1:
    return "v1";
  case CgroupVersion::V2:
    return "v2";
  case CgroupVersion::HYBRID:
    return "hybrid";
  case CgroupVersion::UNKNOWN:
  default:
    return "unknown";
  }
}

/* ----------------------------- ContainerLimits Methods ----------------------------- */

double ContainerLimits::cpuQuotaPercent() const noexcept {
  if (cpuQuotaUs <= 0 || cpuPeriodUs <= 0) {
    return 0.0;
  }
  return (static_cast<double>(cpuQuotaUs) / static_cast<double>(cpuPeriodUs)) * 100.0;
}

bool ContainerLimits::hasCpuLimit() const noexcept {
  return cpuQuotaUs != LIMIT_UNLIMITED && cpuQuotaUs > 0;
}

bool ContainerLimits::hasMemoryLimit() const noexcept {
  return memMaxBytes != LIMIT_UNLIMITED && memMaxBytes > 0;
}

bool ContainerLimits::hasPidLimit() const noexcept {
  return pidsMax != LIMIT_UNLIMITED && pidsMax > 0;
}

bool ContainerLimits::hasCpusetLimit() const noexcept { return cpusetCpus[0] != '\0'; }

std::string ContainerLimits::toString() const {
  std::string out;
  out.reserve(512);

  out += "Container Limits:\n";
  out += fmt::format("  Detected:     {}\n", detected ? "yes" : "no");

  if (detected) {
    if (runtime[0] != '\0') {
      out += fmt::format("  Runtime:      {}\n", runtime.data());
    }
    if (containerId[0] != '\0') {
      out += fmt::format("  Container ID: {}\n", containerId.data());
    }
  }

  out += fmt::format("  cgroup:       {}\n", system::toString(cgroupVersion));

  out += "  CPU:\n";
  if (hasCpuLimit()) {
    out += fmt::format("    Quota:  {} us ({:.1f}% of 1 CPU)\n", cpuQuotaUs, cpuQuotaPercent());
    out += fmt::format("    Period: {} us\n", cpuPeriodUs);
  } else {
    out += "    Quota:  unlimited\n";
  }
  if (hasCpusetLimit()) {
    out += fmt::format("    Cpuset: {}\n", cpusetCpus.data());
  }

  out += "  Memory:\n";
  if (hasMemoryLimit()) {
    out += fmt::format("    Max:     {} bytes\n", memMaxBytes);
    if (memCurrentBytes != LIMIT_UNLIMITED) {
      out += fmt::format("    Current: {} bytes\n", memCurrentBytes);
    }
  } else {
    out += "    Max:     unlimited\n";
  }
  if (swapMaxBytes != LIMIT_UNLIMITED) {
    out += fmt::format("    Swap:    {} bytes\n", swapMaxBytes);
  }

  out += "  PIDs:\n";
  if (hasPidLimit()) {
    out += fmt::format("    Max:     {}\n", pidsMax);
  } else {
    out += "    Max:     unlimited\n";
  }
  if (pidsCurrent != LIMIT_UNLIMITED) {
    out += fmt::format("    Current: {}\n", pidsCurrent);
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

bool isRunningInContainer() noexcept {
  // Quick marker file checks
  if (fileExists("/.dockerenv") || fileExists("/run/.containerenv")) {
    return true;
  }

  // Check /proc/1/cgroup for container hints
  std::array<char, 2048> buf{};
  if (readFileToBuffer("/proc/1/cgroup", buf.data(), buf.size()) > 0) {
    if (std::strstr(buf.data(), "docker") != nullptr ||
        std::strstr(buf.data(), "podman") != nullptr ||
        std::strstr(buf.data(), "kubepods") != nullptr ||
        std::strstr(buf.data(), "containerd") != nullptr ||
        std::strstr(buf.data(), "lxc") != nullptr) {
      return true;
    }
  }

  return false;
}

CgroupVersion detectCgroupVersion() noexcept {
  const bool HAS_V2 = fileExists("/sys/fs/cgroup/cgroup.controllers");
  const bool HAS_V1 = fileExists("/sys/fs/cgroup/cpu/cpu.cfs_quota_us") ||
                      fileExists("/sys/fs/cgroup/memory/memory.limit_in_bytes");

  if (HAS_V2 && HAS_V1) {
    return CgroupVersion::HYBRID;
  }
  if (HAS_V2) {
    return CgroupVersion::V2;
  }
  if (HAS_V1) {
    return CgroupVersion::V1;
  }
  return CgroupVersion::UNKNOWN;
}

ContainerLimits getContainerLimits() noexcept {
  ContainerLimits limits{};

  // Container detection
  limits.detected = isRunningInContainer();

  // Get cgroup content for runtime/ID detection
  std::array<char, 2048> cgroupBuf{};
  if (readFileToBuffer("/proc/self/cgroup", cgroupBuf.data(), cgroupBuf.size()) > 0) {
    safeCopy(limits.runtime, detectRuntime(cgroupBuf.data()));
    extractContainerId(cgroupBuf.data(), limits.containerId.data(), limits.containerId.size());
  }

  // Fallback runtime detection from marker files
  if (limits.runtime[0] == '\0' || std::strcmp(limits.runtime.data(), "unknown") == 0) {
    if (fileExists("/.dockerenv")) {
      safeCopy(limits.runtime, "docker");
    } else if (fileExists("/run/.containerenv")) {
      safeCopy(limits.runtime, "podman");
    }
  }

  // Detect cgroup version
  limits.cgroupVersion = detectCgroupVersion();

  // Stack buffer for reading limit files
  std::array<char, 256> buf{};

  if (limits.cgroupVersion == CgroupVersion::V2 || limits.cgroupVersion == CgroupVersion::HYBRID) {
    // cgroup v2 paths

    // CPU (cpu.max format: "quota period" or "max period")
    if (readFileToBuffer("/sys/fs/cgroup/cpu.max", buf.data(), buf.size()) > 0) {
      parseCgroupV2CpuMax(buf.data(), limits.cpuQuotaUs, limits.cpuPeriodUs);
    }

    // Cpuset
    if (readFileToBuffer("/sys/fs/cgroup/cpuset.cpus", buf.data(), buf.size()) > 0) {
      safeCopy(limits.cpusetCpus, buf.data());
    }

    // Memory
    if (readFileToBuffer("/sys/fs/cgroup/memory.max", buf.data(), buf.size()) > 0) {
      limits.memMaxBytes = parseLongLong(buf.data(), LIMIT_UNLIMITED);
    }
    if (readFileToBuffer("/sys/fs/cgroup/memory.current", buf.data(), buf.size()) > 0) {
      limits.memCurrentBytes = parseLongLong(buf.data(), LIMIT_UNLIMITED);
    }
    if (readFileToBuffer("/sys/fs/cgroup/memory.swap.max", buf.data(), buf.size()) > 0) {
      limits.swapMaxBytes = parseLongLong(buf.data(), LIMIT_UNLIMITED);
    }

    // PIDs
    if (readFileToBuffer("/sys/fs/cgroup/pids.max", buf.data(), buf.size()) > 0) {
      limits.pidsMax = parseLongLong(buf.data(), LIMIT_UNLIMITED);
    }
    if (readFileToBuffer("/sys/fs/cgroup/pids.current", buf.data(), buf.size()) > 0) {
      limits.pidsCurrent = parseLongLong(buf.data(), LIMIT_UNLIMITED);
    }
  }

  // cgroup v1 fallback (or primary if v1-only)
  if (limits.cgroupVersion == CgroupVersion::V1 ||
      (limits.cgroupVersion == CgroupVersion::HYBRID && limits.cpuQuotaUs == LIMIT_UNLIMITED)) {
    // CPU
    if (readFileToBuffer("/sys/fs/cgroup/cpu/cpu.cfs_quota_us", buf.data(), buf.size()) > 0) {
      limits.cpuQuotaUs = parseLongLong(buf.data(), LIMIT_UNLIMITED);
    }
    if (readFileToBuffer("/sys/fs/cgroup/cpu/cpu.cfs_period_us", buf.data(), buf.size()) > 0) {
      limits.cpuPeriodUs = parseLongLong(buf.data(), LIMIT_UNLIMITED);
    }

    // Cpuset
    if (limits.cpusetCpus[0] == '\0') {
      if (readFileToBuffer("/sys/fs/cgroup/cpuset/cpuset.cpus", buf.data(), buf.size()) > 0) {
        safeCopy(limits.cpusetCpus, buf.data());
      }
    }

    // Memory
    if (limits.memMaxBytes == LIMIT_UNLIMITED) {
      if (readFileToBuffer("/sys/fs/cgroup/memory/memory.limit_in_bytes", buf.data(), buf.size()) >
          0) {
        limits.memMaxBytes = parseLongLong(buf.data(), LIMIT_UNLIMITED);
      }
    }
    if (limits.memCurrentBytes == LIMIT_UNLIMITED) {
      if (readFileToBuffer("/sys/fs/cgroup/memory/memory.usage_in_bytes", buf.data(), buf.size()) >
          0) {
        limits.memCurrentBytes = parseLongLong(buf.data(), LIMIT_UNLIMITED);
      }
    }
    if (limits.swapMaxBytes == LIMIT_UNLIMITED) {
      if (readFileToBuffer("/sys/fs/cgroup/memory/memory.memsw.limit_in_bytes", buf.data(),
                           buf.size()) > 0) {
        limits.swapMaxBytes = parseLongLong(buf.data(), LIMIT_UNLIMITED);
      }
    }

    // PIDs
    if (limits.pidsMax == LIMIT_UNLIMITED) {
      if (readFileToBuffer("/sys/fs/cgroup/pids/pids.max", buf.data(), buf.size()) > 0) {
        limits.pidsMax = parseLongLong(buf.data(), LIMIT_UNLIMITED);
      }
    }
    if (limits.pidsCurrent == LIMIT_UNLIMITED) {
      if (readFileToBuffer("/sys/fs/cgroup/pids/pids.current", buf.data(), buf.size()) > 0) {
        limits.pidsCurrent = parseLongLong(buf.data(), LIMIT_UNLIMITED);
      }
    }
  }

  return limits;
}

} // namespace system

} // namespace seeker