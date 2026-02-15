#ifndef SEEKER_HELPERS_FILES_HPP
#define SEEKER_HELPERS_FILES_HPP
/**
 * @file Files.hpp
 * @brief File I/O and path utilities for embedded/RT systems.
 *
 * Provides safe file operations using C-style I/O (open/read/close) to avoid
 * heap allocations. All functions work with fixed-size buffers.
 *
 * @note RT-SAFE: Uses C-style I/O, no heap allocation in core functions.
 *       Path checking uses stat()/access() syscalls.
 */

#include "src/helpers/inc/Strings.hpp"

#include <fcntl.h>    // open, O_RDONLY, O_CLOEXEC
#include <sys/stat.h> // stat, S_ISDIR, S_ISCHR
#include <unistd.h>   // read, close

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib> // strtol, strtoll, strtoull

namespace seeker {
namespace helpers {
namespace files {

/* ----------------------------- Constants ----------------------------- */

/// Default buffer size for file reads.
inline constexpr std::size_t FILE_READ_BUFFER_SIZE = 256;

/// Size for small integer file reads.
inline constexpr std::size_t INT_READ_BUFFER_SIZE = 64;

/* ----------------------------- File Reading ----------------------------- */

/**
 * @brief Read file contents into buffer using C-style I/O.
 * @param path File path to read.
 * @param buf Output buffer.
 * @param bufSize Size of output buffer.
 * @return Number of bytes read (excluding null terminator), 0 on error.
 * @note RT-SAFE: Uses open/read/close, no heap allocation.
 *
 * Strips trailing newlines and carriage returns. Always null-terminates.
 */
[[nodiscard]] inline std::size_t readFileToBuffer(const char* path, char* buf,
                                                  std::size_t bufSize) noexcept {
  if (path == nullptr || buf == nullptr || bufSize == 0) {
    if (buf != nullptr && bufSize > 0) {
      buf[0] = '\0';
    }
    return 0;
  }

  buf[0] = '\0';

  const int FD = ::open(path, O_RDONLY | O_CLOEXEC);
  if (FD < 0) {
    return 0;
  }

  std::size_t total = 0;
  while (total < bufSize - 1) {
    const ssize_t N = ::read(FD, buf + total, bufSize - 1 - total);
    if (N <= 0) {
      break;
    }
    total += static_cast<std::size_t>(N);
  }

  ::close(FD);
  buf[total] = '\0';

  seeker::helpers::strings::stripTrailingWhitespace(buf, total);

  return total;
}

/**
 * @brief Read first line from file into fixed array.
 * @tparam N Array size.
 * @param path File path to read.
 * @param out Output array.
 * @return Number of characters read (excluding null), 0 on error.
 * @note RT-SAFE: Uses open/read/close, no heap allocation.
 */
template <std::size_t N>
[[nodiscard]] inline std::size_t readFileLine(const char* path, std::array<char, N>& out) noexcept {
  out[0] = '\0';

  std::array<char, N> buf{};
  const std::size_t LEN = readFileToBuffer(path, buf.data(), buf.size());
  if (LEN == 0) {
    return 0;
  }

  std::size_t copyLen = 0;
  while (copyLen < LEN && copyLen < N - 1 && buf[copyLen] != '\n' && buf[copyLen] != '\0') {
    out[copyLen] = buf[copyLen];
    ++copyLen;
  }
  out[copyLen] = '\0';

  return copyLen;
}

/**
 * @brief Read signed 32-bit integer from file.
 * @param path File path to read.
 * @param defaultVal Value to return on error.
 * @return Parsed integer or defaultVal on failure.
 * @note RT-SAFE: No heap allocation.
 */
[[nodiscard]] inline std::int32_t readFileInt(const char* path,
                                              std::int32_t defaultVal = -1) noexcept {
  std::array<char, INT_READ_BUFFER_SIZE> buf{};
  if (readFileToBuffer(path, buf.data(), buf.size()) == 0) {
    return defaultVal;
  }

  char* end = nullptr;
  const long VAL = std::strtol(buf.data(), &end, 10);
  if (end == buf.data()) {
    return defaultVal;
  }

  return static_cast<std::int32_t>(VAL);
}

/**
 * @brief Read signed 64-bit integer from file.
 * @param path File path to read.
 * @param defaultVal Value to return on error.
 * @return Parsed integer or defaultVal on failure.
 * @note RT-SAFE: No heap allocation.
 */
[[nodiscard]] inline std::int64_t readFileInt64(const char* path,
                                                std::int64_t defaultVal = -1) noexcept {
  std::array<char, INT_READ_BUFFER_SIZE> buf{};
  if (readFileToBuffer(path, buf.data(), buf.size()) == 0) {
    return defaultVal;
  }

  char* end = nullptr;
  const long long VAL = std::strtoll(buf.data(), &end, 10);
  if (end == buf.data()) {
    return defaultVal;
  }

  return static_cast<std::int64_t>(VAL);
}

/**
 * @brief Read unsigned 64-bit integer from file.
 * @param path File path to read.
 * @param defaultVal Value to return on error.
 * @return Parsed integer or defaultVal on failure.
 * @note RT-SAFE: No heap allocation.
 */
[[nodiscard]] inline std::uint64_t readFileUint64(const char* path,
                                                  std::uint64_t defaultVal = 0) noexcept {
  std::array<char, INT_READ_BUFFER_SIZE> buf{};
  if (readFileToBuffer(path, buf.data(), buf.size()) == 0) {
    return defaultVal;
  }

  char* end = nullptr;
  const unsigned long long VAL = std::strtoull(buf.data(), &end, 10);
  if (end == buf.data()) {
    return defaultVal;
  }

  return static_cast<std::uint64_t>(VAL);
}

/* ----------------------------- Path Utilities ----------------------------- */

/**
 * @brief Check if path exists (file or directory).
 * @param path Path to check.
 * @return true if path exists.
 * @note RT-CAUTION: Syscall latency depends on filesystem/cache state.
 */
[[nodiscard]] inline bool pathExists(const char* path) noexcept {
  if (path == nullptr) {
    return false;
  }
  struct stat st{};
  return ::stat(path, &st) == 0;
}

/**
 * @brief Check if path is a directory.
 * @param path Path to check.
 * @return true if path exists and is a directory.
 * @note RT-CAUTION: Syscall latency depends on filesystem/cache state.
 */
[[nodiscard]] inline bool isDirectory(const char* path) noexcept {
  if (path == nullptr) {
    return false;
  }
  struct stat st{};
  if (::stat(path, &st) != 0) {
    return false;
  }
  return S_ISDIR(st.st_mode);
}

/**
 * @brief Check if path is a character device.
 * @param path Path to check.
 * @return true if path exists and is a character device.
 * @note RT-CAUTION: Syscall latency depends on filesystem/cache state.
 */
[[nodiscard]] inline bool isCharDevice(const char* path) noexcept {
  if (path == nullptr) {
    return false;
  }
  struct stat st{};
  if (::stat(path, &st) != 0) {
    return false;
  }
  return S_ISCHR(st.st_mode);
}

} // namespace files
} // namespace helpers
} // namespace seeker

#endif // SEEKER_HELPERS_FILES_HPP
