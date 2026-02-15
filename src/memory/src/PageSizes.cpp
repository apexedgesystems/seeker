/**
 * @file PageSizes.cpp
 * @brief Implementation of page size queries.
 */

#include "src/memory/inc/PageSizes.hpp"
#include "src/helpers/inc/Format.hpp"

#include <unistd.h> // getpagesize

#include <algorithm>  // std::sort
#include <cstdlib>    // strtoull
#include <cstring>    // strlen
#include <filesystem> // directory_iterator

#include <fmt/core.h>

namespace fs = std::filesystem;

namespace seeker {

namespace memory {

using seeker::helpers::format::bytesBinary;

namespace {

/* ----------------------------- File Helpers ----------------------------- */

/**
 * Parse directory name like "hugepages-2048kB" or "hugepages-1048576kB" into bytes.
 * Format: hugepages-<number>kB
 */
inline std::uint64_t parseHugepageDirName(const char* name) noexcept {
  // Skip "hugepages-" prefix (10 chars)
  if (std::strncmp(name, "hugepages-", 10) != 0) {
    return 0;
  }

  const char* numStart = name + 10;
  char* numEnd = nullptr;
  const unsigned long long KB = std::strtoull(numStart, &numEnd, 10);

  // Verify "kB" suffix
  if (numEnd == numStart || std::strcmp(numEnd, "kB") != 0) {
    return 0;
  }

  return static_cast<std::uint64_t>(KB) * 1024ULL;
}

/// Check if path exists (suppresses exceptions).
inline bool pathExists(const fs::path& path) noexcept {
  std::error_code ec;
  return fs::exists(path, ec);
}

} // namespace

/* ----------------------------- PageSizes Methods ----------------------------- */

bool PageSizes::hasHugePageSize(std::uint64_t sizeBytes) const noexcept {
  for (std::size_t i = 0; i < hugeSizeCount; ++i) {
    if (hugeSizes[i] == sizeBytes) {
      return true;
    }
  }
  return false;
}

bool PageSizes::hasHugePages() const noexcept { return hugeSizeCount > 0; }

std::uint64_t PageSizes::largestHugePageSize() const noexcept {
  std::uint64_t largest = 0;
  for (std::size_t i = 0; i < hugeSizeCount; ++i) {
    if (hugeSizes[i] > largest) {
      largest = hugeSizes[i];
    }
  }
  return largest;
}

std::string PageSizes::toString() const {
  std::string out;
  out += fmt::format("Base page: {}\n", bytesBinary(basePageBytes));
  out += "Huge pages: ";

  if (hugeSizeCount == 0) {
    out += "(none available)";
  } else {
    for (std::size_t i = 0; i < hugeSizeCount; ++i) {
      if (i > 0) {
        out += ", ";
      }
      out += bytesBinary(hugeSizes[i]);
    }
  }

  return out;
}

/* ----------------------------- API ----------------------------- */

std::string formatBytes(std::uint64_t bytes) { return bytesBinary(bytes); }

PageSizes getPageSizes() noexcept {
  PageSizes ps{};

  // Base page size from syscall
  ps.basePageBytes = static_cast<std::uint64_t>(::getpagesize());

  // Scan /sys/kernel/mm/hugepages/ for available sizes
  const fs::path HP_DIR{"/sys/kernel/mm/hugepages"};
  if (!pathExists(HP_DIR)) {
    return ps;
  }

  std::error_code ec;
  for (const auto& ENTRY : fs::directory_iterator(HP_DIR, ec)) {
    if (!ENTRY.is_directory(ec)) {
      continue;
    }

    const std::string NAME = ENTRY.path().filename().string();
    const std::uint64_t SIZE_BYTES = parseHugepageDirName(NAME.c_str());

    if (SIZE_BYTES > 0 && ps.hugeSizeCount < MAX_HUGEPAGE_SIZES) {
      ps.hugeSizes[ps.hugeSizeCount] = SIZE_BYTES;
      ++ps.hugeSizeCount;
    }
  }

  // Sort sizes ascending for consistent output
  std::sort(ps.hugeSizes, ps.hugeSizes + ps.hugeSizeCount);

  return ps;
}

} // namespace memory

} // namespace seeker