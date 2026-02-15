/**
 * @file HugepageStatus.cpp
 * @brief Hugepage allocation status collection from sysfs.
 * @note Reads /sys/kernel/mm/hugepages/ and /sys/devices/system/node/.
 */

#include "src/memory/inc/HugepageStatus.hpp"
#include "src/helpers/inc/Files.hpp"
#include "src/helpers/inc/Format.hpp"

#include <dirent.h> // opendir, readdir, closedir
#include <fcntl.h>  // open, O_RDONLY
#include <unistd.h> // read, close

#include <algorithm> // std::sort
#include <array>     // std::array
#include <cstdlib>   // strtoull, atoi
#include <cstring>   // strlen, strncmp

#include <fmt/core.h>

namespace seeker {

namespace memory {

using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::format::bytesBinary;

namespace {

/* ----------------------------- File Helpers ----------------------------- */

/// Read uint64 from file.
std::uint64_t readFileUint64(const char* path) noexcept {
  std::array<char, 64> buf{};
  if (readFileToBuffer(path, buf.data(), buf.size()) == 0) {
    return 0;
  }
  return std::strtoull(buf.data(), nullptr, 10);
}

/* ----------------------------- Path Building ----------------------------- */

/// Build path: /sys/kernel/mm/hugepages/<dir>/<file>
void buildGlobalPath(char* out, std::size_t outSize, const char* dir, const char* file) noexcept {
  std::snprintf(out, outSize, "/sys/kernel/mm/hugepages/%s/%s", dir, file);
}

/// Build path: /sys/devices/system/node/node<N>/hugepages/<dir>/<file>
void buildNodePath(char* out, std::size_t outSize, int nodeId, const char* dir,
                   const char* file) noexcept {
  std::snprintf(out, outSize, "/sys/devices/system/node/node%d/hugepages/%s/%s", nodeId, dir, file);
}

/* ----------------------------- Size Parsing ----------------------------- */

/// Parse hugepage size from directory name "hugepages-2048kB" -> 2048 * 1024
std::uint64_t parseHugepageDirSize(const char* dirName) noexcept {
  // Format: "hugepages-<N>kB"
  if (std::strncmp(dirName, "hugepages-", 10) != 0) {
    return 0;
  }

  const char* sizeStr = dirName + 10;
  char* end = nullptr;
  const std::uint64_t KB = std::strtoull(sizeStr, &end, 10);

  // Verify "kB" suffix
  if (end == sizeStr || std::strncmp(end, "kB", 2) != 0) {
    return 0;
  }

  return KB * 1024ULL;
}

/* ----------------------------- Formatting ----------------------------- */

/// Format page size as human-readable (e.g., "2 MiB", "1 GiB").
std::string formatPageSize(std::uint64_t bytes) {
  if (bytes >= 1024ULL * 1024 * 1024) {
    return fmt::format("{} GiB", bytes / (1024ULL * 1024 * 1024));
  }
  if (bytes >= 1024ULL * 1024) {
    return fmt::format("{} MiB", bytes / (1024ULL * 1024));
  }
  if (bytes >= 1024ULL) {
    return fmt::format("{} KiB", bytes / 1024ULL);
  }
  return fmt::format("{} B", bytes);
}

} // namespace

/* ----------------------------- HugepageSizeStatus ----------------------------- */

std::uint64_t HugepageSizeStatus::used() const noexcept {
  // Used = total + surplus - free
  const std::uint64_t ALLOCATED = total + surplus;
  return (ALLOCATED >= free) ? (ALLOCATED - free) : 0;
}

std::uint64_t HugepageSizeStatus::totalBytes() const noexcept {
  return (total + surplus) * pageSize;
}

std::uint64_t HugepageSizeStatus::freeBytes() const noexcept { return free * pageSize; }

std::uint64_t HugepageSizeStatus::usedBytes() const noexcept { return used() * pageSize; }

std::string HugepageSizeStatus::toString() const {
  return fmt::format("{}: total={} free={} used={} resv={} surplus={} ({})",
                     formatPageSize(pageSize), total, free, used(), reserved, surplus,
                     bytesBinary(totalBytes()));
}

/* ----------------------------- HugepageStatus ----------------------------- */

bool HugepageStatus::hasHugepages() const noexcept {
  for (std::size_t i = 0; i < sizeCount; ++i) {
    if (sizes[i].total > 0 || sizes[i].surplus > 0) {
      return true;
    }
  }
  return false;
}

std::uint64_t HugepageStatus::totalBytes() const noexcept {
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < sizeCount; ++i) {
    sum += sizes[i].totalBytes();
  }
  return sum;
}

std::uint64_t HugepageStatus::freeBytes() const noexcept {
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < sizeCount; ++i) {
    sum += sizes[i].freeBytes();
  }
  return sum;
}

std::uint64_t HugepageStatus::usedBytes() const noexcept {
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < sizeCount; ++i) {
    sum += sizes[i].usedBytes();
  }
  return sum;
}

const HugepageSizeStatus* HugepageStatus::findSize(std::uint64_t pageSize) const noexcept {
  for (std::size_t i = 0; i < sizeCount; ++i) {
    if (sizes[i].pageSize == pageSize) {
      return &sizes[i];
    }
  }
  return nullptr;
}

