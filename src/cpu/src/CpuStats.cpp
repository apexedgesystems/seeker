/**
 * @file CpuStats.cpp
 * @brief CPU and memory statistics collection.
 * @note Uses sysinfo(2) and /proc filesystem for data collection.
 */

#include "src/cpu/inc/CpuStats.hpp"
#include "src/helpers/inc/Strings.hpp"
#include "src/helpers/inc/Format.hpp"

#include <sys/sysinfo.h> // sysinfo, get_nprocs

#include <algorithm> // std::min
#include <array>     // std::array
#include <cmath>     // std::lround
#include <cstdlib>   // std::strtod, std::strtoull
#include <cstring>   // std::memcpy
#include <fstream>   // std::ifstream

#include <fmt/core.h>

namespace seeker {

namespace cpu {

using seeker::helpers::format::bytesBinary;
using seeker::helpers::strings::copyToFixedArray;

namespace {

/* ----------------------------- File Helpers ----------------------------- */

/// Trim leading spaces/colons and return value portion of "key: value" line.
inline const char* trimAfterColon(const std::string& line, std::size_t& outLen) noexcept {
  const std::size_t POS = line.find(':');
  if (POS == std::string::npos) {
    outLen = 0;
    return nullptr;
  }
  std::size_t start = POS + 1;
  while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
    ++start;
  }
  outLen = line.size() - start;
  return line.c_str() + start;
}

/// Parse "MemAvailable: 123456 kB" -> bytes.
inline std::uint64_t parseKbToBytes(const std::string& line) noexcept {
  std::size_t i = 0;
  while (i < line.size() && (line[i] < '0' || line[i] > '9')) {
    ++i;
  }
  std::size_t j = i;
  while (j < line.size() && (line[j] >= '0' && line[j] <= '9')) {
    ++j;
  }
  if (i == j) {
    return 0;
  }
  const std::uint64_t KB = std::strtoull(line.c_str() + i, nullptr, 10);
  return KB * 1024ULL;
}

/// Scale sysinfo memory values by mem_unit.
inline std::uint64_t scaleMem(unsigned long val, unsigned long memUnit) noexcept {
  const std::uint64_t SCALE = (memUnit == 0UL) ? 1ULL : static_cast<std::uint64_t>(memUnit);
  return static_cast<std::uint64_t>(val) * SCALE;
}

/* ----------------------------- Formatting Helpers ----------------------------- */

} // namespace

/* ----------------------------- Individual Readers ----------------------------- */

SysinfoData readSysinfo() noexcept {
  SysinfoData out{};
  struct sysinfo si{};
  if (::sysinfo(&si) == 0) {
    out.totalRamBytes = scaleMem(si.totalram, si.mem_unit);
    out.freeRamBytes = scaleMem(si.freeram, si.mem_unit);
    out.totalSwapBytes = scaleMem(si.totalswap, si.mem_unit);
    out.freeSwapBytes = scaleMem(si.freeswap, si.mem_unit);
    out.uptimeSeconds = static_cast<std::uint64_t>(si.uptime);
    out.processCount = static_cast<int>(si.procs);

    // Load averages: fixed-point 16.16 format
    static constexpr double SCALE = 1.0 / 65536.0;
    out.load1 = static_cast<double>(si.loads[0]) * SCALE;
    out.load5 = static_cast<double>(si.loads[1]) * SCALE;
    out.load15 = static_cast<double>(si.loads[2]) * SCALE;
  }
  return out;
}

KernelVersionData readKernelVersion() noexcept {
  KernelVersionData out{};
  std::ifstream file("/proc/version");
  if (file) {
    std::string line;
    std::getline(file, line);
    copyToFixedArray(out.version, line.c_str(), line.size());
  }
  return out;
}

CpuInfoData readCpuInfo() noexcept {
  CpuInfoData out{};
  std::ifstream file("/proc/cpuinfo");
  if (!file) {
    return out;
  }

  std::string line;
  bool gotModel = false;
  bool gotMhz = false;

  while (std::getline(file, line)) {
    if (!gotModel && line.size() >= 10 && line.compare(0, 10, "model name") == 0) {
      std::size_t len = 0;
      const char* VAL = trimAfterColon(line, len);
      if (VAL != nullptr && len > 0) {
        copyToFixedArray(out.model, VAL, len);
        gotModel = true;
      }
    } else if (!gotMhz && line.size() >= 7 && line.compare(0, 7, "cpu MHz") == 0) {
      std::size_t len = 0;
      const char* VAL = trimAfterColon(line, len);
      if (VAL != nullptr && len > 0) {
        char* end = nullptr;
        const double MHZ = std::strtod(VAL, &end);
        if (end != VAL) {
          // Round to nearest 10 MHz
          out.frequencyMhz = static_cast<long>(std::lround(MHZ / 10.0) * 10);
          gotMhz = true;
        }
      }
    }
    if (gotModel && gotMhz) {
      break;
    }
  }
  return out;
}

MeminfoData readMeminfo() noexcept {
  MeminfoData out{};
  std::ifstream file("/proc/meminfo");
  if (!file) {
    return out;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.size() >= 13 && line.compare(0, 13, "MemAvailable:") == 0) {
      out.availableBytes = parseKbToBytes(line);
      out.hasAvailable = true;
      break;
    }
  }
  return out;
}

CpuCountData readCpuCount() noexcept {
  CpuCountData out{};
  out.count = ::get_nprocs();
  return out;
}

/* ----------------------------- API ----------------------------- */

CpuStats getCpuStats() noexcept {
  CpuStats stats{};
  stats.cpuCount = readCpuCount();
  stats.kernel = readKernelVersion();
  stats.cpuInfo = readCpuInfo();
  stats.sysinfo = readSysinfo();
  stats.meminfo = readMeminfo();
  return stats;
}

std::string CpuStats::toString() const {
  return fmt::format("CPUs: {}\n"
                     "Kernel: {}\n"
                     "CPU: {} @ {} MHz\n"
                     "Uptime: {} s  |  Processes: {}\n"
                     "Load avg (1/5/15): {:.2f} / {:.2f} / {:.2f}\n"
                     "RAM total/free/avail: {} / {} / {}\n"
                     "Swap total/free: {} / {}",
                     cpuCount.count, kernel.version.data(), cpuInfo.model.data(),
                     cpuInfo.frequencyMhz, sysinfo.uptimeSeconds, sysinfo.processCount,
                     sysinfo.load1, sysinfo.load5, sysinfo.load15,
                     bytesBinary(sysinfo.totalRamBytes), bytesBinary(sysinfo.freeRamBytes),
                     bytesBinary(meminfo.availableBytes), bytesBinary(sysinfo.totalSwapBytes),
                     bytesBinary(sysinfo.freeSwapBytes));
}

} // namespace cpu

} // namespace seeker