#ifndef SEEKER_HELPERS_STRINGS_HPP
#define SEEKER_HELPERS_STRINGS_HPP
/**
 * @file Strings.hpp
 * @brief String manipulation helpers for embedded/RT systems.
 *
 * Provides safe string operations with bounds checking and no heap allocation.
 * All functions are designed for fixed-size buffers common in embedded systems.
 *
 * @note RT-SAFE: All functions are noexcept with no allocations.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring> // strlen, strncmp, strcmp, memcpy
#include <string>

namespace seeker {
namespace helpers {
namespace strings {

/* ----------------------------- Parsing ----------------------------- */

/**
 * @brief Skip leading whitespace (spaces and tabs).
 * @param ptr Pointer into string.
 * @return Pointer to first non-whitespace character (or end of string).
 * @note RT-SAFE: No allocation.
 */
[[nodiscard]] inline const char* skipWhitespace(const char* ptr) noexcept {
  if (ptr == nullptr) {
    return nullptr;
  }
  while (*ptr == ' ' || *ptr == '\t') {
    ++ptr;
  }
  return ptr;
}

/* ----------------------------- Manipulation ----------------------------- */

/**
 * @brief Strip trailing whitespace in-place.
 * @param buf Buffer to modify (null-terminated).
 * @param len Current string length (will be updated).
 * @note RT-SAFE: No allocation.
 */
inline void stripTrailingWhitespace(char* buf, std::size_t& len) noexcept {
  if (buf == nullptr) {
    return;
  }

  while (len > 0) {
    const char C = buf[len - 1];
    if (C == '\n' || C == '\r' || C == ' ' || C == '\t') {
      --len;
      buf[len] = '\0';
    } else {
      break;
    }
  }
}

/**
 * @brief Copy string into fixed-size array with null termination.
 * @tparam N Array size.
 * @param dest Destination array.
 * @param src Source string (null-terminated).
 * @note RT-SAFE: No allocation, bounded operation.
 */
template <std::size_t N>
inline void copyToFixedArray(std::array<char, N>& dest, const char* src) noexcept {
  if (src == nullptr) {
    dest[0] = '\0';
    return;
  }

  std::size_t i = 0;
  while (i < N - 1 && src[i] != '\0') {
    dest[i] = src[i];
    ++i;
  }
  dest[i] = '\0';
}

/**
 * @brief Copy exactly len bytes into fixed-size array with null termination.
 * @tparam N Array size.
 * @param dest Destination array.
 * @param src Source buffer (not necessarily null-terminated).
 * @param len Number of bytes to copy from src.
 * @note RT-SAFE: No allocation, bounded operation.
 */
template <std::size_t N>
inline void copyToFixedArray(std::array<char, N>& dest, const char* src, std::size_t len) noexcept {
  if (src == nullptr || len == 0) {
    dest[0] = '\0';
    return;
  }

  const std::size_t COPY_LEN = (len < N - 1) ? len : (N - 1);
  std::memcpy(dest.data(), src, COPY_LEN);
  dest[COPY_LEN] = '\0';
}

/**
 * @brief Copy std::string into fixed-size array with null termination.
 * @tparam N Array size.
 * @param dest Destination array.
 * @param src Source string.
 * @note RT-SAFE: No allocation, bounded operation.
 */
template <std::size_t N>
inline void copyToFixedArray(std::array<char, N>& dest, const std::string& src) noexcept {
  copyToFixedArray(dest, src.c_str(), src.size());
}

/* ----------------------------- Sorting ----------------------------- */

/**
 * @brief Insertion sort for a counted subrange of fixed-size char arrays.
 *
 * Optimal for the small, bounded collections used throughout seeker
 * (device names, controller IDs, etc.).
 *
 * @tparam N Element char array size.
 * @tparam M Container capacity.
 * @param arr Array of fixed-size char arrays.
 * @param count Number of valid elements to sort (must be <= M).
 * @note RT-SAFE: No allocation, O(n^2) but n is bounded by M (typically <= 16).
 */
template <std::size_t N, std::size_t M>
inline void sortFixedStrings(std::array<std::array<char, N>, M>& arr, std::size_t count) noexcept {
  const std::size_t LIMIT = (count < M) ? count : M;
  for (std::size_t i = 1; i < LIMIT; ++i) {
    for (std::size_t j = i; j > 0 && std::strcmp(arr[j - 1].data(), arr[j].data()) > 0; --j) {
      arr[j - 1].swap(arr[j]);
    }
  }
}

/* ----------------------------- Copying ----------------------------- */

/**
 * @brief Copy string into raw buffer with null termination.
 * @param dest Destination buffer.
 * @param destSize Size of destination buffer.
 * @param src Source string (null-terminated).
 * @note RT-SAFE: No allocation, bounded operation.
 */
inline void copyToBuffer(char* dest, std::size_t destSize, const char* src) noexcept {
  if (dest == nullptr || destSize == 0) {
    return;
  }

  if (src == nullptr) {
    dest[0] = '\0';
    return;
  }

  std::size_t i = 0;
  while (i < destSize - 1 && src[i] != '\0') {
    dest[i] = src[i];
    ++i;
  }
  dest[i] = '\0';
}

/**
 * @brief Check if string starts with prefix.
 * @param str String to check.
 * @param prefix Prefix to look for.
 * @return true if str starts with prefix.
 * @note RT-SAFE: No allocation.
 */
[[nodiscard]] inline bool startsWith(const char* str, const char* prefix) noexcept {
  if (str == nullptr || prefix == nullptr) {
    return false;
  }
  const std::size_t PREFIX_LEN = std::strlen(prefix);
  return std::strncmp(str, prefix, PREFIX_LEN) == 0;
}

} // namespace strings
} // namespace helpers
} // namespace seeker

#endif // SEEKER_HELPERS_STRINGS_HPP
