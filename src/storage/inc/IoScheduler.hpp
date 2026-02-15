#ifndef SEEKER_STORAGE_IO_SCHEDULER_HPP
#define SEEKER_STORAGE_IO_SCHEDULER_HPP
/**
 * @file IoScheduler.hpp
 * @brief I/O scheduler configuration and queue parameters.
 * @note Linux-only. Reads /sys/block/\<dev\>/queue/ for scheduler info.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides scheduler configuration for RT validation:
 *  - Current and available schedulers
 *  - Queue depth (nr_requests)
 *  - Read-ahead settings
 *  - RT-friendliness assessment
 *
 * RT Scheduler Guidelines:
 *  - "none" - Best for NVMe, bypasses kernel scheduling entirely
 *  - "mq-deadline" - Good for HDDs, provides latency guarantees
 *  - "bfq" - Fair queuing, higher overhead, not ideal for RT
 *  - "kyber" - Latency-focused, moderate overhead
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace storage {

/* ----------------------------- Constants ----------------------------- */

/// Maximum scheduler name length.
inline constexpr std::size_t SCHEDULER_NAME_SIZE = 32;

/// Maximum number of available schedulers.
inline constexpr std::size_t MAX_SCHEDULERS = 8;

/// Device name size (shared constant).
inline constexpr std::size_t SCHED_DEVICE_NAME_SIZE = 32;

/* ----------------------------- IoSchedulerConfig ----------------------------- */

/**
 * @brief I/O scheduler and queue configuration for a block device.
 *
 * Contains all scheduler-related parameters from /sys/block/\<dev\>/queue/.
 * Used for both information display and RT validation.
 */
struct IoSchedulerConfig {
  std::array<char, SCHED_DEVICE_NAME_SIZE> device{};                 ///< Device name
  std::array<char, SCHEDULER_NAME_SIZE> current{};                   ///< Active scheduler
  std::array<char, SCHEDULER_NAME_SIZE> available[MAX_SCHEDULERS]{}; ///< Available schedulers
  std::size_t availableCount{0}; ///< Valid entries in available[]

  std::int32_t nrRequests{-1};   ///< Queue depth (queue/nr_requests), -1 if unavailable
  std::int32_t readAheadKb{-1};  ///< Read-ahead buffer in KB, -1 if unavailable
  std::int32_t maxSectorsKb{-1}; ///< Maximum request size in KB, -1 if unavailable
  std::int32_t rqAffinity{-1};   ///< Request affinity: 0=none, 1=weak, 2=strong
  std::int32_t noMerges{-1};     ///< Merge policy: 0=merge, 1=nomerge, 2=try-nomerge

  bool iostatsEnabled{false}; ///< I/O statistics collection enabled
  bool addRandom{false};      ///< Contribute to entropy pool

  /// @brief Check if scheduler is "none" (best for NVMe).
  [[nodiscard]] bool isNoneScheduler() const noexcept;

  /// @brief Check if scheduler is "mq-deadline" (good for HDDs).
  [[nodiscard]] bool isMqDeadline() const noexcept;

  /// @brief Check if scheduler is RT-friendly (none or mq-deadline).
  [[nodiscard]] bool isRtFriendly() const noexcept;

  /// @brief Check if read-ahead is disabled or minimal (<= 128 KB).
  [[nodiscard]] bool isReadAheadLow() const noexcept;

  /// @brief Check if specific scheduler is available.
  /// @param name Scheduler name (e.g., "mq-deadline").
  [[nodiscard]] bool hasScheduler(const char* name) const noexcept;

  /// @brief Get RT-friendliness score (0-100).
  /// @note Higher is better for RT. Considers scheduler, read-ahead, merges.
  [[nodiscard]] int rtScore() const noexcept;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;

  /// @brief Get RT assessment string.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string rtAssessment() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Get I/O scheduler configuration for a block device.
 * @param device Device name (e.g., "nvme0n1", "sda").
 * @return Populated IoSchedulerConfig.
 * @note RT-safe: Bounded file reads from /sys/block/\<dev\>/queue/.
 *
 * Sources:
 *  - /sys/block/\<dev\>/queue/scheduler
 *  - /sys/block/\<dev\>/queue/nr_requests
 *  - /sys/block/\<dev\>/queue/read_ahead_kb
 *  - /sys/block/\<dev\>/queue/max_sectors_kb
 *  - /sys/block/\<dev\>/queue/rq_affinity
 *  - /sys/block/\<dev\>/queue/nomerges
 *  - /sys/block/\<dev\>/queue/iostats
 *  - /sys/block/\<dev\>/queue/add_random
 */
[[nodiscard]] IoSchedulerConfig getIoSchedulerConfig(const char* device) noexcept;

} // namespace storage

} // namespace seeker

#endif // SEEKER_STORAGE_IO_SCHEDULER_HPP