#ifndef SEEKER_HELPERS_FILES_HPP
#define SEEKER_HELPERS_FILES_HPP
/**
 * @file Files.hpp
 * @brief File I/O and path utilities for embedded/RT systems.
 *
 * Provides safe file operations using C-style I/O (open/read/close) to avoid
 * heap allocations. All functions work with fixed-size buffers.
 *
 * Every absolute path a helper opens resolves under the process-wide
 * filesystem root (setFilesystemRoot()). The root is empty by default, and
 * an empty root leaves paths untouched, so the helpers read the real /proc
 * and /sys; tests point it at a directory tree built from a capture.
 *
 * @note RT-SAFE: Uses C-style I/O, no heap allocation in core functions.
 *       Path checking uses stat() syscalls. Each helper resolves its path
 *       into a RESOLVED_PATH_SIZE stack buffer.
 */

#include "src/helpers/inc/Strings.hpp"

#include <fcntl.h>    // open, O_RDONLY, O_CLOEXEC
#include <string.h>   // strnlen
#include <sys/stat.h> // stat, S_ISDIR, S_ISCHR
#include <unistd.h>   // read, close

#include <array>
#include <climits> // PATH_MAX
#include <cstddef>
#include <cstdint>
#include <cstdlib> // strtol, strtoll, strtoull
#include <cstring> // memcpy, memmove