std::string HugepageStatus::toString() const {
  std::string out;
  out.reserve(512);

  if (sizeCount == 0) {
    out = "Hugepages: (none configured)\n";
    return out;
  }

  out += "Hugepages:\n";

  for (std::size_t i = 0; i < sizeCount; ++i) {
    out += fmt::format("  {}\n", sizes[i].toString());
  }

  out += fmt::format("  Total: {} allocated, {} free, {} used\n", bytesBinary(totalBytes()),
                     bytesBinary(freeBytes()), bytesBinary(usedBytes()));

  // Per-NUMA node summary if available
  if (nodeCount > 0) {
    out += "  Per-NUMA:\n";
    for (std::size_t s = 0; s < sizeCount; ++s) {
      if (sizes[s].total == 0 && sizes[s].surplus == 0) {
        continue;
      }
      out += fmt::format("    {}:", formatPageSize(sizes[s].pageSize));
      for (std::size_t n = 0; n < nodeCount; ++n) {
        const auto& NODE = perNode[s][n];
        if (NODE.nodeId >= 0) {
          out += fmt::format(" N{}={}/{}", NODE.nodeId, NODE.free, NODE.total);
        }
      }
      out += "\n";
    }
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

HugepageStatus getHugepageStatus() noexcept {
  HugepageStatus status{};

  // Collect page sizes from /sys/kernel/mm/hugepages/
  std::array<std::uint64_t, HP_MAX_SIZES> foundSizes{};
  std::size_t foundCount = 0;

  DIR* hpDir = ::opendir("/sys/kernel/mm/hugepages");
  if (hpDir == nullptr) {
    return status;
  }

  struct dirent* entry = nullptr;
  while ((entry = ::readdir(hpDir)) != nullptr && foundCount < HP_MAX_SIZES) {
    if (entry->d_name[0] == '.') {
      continue;
    }

    const std::uint64_t SIZE = parseHugepageDirSize(entry->d_name);
    if (SIZE > 0) {
      foundSizes[foundCount++] = SIZE;
    }
  }
  ::closedir(hpDir);

  if (foundCount == 0) {
    return status;
  }

  // Sort sizes for consistent ordering
  std::sort(foundSizes.begin(), foundSizes.begin() + foundCount);

  // Read global stats for each size
  std::array<char, 256> pathBuf{};

  for (std::size_t i = 0; i < foundCount; ++i) {
    const std::uint64_t SIZE = foundSizes[i];
    const std::string DIR_NAME = fmt::format("hugepages-{}kB", SIZE / 1024);

    HugepageSizeStatus& sizeStatus = status.sizes[status.sizeCount];
    sizeStatus.pageSize = SIZE;

    buildGlobalPath(pathBuf.data(), pathBuf.size(), DIR_NAME.c_str(), "nr_hugepages");
    sizeStatus.total = readFileUint64(pathBuf.data());

    buildGlobalPath(pathBuf.data(), pathBuf.size(), DIR_NAME.c_str(), "free_hugepages");
    sizeStatus.free = readFileUint64(pathBuf.data());

    buildGlobalPath(pathBuf.data(), pathBuf.size(), DIR_NAME.c_str(), "resv_hugepages");
    sizeStatus.reserved = readFileUint64(pathBuf.data());

    buildGlobalPath(pathBuf.data(), pathBuf.size(), DIR_NAME.c_str(), "surplus_hugepages");
    sizeStatus.surplus = readFileUint64(pathBuf.data());

    ++status.sizeCount;
  }

  // Collect NUMA node IDs from /sys/devices/system/node/
  std::array<int, HP_MAX_NUMA_NODES> nodeIds{};
  std::size_t nodeIdCount = 0;

  DIR* nodeDir = ::opendir("/sys/devices/system/node");
  if (nodeDir != nullptr) {
    while ((entry = ::readdir(nodeDir)) != nullptr && nodeIdCount < HP_MAX_NUMA_NODES) {
      if (std::strncmp(entry->d_name, "node", 4) != 0) {
        continue;
      }

      const int NODE_ID = std::atoi(entry->d_name + 4);
      if (NODE_ID >= 0) {
        nodeIds[nodeIdCount++] = NODE_ID;
      }
    }
    ::closedir(nodeDir);
  }

  if (nodeIdCount == 0) {
    return status;
  }

  // Sort node IDs
  std::sort(nodeIds.begin(), nodeIds.begin() + nodeIdCount);
  status.nodeCount = nodeIdCount;

  // Read per-node stats for each size
  for (std::size_t s = 0; s < status.sizeCount; ++s) {
    const std::uint64_t SIZE = status.sizes[s].pageSize;
    const std::string DIR_NAME = fmt::format("hugepages-{}kB", SIZE / 1024);

    for (std::size_t n = 0; n < nodeIdCount; ++n) {
      const int NODE_ID = nodeIds[n];
      HugepageNodeStatus& nodeStatus = status.perNode[s][n];
      nodeStatus.nodeId = NODE_ID;

      buildNodePath(pathBuf.data(), pathBuf.size(), NODE_ID, DIR_NAME.c_str(), "nr_hugepages");
      nodeStatus.total = readFileUint64(pathBuf.data());

      buildNodePath(pathBuf.data(), pathBuf.size(), NODE_ID, DIR_NAME.c_str(), "free_hugepages");
      nodeStatus.free = readFileUint64(pathBuf.data());

      buildNodePath(pathBuf.data(), pathBuf.size(), NODE_ID, DIR_NAME.c_str(), "surplus_hugepages");
      nodeStatus.surplus = readFileUint64(pathBuf.data());
    }
  }

  return status;
}

} // namespace memory

} // namespace seeker