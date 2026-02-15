/**
 * @file MemoryStats.cpp
 * @brief Implementation of memory statistics and VM policy queries.
 */

#include "src/memory/inc/MemoryStats.hpp"
#include "src/helpers/inc/Files.hpp"

#include <fcntl.h>  // open, O_RDONLY
#include <unistd.h> // read, close

#include <algorithm> // std::min
#include <cstdio>    // snprintf
#include <cstdlib>   // strtol, strtoull
#include <cstring>   // strncmp, strlen, memcpy

#include <fmt/core.h>

namespace seeker {

namespace memory {

using seeker::helpers::files::readFileToBuffer;

namespace {

/* ----------------------------- File Helpers ----------------------------- */

/// Read single integer from a sysfs/procfs file. Returns defaultVal on failure.
inline int readIntFile(const char* path, int defaultVal) noexcept {
  std::array<char, 32> buf{};
  if (readFileToBuffer(path, buf.data(), buf.size()) == 0) {
    return defaultVal;
  }

  char* end = nullptr;
  const long VAL = std::strtol(buf.data(), &end, 10);
  if (end == buf.data()) {
    return defaultVal;
  }

  return static_cast<int>(VAL);
}

/// Read first line into fixed-size array, trim newline.
template <std::size_t N>
inline void readLineToArray(const char* path, std::array<char, N>& out) noexcept {
  out[0] = '\0';

  std::array<char, N> buf{};
  const std::size_t LEN = readFileToBuffer(path, buf.data(), buf.size());
  if (LEN == 0) {
    return;
  }

  // Copy up to first newline
  std::size_t copyLen = 0;
  while (copyLen < LEN && copyLen < N - 1 && buf[copyLen] != '\n' && buf[copyLen] != '\0') {
    out[copyLen] = buf[copyLen];
    ++copyLen;
  }
  out[copyLen] = '\0';
}

/* ----------------------------- Meminfo Parsing ----------------------------- */

/// Parse "FieldName:    12345 kB" format, return bytes.
inline std::uint64_t parseMemInfoKb(const char* line) noexcept {
  // Find colon
  const char* colon = std::strchr(line, ':');
  if (colon == nullptr) {
    return 0;
  }

  // Skip whitespace after colon
  const char* ptr = colon + 1;
  while (*ptr == ' ' || *ptr == '\t') {
    ++ptr;
  }

  // Parse number
  char* end = nullptr;
  const unsigned long long KB = std::strtoull(ptr, &end, 10);
  if (end == ptr) {
    return 0;
  }

  return static_cast<std::uint64_t>(KB) * 1024ULL;
}

/// Check if line starts with prefix.
inline bool lineStartsWith(const char* line, const char* prefix) noexcept {
  return std::strncmp(line, prefix, std::strlen(prefix)) == 0;
}

/// Parse /proc/meminfo and populate stats.
inline void parseMemInfo(MemoryStats& stats) noexcept {
  std::array<char, 4096> buf{};
  if (readFileToBuffer("/proc/meminfo", buf.data(), buf.size()) == 0) {
    return;
  }

  // Parse line by line
  const char* ptr = buf.data();
  while (*ptr != '\0') {
    // Find end of line
    const char* eol = ptr;
    while (*eol != '\0' && *eol != '\n') {
      ++eol;
    }

    // Parse known fields
    if (lineStartsWith(ptr, "MemTotal:")) {
      stats.totalBytes = parseMemInfoKb(ptr);
    } else if (lineStartsWith(ptr, "MemFree:")) {
      stats.freeBytes = parseMemInfoKb(ptr);
    } else if (lineStartsWith(ptr, "MemAvailable:")) {
      stats.availableBytes = parseMemInfoKb(ptr);
    } else if (lineStartsWith(ptr, "Buffers:")) {
      stats.buffersBytes = parseMemInfoKb(ptr);
    } else if (lineStartsWith(ptr, "Cached:")) {
      stats.cachedBytes += parseMemInfoKb(ptr);
    } else if (lineStartsWith(ptr, "SReclaimable:")) {
      stats.cachedBytes += parseMemInfoKb(ptr);
    } else if (lineStartsWith(ptr, "SwapTotal:")) {
      stats.swapTotalBytes = parseMemInfoKb(ptr);
    } else if (lineStartsWith(ptr, "SwapFree:")) {
      stats.swapFreeBytes = parseMemInfoKb(ptr);
    }

    // Move to next line
    ptr = (*eol == '\0') ? eol : eol + 1;
  }
}

} // namespace

/* ----------------------------- MemoryStats Methods ----------------------------- */

std::uint64_t MemoryStats::usedBytes() const noexcept {
  // Used = Total - Free - Buffers - Cached
  const std::uint64_t SUBTOTAL = freeBytes + buffersBytes + cachedBytes;
  return (totalBytes > SUBTOTAL) ? (totalBytes - SUBTOTAL) : 0;
}

std::uint64_t MemoryStats::swapUsedBytes() const noexcept {
  return (swapTotalBytes > swapFreeBytes) ? (swapTotalBytes - swapFreeBytes) : 0;
}

double MemoryStats::utilizationPercent() const noexcept {
  if (totalBytes == 0) {
    return 0.0;
  }
  return 100.0 * static_cast<double>(usedBytes()) / static_cast<double>(totalBytes);
}

double MemoryStats::swapUtilizationPercent() const noexcept {
  if (swapTotalBytes == 0) {
    return 0.0;
  }
  return 100.0 * static_cast<double>(swapUsedBytes()) / static_cast<double>(swapTotalBytes);
}

bool MemoryStats::isTHPEnabled() const noexcept {
  // THP is disabled if thpEnabled contains "[never]"
  // Format: "always madvise [never]" where brackets show current setting
  const char* ptr = thpEnabled.data();
  while (*ptr != '\0') {
    if (*ptr == '[') {
      // Check if this is "[never]"
      if (std::strncmp(ptr, "[never]", 7) == 0) {
        return false;
      }
      // Any other bracketed value means THP is enabled in some form
      return true;
    }
    ++ptr;
  }
  // If no brackets found, assume enabled if non-empty
  return thpEnabled[0] != '\0';
}

bool MemoryStats::isSwappinessLow() const noexcept {
  // Low swappiness (<=10) is generally recommended for RT systems
  return swappiness >= 0 && swappiness <= 10;
}

std::string MemoryStats::toString() const {
  // Forward declare formatBytes from PageSizes (or inline helper)
  auto fmtBytes = [](std::uint64_t bytes) -> std::string {
    if (bytes == 0) {
      return "0 B";
    }
    constexpr std::uint64_t GIB = 1024ULL * 1024 * 1024;
    constexpr std::uint64_t MIB = 1024ULL * 1024;
    constexpr std::uint64_t KIB = 1024ULL;

    if (bytes >= GIB) {
      return fmt::format("{:.1f} GiB", static_cast<double>(bytes) / static_cast<double>(GIB));
    }
    if (bytes >= MIB) {
      return fmt::format("{:.1f} MiB", static_cast<double>(bytes) / static_cast<double>(MIB));
    }
    if (bytes >= KIB) {
      return fmt::format("{:.1f} KiB", static_cast<double>(bytes) / static_cast<double>(KIB));
    }
    return fmt::format("{} B", bytes);
  };

  std::string out;
  out += fmt::format("RAM: {} total, {} used, {} available ({:.1f}% used)\n", fmtBytes(totalBytes),
                     fmtBytes(usedBytes()), fmtBytes(availableBytes), utilizationPercent());

  if (swapTotalBytes > 0) {
    out += fmt::format("Swap: {} total, {} used ({:.1f}% used)\n", fmtBytes(swapTotalBytes),
                       fmtBytes(swapUsedBytes()), swapUtilizationPercent());
  } else {
    out += "Swap: disabled\n";
  }

  out += fmt::format("VM policies:\n");
  out += fmt::format("  swappiness: {}{}\n", swappiness >= 0 ? std::to_string(swappiness) : "N/A",
                     isSwappinessLow() ? " (RT-friendly)" : "");
  out += fmt::format("  zone_reclaim_mode: {}\n",
                     zoneReclaimMode >= 0 ? std::to_string(zoneReclaimMode) : "N/A");
  out += fmt::format("  overcommit_memory: {}\n",
                     overcommitMemory >= 0 ? std::to_string(overcommitMemory) : "N/A");
  out += fmt::format("  THP enabled: {}{}\n", thpEnabled.data(),
                     isTHPEnabled() ? "" : " (disabled - RT-friendly)");
  out += fmt::format("  THP defrag:  {}\n", thpDefrag.data());

  return out;
}

/* ----------------------------- API ----------------------------- */

MemoryStats getMemoryStats() noexcept {
  MemoryStats stats{};

  // Parse /proc/meminfo for RAM and swap
  parseMemInfo(stats);

  // Read VM policy settings
  stats.swappiness = readIntFile("/proc/sys/vm/swappiness", -1);
  stats.zoneReclaimMode = readIntFile("/proc/sys/vm/zone_reclaim_mode", -1);
  stats.overcommitMemory = readIntFile("/proc/sys/vm/overcommit_memory", -1);

  // Read THP settings
  readLineToArray("/sys/kernel/mm/transparent_hugepage/enabled", stats.thpEnabled);
  readLineToArray("/sys/kernel/mm/transparent_hugepage/defrag", stats.thpDefrag);

  return stats;
}

} // namespace memory

} // namespace seeker