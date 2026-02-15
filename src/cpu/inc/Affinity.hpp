#ifndef SEEKER_CPU_AFFINITY_HPP
#define SEEKER_CPU_AFFINITY_HPP
/**
 * @file Affinity.hpp
 * @brief Thread CPU affinity query and control (Linux).
 * @note Linux-only. Requires pthread and sched_* syscalls.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 */

#include <bitset>  // std::bitset
#include <cstddef> // std::size_t
#include <string>  // std::string

namespace seeker {

namespace cpu {

/* ----------------------------- Constants ----------------------------- */

/// Maximum supported CPU count (covers most systems; matches common CPU_SETSIZE).
inline constexpr std::size_t MAX_CPUS = 1024;

/* ----------------------------- AffinityStatus ----------------------------- */

/**
 * @brief Status codes for affinity operations.
 */
enum class AffinityStatus : unsigned char {
  OK = 0,
  INVALID_ARGUMENT,
  SYSCALL_FAILED,
};

/**
 * @brief Human-readable status string.
 * @note NOT RT-safe: Returns static string pointer (safe) but intended for logging.
 */
[[nodiscard]] const char* toString(AffinityStatus status) noexcept;

/* ----------------------------- CpuSet ----------------------------- */

/**
 * @brief Fixed-size CPU set using bitset (RT-safe, no heap allocation).
 */
struct CpuSet {
  std::bitset<MAX_CPUS> mask{};

  /// @brief Test if CPU is in set.
  [[nodiscard]] bool test(std::size_t cpuId) const noexcept;

  /// @brief Add CPU to set.
  void set(std::size_t cpuId) noexcept;

  /// @brief Remove CPU from set.
  void clear(std::size_t cpuId) noexcept;

  /// @brief Clear all CPUs from set.
  void reset() noexcept;

  /// @brief Count of CPUs in set.
  [[nodiscard]] std::size_t count() const noexcept;

  /// @brief Check if set is empty.
  [[nodiscard]] bool empty() const noexcept;

  /// @brief Human-readable summary (e.g., "{0,2,3}").
  /// @note NOT RT-safe: Allocates for string building.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Get current thread's CPU affinity.
 * @return Populated CpuSet; empty on failure.
 * @note RT-safe: No heap allocation, single syscall.
 */
[[nodiscard]] CpuSet getCurrentThreadAffinity() noexcept;

/**
 * @brief Set current thread's CPU affinity.
 * @param set CPU set to apply (must be non-empty).
 * @return Status code indicating success or failure reason.
 * @note RT-safe: No heap allocation, single syscall.
 */
[[nodiscard]] AffinityStatus setCurrentThreadAffinity(const CpuSet& set) noexcept;

/**
 * @brief Get the number of configured CPUs on the system.
 * @return CPU count (>= 1), or MAX_CPUS as fallback.
 * @note RT-safe: Single sysconf call.
 */
[[nodiscard]] std::size_t getConfiguredCpuCount() noexcept;

} // namespace cpu

} // namespace seeker

#endif // SEEKER_CPU_AFFINITY_HPP