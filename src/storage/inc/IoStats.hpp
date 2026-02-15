#ifndef SEEKER_STORAGE_IO_STATS_HPP
#define SEEKER_STORAGE_IO_STATS_HPP
/**
 * @file IoStats.hpp
 * @brief Block device I/O statistics with snapshot + delta pattern.
 * @note Linux-only. Reads /sys/block/\<dev\>/stat for I/O counters.
 * @note Thread-safe: All functions are stateless and safe to call concurrently.
 *
 * Provides I/O performance monitoring for RT systems:
 *  - Read/write IOPS and throughput
 *  - Average I/O latency
 *  - Device utilization percentage
 *  - Queue depth monitoring
 *
 * Usage pattern:
 *   auto before = getIoStatsSnapshot("nvme0n1");
 *   // ... wait or do work ...
 *   auto after = getIoStatsSnapshot("nvme0n1");
 *   auto delta = computeIoStatsDelta(before, after);
 *   // delta now contains rates and percentages
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace seeker {

namespace storage {

/* ----------------------------- Constants ----------------------------- */

/// Device name size for I/O stats.
inline constexpr std::size_t IOSTAT_DEVICE_NAME_SIZE = 32;

/* ----------------------------- IoCounters ----------------------------- */

/**
 * @brief Raw I/O counters from /sys/block/\<dev\>/stat.
 *
 * These counters are cumulative since boot and need delta calculation
 * to derive rates. Units match kernel documentation:
 *  - Operations: completed I/O requests
 *  - Sectors: 512-byte units
 *  - Time: milliseconds
 */
struct IoCounters {
  std::uint64_t readOps{0};     ///< Completed read operations
  std::uint64_t readMerges{0};  ///< Read requests merged
  std::uint64_t readSectors{0}; ///< Sectors read (512-byte units)
  std::uint64_t readTimeMs{0};  ///< Time spent reading (ms)

  std::uint64_t writeOps{0};     ///< Completed write operations
  std::uint64_t writeMerges{0};  ///< Write requests merged
  std::uint64_t writeSectors{0}; ///< Sectors written
  std::uint64_t writeTimeMs{0};  ///< Time spent writing (ms)

  std::uint64_t ioInFlight{0}; ///< Currently in-flight I/O (snapshot)

  std::uint64_t ioTimeMs{0};         ///< Total time spent doing I/O (ms)
  std::uint64_t weightedIoTimeMs{0}; ///< Weighted I/O time (for queue depth)

  std::uint64_t discardOps{0};     ///< TRIM/discard operations (kernel 4.18+)
  std::uint64_t discardMerges{0};  ///< Discard requests merged
  std::uint64_t discardSectors{0}; ///< Sectors discarded
  std::uint64_t discardTimeMs{0};  ///< Time spent discarding

  std::uint64_t flushOps{0};    ///< Flush operations (kernel 5.5+)
  std::uint64_t flushTimeMs{0}; ///< Time spent flushing

  /// @brief Total read bytes (sectors * 512).
  [[nodiscard]] std::uint64_t readBytes() const noexcept;

  /// @brief Total write bytes (sectors * 512).
  [[nodiscard]] std::uint64_t writeBytes() const noexcept;

  /// @brief Total I/O operations (read + write).
  [[nodiscard]] std::uint64_t totalOps() const noexcept;

  /// @brief Total I/O bytes (read + write).
  [[nodiscard]] std::uint64_t totalBytes() const noexcept;
};

/* ----------------------------- IoStatsSnapshot ----------------------------- */

/**
 * @brief Snapshot of I/O statistics at a point in time.
 */
struct IoStatsSnapshot {
  std::array<char, IOSTAT_DEVICE_NAME_SIZE> device{}; ///< Device name
  IoCounters counters{};                              ///< Raw counters
  std::uint64_t timestampNs{0};                       ///< Monotonic timestamp (ns)

  /// @brief Human-readable raw counters (for debugging).
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- IoStatsDelta ----------------------------- */

/**
 * @brief Computed delta/rates between two snapshots.
 *
 * All rate values are per-second. Percentages are 0-100.
 */
struct IoStatsDelta {
  std::array<char, IOSTAT_DEVICE_NAME_SIZE> device{}; ///< Device name
  double intervalSec{0.0};                            ///< Measurement interval (seconds)

  // IOPS (operations per second)
  double readIops{0.0};  ///< Read operations per second
  double writeIops{0.0}; ///< Write operations per second
  double totalIops{0.0}; ///< Total IOPS

  // Throughput (bytes per second)
  double readBytesPerSec{0.0};  ///< Read throughput
  double writeBytesPerSec{0.0}; ///< Write throughput
  double totalBytesPerSec{0.0}; ///< Total throughput

  // Latency (average per operation, milliseconds)
  double avgReadLatencyMs{0.0};  ///< Average read latency
  double avgWriteLatencyMs{0.0}; ///< Average write latency

  // Utilization
  double utilizationPct{0.0}; ///< % time device was busy (0-100)
  double avgQueueDepth{0.0};  ///< Average queue depth (weighted time / wall time)

  // Merge statistics
  double readMergesPct{0.0};  ///< % of reads that were merged
  double writeMergesPct{0.0}; ///< % of writes that were merged

  // Discard stats (if available)
  double discardIops{0.0};        ///< TRIM operations per second
  double discardBytesPerSec{0.0}; ///< Discard throughput

  /// @brief Check if device was idle during interval.
  [[nodiscard]] bool isIdle() const noexcept;

  /// @brief Check if device is heavily utilized (>80%).
  [[nodiscard]] bool isHighUtilization() const noexcept;

  /// @brief Get combined throughput in human-readable format.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string formatThroughput() const;

  /// @brief Human-readable summary.
  /// @note NOT RT-safe: Allocates std::string.
  [[nodiscard]] std::string toString() const;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Take I/O statistics snapshot for a block device.
 * @param device Device name (e.g., "nvme0n1", "sda").
 * @return Snapshot with current counters and timestamp.
 * @note RT-safe: Single file read, fixed-size output, bounded parsing.
 *
 * Source: /sys/block/\<dev\>/stat
 */
[[nodiscard]] IoStatsSnapshot getIoStatsSnapshot(const char* device) noexcept;

/**
 * @brief Compute delta between two I/O snapshots.
 * @param before Earlier snapshot.
 * @param after Later snapshot.
 * @return Delta with rates and percentages.
 * @note RT-safe: Pure computation, no I/O, no allocation.
 *
 * The snapshots should be from the same device. If devices don't match
 * or interval is invalid, returns zeroed delta.
 */
[[nodiscard]] IoStatsDelta computeIoStatsDelta(const IoStatsSnapshot& before,
                                               const IoStatsSnapshot& after) noexcept;

} // namespace storage

} // namespace seeker

#endif // SEEKER_STORAGE_IO_STATS_HPP