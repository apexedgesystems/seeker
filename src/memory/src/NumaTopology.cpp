/**
 * @file NumaTopology.cpp
 * @brief Implementation of NUMA topology queries from sysfs.
 */

#include "src/memory/inc/NumaTopology.hpp"
#include "src/helpers/inc/Files.hpp"
#include "src/helpers/inc/Format.hpp"

#include <fcntl.h>  // open, O_RDONLY
#include <unistd.h> // read, close

#include <algorithm>  // std::min, std::sort
#include <cstdio>     // snprintf
#include <cstdlib>    // strtol, strtoull
#include <cstring>    // strncmp, strlen, memset
#include <filesystem> // directory iteration

#include <fmt/core.h>

namespace fs = std::filesystem;

namespace seeker {

namespace memory {

using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::format::bytesBinary;

namespace {

/* ----------------------------- File Helpers ----------------------------- */

/// Read file using fs::path.
inline std::size_t readFsPath(const fs::path& path, char* buf, std::size_t bufSize) noexcept {
  return readFileToBuffer(path.c_str(), buf, bufSize);
}

/* ----------------------------- Parsing Helpers ----------------------------- */

/// Parse node ID from directory name like "node0", "node12".
inline int parseNodeId(const std::string& name) noexcept {
  if (name.size() < 5 || name.compare(0, 4, "node") != 0) {
    return -1;
  }
  // Check remaining chars are digits
  for (std::size_t i = 4; i < name.size(); ++i) {
    if (name[i] < '0' || name[i] > '9') {
      return -1;
    }
  }
  return std::atoi(name.c_str() + 4);
}

/// Parse CPU list string (e.g., "0-3,8-11") into cpuIds array.
/// Returns count of CPUs parsed.
inline std::size_t parseCpuList(const char* cpuList, int* cpuIds, std::size_t maxCpus) noexcept {
  if (cpuList == nullptr || cpuList[0] == '\0') {
    return 0;
  }

  std::size_t count = 0;
  const char* ptr = cpuList;

  while (*ptr != '\0' && count < maxCpus) {
    // Skip whitespace and commas
    while (*ptr == ' ' || *ptr == ',' || *ptr == '\t' || *ptr == '\n') {
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

    // Add CPUs in range
    for (long cpu = START; cpu <= end && count < maxCpus; ++cpu) {
      cpuIds[count] = static_cast<int>(cpu);
      ++count;
    }
  }

  return count;
}

/// Parse "MemTotal:    12345 kB" style line, return value in bytes.
inline std::uint64_t parseMemValueKb(const char* line) noexcept {
  const char* colon = std::strchr(line, ':');
  if (colon == nullptr) {
    return 0;
  }

  const char* ptr = colon + 1;
  while (*ptr == ' ' || *ptr == '\t') {
    ++ptr;
  }

  char* end = nullptr;
  const unsigned long long KB = std::strtoull(ptr, &end, 10);
  if (end == ptr) {
    return 0;
  }

  return static_cast<std::uint64_t>(KB) * 1024ULL;
}

/// Check if line contains a field name (handles "Node N FieldName:" format).
inline const char* findField(const char* line, const char* fieldName) noexcept {
  const char* found = std::strstr(line, fieldName);
  return found;
}

/// Parse node meminfo file for MemTotal and MemFree.
/// Format: "Node 0 MemTotal:       12345 kB"
inline void parseNodeMeminfo(const fs::path& path, std::uint64_t& total,
                             std::uint64_t& free) noexcept {
  total = 0;
  free = 0;

  std::array<char, 2048> buf{};
  if (readFsPath(path, buf.data(), buf.size()) == 0) {
    return;
  }

  const char* ptr = buf.data();
  while (*ptr != '\0') {
    const char* eol = ptr;
    while (*eol != '\0' && *eol != '\n') {
      ++eol;
    }

    // Look for "MemTotal:" anywhere in the line (handles "Node N MemTotal:" format)
    if (findField(ptr, "MemTotal:") != nullptr) {
      total = parseMemValueKb(ptr);
    } else if (findField(ptr, "MemFree:") != nullptr) {
      free = parseMemValueKb(ptr);
    }

    ptr = (*eol == '\0') ? eol : eol + 1;
  }
}

/// Parse distance file "10 20 20" into distance array for this node.
inline void parseDistances(const fs::path& path, std::uint8_t* distances,
                           std::size_t maxNodes) noexcept {
  std::array<char, 512> buf{};
  if (readFsPath(path, buf.data(), buf.size()) == 0) {
    return;
  }

  const char* ptr = buf.data();
  std::size_t idx = 0;

  while (*ptr != '\0' && idx < maxNodes) {
    // Skip whitespace
    while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n') {
      ++ptr;
    }
    if (*ptr == '\0') {
      break;
    }

    char* end = nullptr;
    const long VAL = std::strtol(ptr, &end, 10);
    if (end == ptr) {
      break;
    }

    if (VAL >= 0 && VAL <= 255) {
      distances[idx] = static_cast<std::uint8_t>(VAL);
    } else {
      distances[idx] = NUMA_DISTANCE_INVALID;
    }

    ++idx;
    ptr = end;
  }
}

} // namespace

/* ----------------------------- NumaNodeInfo Methods ----------------------------- */

std::uint64_t NumaNodeInfo::usedBytes() const noexcept {
  return (totalBytes > freeBytes) ? (totalBytes - freeBytes) : 0;
}

bool NumaNodeInfo::hasCpu(int cpuId) const noexcept {
  for (std::size_t i = 0; i < cpuCount; ++i) {
    if (cpuIds[i] == cpuId) {
      return true;
    }
  }
  return false;
}

std::string NumaNodeInfo::toString() const {
  std::string cpuList;
  for (std::size_t i = 0; i < cpuCount; ++i) {
    if (i > 0) {
      cpuList += ',';
    }
    cpuList += std::to_string(cpuIds[i]);
  }

  return fmt::format("Node {}: {} total, {} free, {} used | CPUs: [{}]", nodeId,
                     bytesBinary(totalBytes), bytesBinary(freeBytes), bytesBinary(usedBytes()),
                     cpuList);
}

/* ----------------------------- NumaTopology Methods ----------------------------- */

bool NumaTopology::isNuma() const noexcept { return nodeCount > 1; }

std::uint64_t NumaTopology::totalMemoryBytes() const noexcept {
  std::uint64_t total = 0;
  for (std::size_t i = 0; i < nodeCount; ++i) {
    total += nodes[i].totalBytes;
  }
  return total;
}

std::uint64_t NumaTopology::freeMemoryBytes() const noexcept {
  std::uint64_t free = 0;
  for (std::size_t i = 0; i < nodeCount; ++i) {
    free += nodes[i].freeBytes;
  }
  return free;
}

int NumaTopology::findNodeForCpu(int cpuId) const noexcept {
  for (std::size_t i = 0; i < nodeCount; ++i) {
    if (nodes[i].hasCpu(cpuId)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

std::uint8_t NumaTopology::getDistance(std::size_t from, std::size_t to) const noexcept {
  if (from >= nodeCount || to >= nodeCount) {
    return NUMA_DISTANCE_INVALID;
  }
  return distance[from][to];
}

std::string NumaTopology::toString() const {
  if (nodeCount == 0) {
    return "NUMA: not available or single node system\n";
  }

  std::string out;
  out += fmt::format("NUMA: {} node(s), {} total, {} free\n", nodeCount,
                     bytesBinary(totalMemoryBytes()), bytesBinary(freeMemoryBytes()));

  // Per-node info
  for (std::size_t i = 0; i < nodeCount; ++i) {
    out += "  ";
    out += nodes[i].toString();
    out += "\n";
  }

  // Distance matrix (only if multiple nodes)
  if (nodeCount > 1) {
    out += "Distance matrix:\n";
    out += "     ";
    for (std::size_t j = 0; j < nodeCount; ++j) {
      out += fmt::format(" {:3}", nodes[j].nodeId);
    }
    out += "\n";

    for (std::size_t i = 0; i < nodeCount; ++i) {
      out += fmt::format("  {:2}:", nodes[i].nodeId);
      for (std::size_t j = 0; j < nodeCount; ++j) {
        out += fmt::format(" {:3}", distance[i][j]);
      }
      out += "\n";
    }
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

NumaTopology getNumaTopology() noexcept {
  NumaTopology topo{};

  // Initialize distance matrix to invalid
  std::memset(topo.distance, NUMA_DISTANCE_INVALID, sizeof(topo.distance));

  const fs::path NODE_BASE{"/sys/devices/system/node"};
  std::error_code ec;

  if (!fs::exists(NODE_BASE, ec)) {
    return topo;
  }

  // Collect node IDs first, then sort for consistent ordering
  std::array<int, MAX_NUMA_NODES> nodeIds{};
  std::size_t nodeIdCount = 0;

  for (const auto& ENTRY : fs::directory_iterator(NODE_BASE, ec)) {
    if (!ENTRY.is_directory(ec)) {
      continue;
    }

    const std::string NAME = ENTRY.path().filename().string();
    const int NODE_ID = parseNodeId(NAME);
    if (NODE_ID < 0 || nodeIdCount >= MAX_NUMA_NODES) {
      continue;
    }

    nodeIds[nodeIdCount] = NODE_ID;
    ++nodeIdCount;
  }

  // Sort node IDs
  std::sort(nodeIds.begin(), nodeIds.begin() + static_cast<std::ptrdiff_t>(nodeIdCount));

  // Now populate node info in sorted order
  for (std::size_t i = 0; i < nodeIdCount; ++i) {
    const int NODE_ID = nodeIds[i];
    const fs::path NODE_PATH = NODE_BASE / fmt::format("node{}", NODE_ID);

    NumaNodeInfo& node = topo.nodes[topo.nodeCount];
    node.nodeId = NODE_ID;

    // Parse meminfo
    parseNodeMeminfo(NODE_PATH / "meminfo", node.totalBytes, node.freeBytes);

    // Parse cpulist
    std::array<char, 256> cpuListBuf{};
    if (readFsPath(NODE_PATH / "cpulist", cpuListBuf.data(), cpuListBuf.size()) > 0) {
      node.cpuCount = parseCpuList(cpuListBuf.data(), node.cpuIds, MAX_CPUS_PER_NODE);
    }

    // Parse distances (row in distance matrix for this node)
    parseDistances(NODE_PATH / "distance", topo.distance[topo.nodeCount], MAX_NUMA_NODES);

    ++topo.nodeCount;
  }

  return topo;
}

} // namespace memory

} // namespace seeker