#ifndef SEEKER_HELPERS_FORMAT_HPP
#define SEEKER_HELPERS_FORMAT_HPP
/**
 * @file Format.hpp
 * @brief Human-readable formatting utilities for bytes.
 *
 * Provides consistent formatting across CLI tools and diagnostic output.
 * Uses fmt library for string formatting.
 *
 * @note NOT RT-SAFE: All functions return std::string (heap allocation).
 *       Use only in cold paths (CLI output, logging, etc.).
 */

#include <cstdint>
#include <string>

#include <fmt/core.h>
#include <fmt/format.h>

namespace seeker {
namespace helpers {
namespace format {

/* ----------------------------- API ----------------------------- */

/**
 * @brief Format bytes using binary units (KiB, MiB, GiB, TiB).
 * @param bytes Byte count.
 * @return Formatted string (e.g., "1.5 GiB").
 * @note NOT RT-SAFE: Returns std::string.
 */
[[nodiscard]] inline std::string bytesBinary(std::uint64_t bytes) {
  if (bytes == 0) {
    return "0 B";
  }

  static constexpr std::uint64_t KIB = 1024ULL;
  static constexpr std::uint64_t MIB = KIB * 1024ULL;
  static constexpr std::uint64_t GIB = MIB * 1024ULL;
  static constexpr std::uint64_t TIB = GIB * 1024ULL;

  if (bytes >= TIB) {
    return fmt::format("{:.1f} TiB", static_cast<double>(bytes) / static_cast<double>(TIB));
  }
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
}

} // namespace format
} // namespace helpers
} // namespace seeker

#endif // SEEKER_HELPERS_FORMAT_HPP
