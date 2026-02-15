/**
 * @file CpuTopology.cpp
 * @brief CPU topology collection from sysfs.
 * @note Reads /sys/devices/system/cpu/cpuN/ for cores, threads, and caches.
 */

#include "src/cpu/inc/CpuTopology.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <cctype>     // std::isdigit
#include <cstdlib>    // std::atoi, std::strtoull
#include <filesystem> // std::filesystem
#include <fstream>    // std::ifstream
#include <map>        // std::map
#include <utility>    // std::pair

#include <fmt/core.h>

namespace fs = std::filesystem;

namespace seeker {

namespace cpu {

namespace {
using seeker::helpers::strings::copyToFixedArray;

/// Read first line of a text file; empty on failure.
inline std::string readLine(const fs::path& path) noexcept {
  std::ifstream file(path);
  if (!file) {
    return {};
  }
  std::string line;
  std::getline(file, line);
  return line;
}

/// Read integer from file; returns defaultVal on failure.
inline int readInt(const fs::path& path, int defaultVal = -1) noexcept {
  std::ifstream file(path);
  if (!file) {
    return defaultVal;
  }
  long value = defaultVal;
  file >> value;
  return file ? static_cast<int>(value) : defaultVal;
}

/// Check if path exists (suppresses exceptions).
inline bool pathExists(const fs::path& path) noexcept {
  std::error_code ec;
  return fs::exists(path, ec);
}

/// Parse cache index directory into CacheInfo.
inline CacheInfo readCacheIndex(const fs::path& dir) noexcept {
  CacheInfo info{};

  info.level = readInt(dir / "level", 0);

  const std::string TYPE = readLine(dir / "type");
  copyToFixedArray(info.type, TYPE);

  // Size: format like "32K", "1M", "32768K"
  const std::string SIZE_STR = readLine(dir / "size");
  if (!SIZE_STR.empty()) {
    std::uint64_t multiplier = 1ULL;
    std::string digits;
    for (char c : SIZE_STR) {
      if (c >= '0' && c <= '9') {
        digits.push_back(c);
      } else if (c == 'K' || c == 'k') {
        multiplier = 1024ULL;
        break;
      } else if (c == 'M' || c == 'm') {
        multiplier = 1024ULL * 1024ULL;
        break;
      } else if (c == 'G' || c == 'g') {
        multiplier = 1024ULL * 1024ULL * 1024ULL;
        break;
      }
    }
    if (!digits.empty()) {
      info.sizeBytes = std::strtoull(digits.c_str(), nullptr, 10) * multiplier;
    }
  }

  const std::string LINE_STR = readLine(dir / "coherency_line_size");
  if (!LINE_STR.empty()) {
    info.lineBytes = std::strtoull(LINE_STR.c_str(), nullptr, 10);
  }

  info.associativity = readInt(dir / "ways_of_associativity", 0);

  // Policy may not be standardized across kernels
  const std::string POLICY = readLine(dir / "write_policy");
  copyToFixedArray(info.policy, POLICY);

  return info;
}

/// Parse CPU id from directory name like "cpu0", "cpu123".
inline int parseCpuId(const std::string& name) noexcept {
  if (name.rfind("cpu", 0) != 0) {
    return -1;
  }
  std::size_t i = 3;
  while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i])) != 0) {
    ++i;
  }
  if (i == 3) {
    return -1;
  }
  return std::atoi(name.c_str() + 3);
}

} // namespace

/* ----------------------------- CacheInfo ----------------------------- */

std::string CacheInfo::toString() const {
  return fmt::format("L{} {}: {} bytes, {} line, {}-way", level,
                     type[0] != '\0' ? type.data() : "?", sizeBytes, lineBytes, associativity);
}

/* ----------------------------- CoreInfo ----------------------------- */

std::string CoreInfo::toString() const {
  std::string threads;
  for (std::size_t i = 0; i < threadCpuIds.size(); ++i) {
    if (i > 0) {
      threads += ',';
    }
    threads += std::to_string(threadCpuIds[i]);
  }

  return fmt::format("core{} pkg{} numa{}: threads=[{}] caches={}", coreId, packageId, numaNode,
                     threads, caches.size());
}

/* ----------------------------- CpuTopology ----------------------------- */

int CpuTopology::threadsPerCore() const noexcept {
  if (physicalCores <= 0) {
    return 0;
  }
  return logicalCpus / physicalCores;
}

std::string CpuTopology::toString() const {
  return fmt::format("Packages: {}  Cores: {}  Threads: {}  NUMA: {}  SMT: {}", packages,
                     physicalCores, logicalCpus, numaNodes, threadsPerCore());
}

/* ----------------------------- API ----------------------------- */

