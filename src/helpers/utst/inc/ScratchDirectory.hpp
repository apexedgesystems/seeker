#ifndef SEEKER_HELPERS_SCRATCH_DIRECTORY_HPP
#define SEEKER_HELPERS_SCRATCH_DIRECTORY_HPP
/**
 * @file ScratchDirectory.hpp
 * @brief Owns one temporary directory: claimed exclusively when constructed,
 *        removed on request or at destruction unless retained.
 *
 * Filesystem-tests paradigm, reference revision 5. Standard library only, so
 * any test runner or fixture can hold one. Copied verbatim between repos;
 * only the namespace and the include-guard prefix change.
 */

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace seeker::test_support {

/**
 * @brief One owned temporary directory under a parent the caller chooses.
 *
 * The directory is `<parent>/<label>.<pid>-<serial>`: the label, sanitized
 * to [A-Za-z0-9._-] and cut to LABEL_MAX characters, keeps it readable; the
 * process id and a per-process serial make it unique; and the owner claims
 * it only when create_directory() makes it. A candidate that already exists
 * is skipped, never removed, so a tree retained by an earlier run or left by
 * another process survives. The parent is made absolute against the current
 * directory at construction and created when missing, so path(), remove()
 * and the destructor keep naming the claimed tree after the current
 * directory changes. When no directory could be claimed, valid() is false,
 * path() is empty and error() says why.
 *
 * remove() deletes the tree and reports anything left through its error
 * code; the destructor removes a still-owned tree best-effort and never
 * throws; retain() disarms the destructor so the tree stays at path(). Not
 * copyable. Cleanup after a process kill or power loss is not promised.
 */
class ScratchDirectory {
public:
  static constexpr std::size_t LABEL_MAX = 80; ///< Longest label kept in the name.
  static constexpr int CLAIM_ATTEMPTS = 64;    ///< Candidates tried before giving up.

  ScratchDirectory(const std::filesystem::path& given, std::string_view label) {
    std::error_code ec;
    const std::filesystem::path PARENT = std::filesystem::absolute(given, ec);
    if (ec || PARENT.empty()) {
      error_ = "cannot resolve parent directory " + given.string() + ": " + ec.message();
      return;
    }
    std::filesystem::create_directories(PARENT, ec);
    if (!std::filesystem::is_directory(PARENT, ec)) {
      error_ = "not a usable parent directory: " + PARENT.string();
      return;
    }
    const std::string STEM = sanitize(label) + "." + std::to_string(processId()) + "-";
    for (int attempt = 0; attempt < CLAIM_ATTEMPTS; ++attempt) {
      const std::filesystem::path CANDIDATE = PARENT / (STEM + std::to_string(nextSerial()));
      ec.clear();
      if (std::filesystem::create_directory(CANDIDATE, ec)) {
        path_ = CANDIDATE;
        return;
      }
      if (ec && ec != std::errc::file_exists) {
        error_ = "cannot create " + CANDIDATE.string() + ": " + ec.message();
        return;
      }
      // The candidate exists (a retained tree, another process's, anything):
      // it is left alone and the next serial is tried
    }
    error_ = "no free name after " + std::to_string(CLAIM_ATTEMPTS) + " attempts under " +
             PARENT.string();
  }

  ~ScratchDirectory() {
    if (!path_.empty() && !retained_) {
      std::error_code ec;
      std::filesystem::remove_all(path_, ec);
    }
  }

  ScratchDirectory(const ScratchDirectory&) = delete;
  ScratchDirectory& operator=(const ScratchDirectory&) = delete;

  [[nodiscard]] bool valid() const noexcept { return !path_.empty(); }
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
  [[nodiscard]] const std::string& error() const noexcept { return error_; }

  /// Removes the tree now. Returns false, with `ec` set, when anything of it
  /// remains; the path stays owned so the caller can report it.
  bool remove(std::error_code& ec) {
    ec.clear();
    if (path_.empty()) {
      return true;
    }
    std::filesystem::remove_all(path_, ec);
    if (!ec && std::filesystem::exists(std::filesystem::symlink_status(path_))) {
      ec = std::make_error_code(std::errc::directory_not_empty);
    }
    if (ec) {
      return false;
    }
    path_.clear();
    return true;
  }

  /// Keeps the tree: the destructor leaves it in place at path(). An explicit
  /// remove() afterwards still removes it.
  void retain() noexcept { retained_ = true; }

private:
  std::filesystem::path path_{};
  std::string error_{};
  bool retained_{false};

  /// The label with every character outside [A-Za-z0-9._-] replaced by '_',
  /// cut to LABEL_MAX; "scratch" when empty.
  static std::string sanitize(std::string_view label) {
    std::string out;
    for (const char c : label.substr(0, LABEL_MAX)) {
      const bool KEEP = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
      out.push_back(KEEP ? c : '_');
    }
    return out.empty() ? std::string("scratch") : out;
  }

  /// Per-process serial; uniqueness comes from create_directory(), this only
  /// keeps the candidates moving.
  static unsigned nextSerial() noexcept {
    static std::atomic<unsigned> serial{0};
    return ++serial;
  }

  static int processId() noexcept {
#ifdef _WIN32
    return ::_getpid();
#else
    return static_cast<int>(::getpid());
#endif
  }
};

} // namespace seeker::test_support

#endif // SEEKER_HELPERS_SCRATCH_DIRECTORY_HPP
