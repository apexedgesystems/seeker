#ifndef SEEKER_HELPERS_CPU_HPP
#define SEEKER_HELPERS_CPU_HPP
/**
 * @file Cpu.hpp
 * @brief CPU timing helper for monotonic timestamps.
 *
 * @note RT-CAUTION: Syscall (clock_gettime), but typically vDSO-accelerated.
 */

#include <cstdint>
#include <ctime> // clock_gettime, CLOCK_MONOTONIC

namespace seeker {
namespace helpers {
namespace cpu {

/* ----------------------------- API ----------------------------- */

/**
 * @brief Get monotonic timestamp in nanoseconds.
 *
 * Uses CLOCK_MONOTONIC for consistent, non-decreasing time measurements
 * unaffected by system clock adjustments.
 *
 * @return Current monotonic time in nanoseconds.
 * @note RT-CAUTION: Syscall (clock_gettime), but typically vDSO-accelerated.
 */
[[nodiscard]] inline std::uint64_t getMonotonicNs() noexcept {
  struct timespec ts{};
  ::clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
         static_cast<std::uint64_t>(ts.tv_nsec);
}

} // namespace cpu
} // namespace helpers
} // namespace seeker

#endif // SEEKER_HELPERS_CPU_HPP