CpuTopology getCpuTopology() noexcept {
  CpuTopology topo{};

  const fs::path CPU_SYS{"/sys/devices/system/cpu"};
  if (!pathExists(CPU_SYS)) {
    return topo;
  }

  // Map: (packageId, coreId) -> CoreInfo
  std::map<std::pair<int, int>, CoreInfo> coreMap;

  // Track unique packages and NUMA nodes
  std::map<int, bool> seenPackages;
  std::map<int, bool> seenNuma;

  std::error_code ec;
  for (const auto& ENTRY : fs::directory_iterator(CPU_SYS, ec)) {
    if (!ENTRY.is_directory()) {
      continue;
    }

    const std::string NAME = ENTRY.path().filename().string();

    // Skip non-cpu directories
    if (NAME == "cpufreq" || NAME == "cpuidle") {
      continue;
    }

    const int CPU_ID = parseCpuId(NAME);
    if (CPU_ID < 0) {
      continue;
    }

    const fs::path TOPO_DIR = ENTRY.path() / "topology";
    const int CORE_ID = readInt(TOPO_DIR / "core_id", -1);
    const int PKG_ID = readInt(TOPO_DIR / "physical_package_id", -1);

    // NUMA node detection
    int numaNode = -1;
    for (const auto& SUB : fs::directory_iterator(ENTRY.path(), ec)) {
      const std::string SUB_NAME = SUB.path().filename().string();
      if (SUB_NAME.rfind("node", 0) == 0) {
        numaNode = std::atoi(SUB_NAME.c_str() + 4);
        break;
      }
    }

    // Build/update core entry
    const auto KEY = std::make_pair(PKG_ID, CORE_ID);
    CoreInfo& core = coreMap[KEY];

    if (core.coreId == -1) {
      core.coreId = CORE_ID;
      core.packageId = PKG_ID;
      core.numaNode = numaNode;
    }

    core.threadCpuIds.push_back(CPU_ID);

    if (PKG_ID >= 0) {
      seenPackages[PKG_ID] = true;
    }
    if (numaNode >= 0) {
      seenNuma[numaNode] = true;
    }

    // Collect per-core caches (L1/L2)
    const fs::path CACHE_DIR = ENTRY.path() / "cache";
    if (pathExists(CACHE_DIR)) {
      for (const auto& CIDX : fs::directory_iterator(CACHE_DIR, ec)) {
        if (!CIDX.is_directory()) {
          continue;
        }
        const CacheInfo CACHE = readCacheIndex(CIDX.path());
        if (CACHE.level > 0 && CACHE.level <= 2) {
          // Check for duplicates (same cache seen from sibling thread)
          bool found = false;
          for (const CacheInfo& existing : core.caches) {
            if (existing.level == CACHE.level && existing.sizeBytes == CACHE.sizeBytes) {
              found = true;
              break;
            }
          }
          if (!found) {
            core.caches.push_back(CACHE);
          }
        }
      }
    }
  }

  // Collect shared caches (L3+) - deduplicate by (level, size)
  std::map<std::pair<int, std::uint64_t>, CacheInfo> sharedMap;
  for (const auto& ENTRY : fs::directory_iterator(CPU_SYS, ec)) {
    if (!ENTRY.is_directory()) {
      continue;
    }

    const int CPU_ID = parseCpuId(ENTRY.path().filename().string());
    if (CPU_ID < 0) {
      continue;
    }

    const fs::path CACHE_DIR = ENTRY.path() / "cache";
    if (!pathExists(CACHE_DIR)) {
      continue;
    }

    for (const auto& CIDX : fs::directory_iterator(CACHE_DIR, ec)) {
      if (!CIDX.is_directory()) {
        continue;
      }
      const CacheInfo CACHE = readCacheIndex(CIDX.path());
      if (CACHE.level >= 3) {
        const auto KEY = std::make_pair(CACHE.level, CACHE.sizeBytes);
        sharedMap.emplace(KEY, CACHE);
      }
    }
  }

  // Populate topology from maps
  topo.logicalCpus = 0;
  topo.physicalCores = static_cast<int>(coreMap.size());
  topo.packages = static_cast<int>(seenPackages.size());
  topo.numaNodes = static_cast<int>(seenNuma.size());

  topo.cores.reserve(coreMap.size());
  for (auto& kv : coreMap) {
    topo.logicalCpus += static_cast<int>(kv.second.threadCpuIds.size());
    topo.cores.push_back(std::move(kv.second));
  }

  for (const auto& kv : sharedMap) {
    topo.sharedCaches.push_back(kv.second);
  }

  return topo;
}

} // namespace cpu

} // namespace seeker