namespace seeker {
namespace helpers {
namespace files {

/* ----------------------------- Constants ----------------------------- */

/// Default buffer size for file reads.
inline constexpr std::size_t FILE_READ_BUFFER_SIZE = 256;

/// Size for small integer file reads.
inline constexpr std::size_t INT_READ_BUFFER_SIZE = 64;

/// Size of a resolved path buffer: PATH_MAX, the longest path the kernel
/// accepts, terminator included.
inline constexpr std::size_t RESOLVED_PATH_SIZE = PATH_MAX;

/* ----------------------------- Filesystem Root ----------------------------- */

namespace detail {

/// Process-wide filesystem root; length 0 means the real root.
struct FilesystemRoot {
  std::array<char, RESOLVED_PATH_SIZE> path{};
  std::size_t length{0};
};

/// The root's storage: a function-local static of an inline function,
/// one object per process only under ordinary shared linkage with
/// default symbol visibility (linkage note at setFilesystemRoot()).
inline FilesystemRoot& filesystemRootStorage() noexcept {
  static FilesystemRoot root{};
  return root;
}

} // namespace detail

/**
 * @brief Set the directory that stands in for "/" when the helpers open an
 *        absolute path.
 * @param root Absolute directory path; nullptr, "" or "/" select the real root.
 * @return false if root is relative or too long; the root is then unchanged.
 * @note NOT RT-safe and not thread-safe: meant for tests and captures. Set it
 *       before reads start, never from an RT thread and never while another
 *       thread reads through the helpers.
 * @note Linkage: the root is one object per process only under ordinary
 *       shared linkage with default symbol visibility, where the
 *       executable and every library that includes this header export
 *       it weak and the loader binds them to one. An executable
 *       compiled with -fvisibility=hidden, or one in a separate loader
 *       namespace, gets its own copy: it sets a root its libraries
 *       never see, and this still returns true. Such embeddings are not
 *       qualified; a consumer that needs one must export the storage
 *       explicitly and test the binding.
 *
 * Trailing slashes are dropped, so "/" and "" are the same root.
 */
[[nodiscard]] inline bool setFilesystemRoot(const char* root) noexcept {
  detail::FilesystemRoot& current = detail::filesystemRootStorage();
  if (root == nullptr) {
    current.path[0] = '\0';
    current.length = 0;
    return true;
  }
  if (root[0] != '\0' && root[0] != '/') {
    return false;
  }

  std::size_t len = ::strnlen(root, RESOLVED_PATH_SIZE);
  if (len == RESOLVED_PATH_SIZE) {
    return false;
  }
  while (len > 0 && root[len - 1] == '/') {
    --len;
  }

  // memmove: root may be filesystemRoot() itself
  std::memmove(current.path.data(), root, len);
  current.path[len] = '\0';
  current.length = len;
  return true;
}

/**
 * @brief Current filesystem root.
 * @return The root without a trailing slash; "" for the real root.
 * @note RT-safe: Returns a pointer to process-wide storage.
 */
[[nodiscard]] inline const char* filesystemRoot() noexcept {
  return detail::filesystemRootStorage().path.data();
}

/**
 * @brief Resolve a path against the filesystem root.
 * @param path Path to resolve.
 * @param out Buffer for the resolved path.
 * @param outSize Size of out (RESOLVED_PATH_SIZE holds any path the kernel accepts).
 * @return path itself if the root is empty or path is relative; otherwise
 *         out, holding the root followed by path; nullptr if path is nullptr
 *         or the resolved path does not fit in out.
 * @note RT-safe: No allocation; bounded copy into the caller's buffer.
 */
[[nodiscard]] inline const char* resolvePath(const char* path, char* out,
                                             std::size_t outSize) noexcept {
  if (path == nullptr) {
    return nullptr;
  }
  const detail::FilesystemRoot& root = detail::filesystemRootStorage();
  if (root.length == 0 || path[0] != '/') {
    return path;
  }
  if (out == nullptr || outSize <= root.length) {
    return nullptr;
  }

  const std::size_t PATH_LEN = ::strnlen(path, outSize - root.length);
  if (root.length + PATH_LEN >= outSize) {
    return nullptr;
  }
  std::memcpy(out, root.path.data(), root.length);
  std::memcpy(out + root.length, path, PATH_LEN);
  out[root.length + PATH_LEN] = '\0';
  return out;
}

/* ----------------------------- File Reading ----------------------------- */

/**
 * @brief Read file contents into buffer using C-style I/O.
 * @param path File path to read (resolved against the filesystem root).
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

  char resolvedBuf[RESOLVED_PATH_SIZE];
  const char* resolved = resolvePath(path, resolvedBuf, sizeof(resolvedBuf));
  if (resolved == nullptr) {
    return 0;
  }

  const int FD = ::open(resolved, O_RDONLY | O_CLOEXEC);
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
 * @param path Path to check (resolved against the filesystem root).
 * @return true if path exists.
 * @note RT-CAUTION: Syscall latency depends on filesystem/cache state.
 */
[[nodiscard]] inline bool pathExists(const char* path) noexcept {
  char resolvedBuf[RESOLVED_PATH_SIZE];
  const char* resolved = resolvePath(path, resolvedBuf, sizeof(resolvedBuf));
  if (resolved == nullptr) {
    return false;
  }
  struct stat st{};
  return ::stat(resolved, &st) == 0;
}

/**
 * @brief Check if path is a directory.
 * @param path Path to check (resolved against the filesystem root).
 * @return true if path exists and is a directory.
 * @note RT-CAUTION: Syscall latency depends on filesystem/cache state.
 */
[[nodiscard]] inline bool isDirectory(const char* path) noexcept {
  char resolvedBuf[RESOLVED_PATH_SIZE];
  const char* resolved = resolvePath(path, resolvedBuf, sizeof(resolvedBuf));
  if (resolved == nullptr) {
    return false;
  }
  struct stat st{};
  if (::stat(resolved, &st) != 0) {
    return false;
  }
  return S_ISDIR(st.st_mode);
}

/**
 * @brief Check if path is a character device.
 * @param path Path to check (resolved against the filesystem root).
 * @return true if path exists and is a character device.
 * @note RT-CAUTION: Syscall latency depends on filesystem/cache state.
 */
[[nodiscard]] inline bool isCharDevice(const char* path) noexcept {
  char resolvedBuf[RESOLVED_PATH_SIZE];
  const char* resolved = resolvePath(path, resolvedBuf, sizeof(resolvedBuf));
  if (resolved == nullptr) {
    return false;
  }
  struct stat st{};
  if (::stat(resolved, &st) != 0) {
    return false;
  }
  return S_ISCHR(st.st_mode);
}

} // namespace files
} // namespace helpers
} // namespace seeker

#endif // SEEKER_HELPERS_FILES_HPP
