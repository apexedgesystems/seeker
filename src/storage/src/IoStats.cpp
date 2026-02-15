/**
 * @file IoStats.cpp
 * @brief Implementation of I/O statistics snapshot and delta computation.
 */

#include "src/storage/inc/IoStats.hpp"

#include "src/helpers/inc/Cpu.hpp"
#include "src/helpers/inc/Files.hpp"
#include "src/helpers/inc/Strings.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fmt/core.h>

namespace seeker {

namespace storage {

namespace {

/* ----------------------------- Constants ----------------------------- */

constexpr const char* SYS_BLOCK = "/sys/block";
constexpr std::size_t PATH_BUF_SIZE = 256;
constexpr std::size_t STAT_BUF_SIZE = 512;

/// Sector size in bytes (kernel standard).
constexpr std::uint64_t SECTOR_SIZE = 512;

/// Nanoseconds per second.
constexpr double NS_PER_SEC = 1.0e9;

/// Milliseconds per second.
constexpr double MS_PER_SEC = 1000.0;

/* ----------------------------- Shared Helpers ----------------------------- */

using seeker::helpers::cpu::getMonotonicNs;
using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::strings::copyToBuffer;

/**
 * Parse /sys/block/<dev>/stat file.
 *
 * Format (space-separated, may have leading spaces):
 * Field 1: reads completed
 * Field 2: reads merged
 * Field 3: sectors read
 * Field 4: time reading (ms)
 * Field 5: writes completed
 * Field 6: writes merged
 * Field 7: sectors written
 * Field 8: time writing (ms)
 * Field 9: I/Os currently in progress
 * Field 10: time doing I/Os (ms)
 * Field 11: weighted time doing I/Os (ms)
 * Field 12: discards completed (4.18+)
 * Field 13: discards merged
 * Field 14: sectors discarded
 * Field 15: time discarding (ms)
 * Field 16: flush requests completed (5.5+)
 * Field 17: time flushing (ms)
 */
inline bool parseStatFile(const char* content, IoCounters* counters) noexcept {
  if (content == nullptr || counters == nullptr) {
    return false;
  }

  // Use sscanf for robust parsing of whitespace-separated fields
  // Some fields may be missing on older kernels, so we try multiple formats
  unsigned long long f[17] = {0};

  // Try parsing all 17 fields (kernel 5.5+)
  int parsed = std::sscanf(
      content,
      "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &f[0],
      &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7], &f[8], &f[9], &f[10], &f[11], &f[12], &f[13],
      &f[14], &f[15], &f[16]);

  // Need at least the first 11 fields (basic I/O stats)
  if (parsed < 11) {
    return false;
  }

  counters->readOps = f[0];
  counters->readMerges = f[1];
  counters->readSectors = f[2];
  counters->readTimeMs = f[3];
  counters->writeOps = f[4];
  counters->writeMerges = f[5];
  counters->writeSectors = f[6];
  counters->writeTimeMs = f[7];
  counters->ioInFlight = f[8];
  counters->ioTimeMs = f[9];
  counters->weightedIoTimeMs = f[10];

  // Discard fields (kernel 4.18+)
  if (parsed >= 15) {
    counters->discardOps = f[11];
    counters->discardMerges = f[12];
    counters->discardSectors = f[13];
    counters->discardTimeMs = f[14];
  }

  // Flush fields (kernel 5.5+)
  if (parsed >= 17) {
    counters->flushOps = f[15];
    counters->flushTimeMs = f[16];
  }

  return true;
}

/// Format bytes per second as human-readable throughput.
inline std::string formatBytesPerSec(double bytesPerSec) {
  if (bytesPerSec < 1000.0) {
    return fmt::format("{:.0f} B/s", bytesPerSec);
  }
  if (bytesPerSec < 1000000.0) {
    return fmt::format("{:.1f} KB/s", bytesPerSec / 1000.0);
  }
  if (bytesPerSec < 1000000000.0) {
    return fmt::format("{:.1f} MB/s", bytesPerSec / 1000000.0);
  }
  return fmt::format("{:.2f} GB/s", bytesPerSec / 1000000000.0);
}

} // namespace

/* ----------------------------- IoCounters Methods ----------------------------- */

std::uint64_t IoCounters::readBytes() const noexcept { return readSectors * SECTOR_SIZE; }

std::uint64_t IoCounters::writeBytes() const noexcept { return writeSectors * SECTOR_SIZE; }

std::uint64_t IoCounters::totalOps() const noexcept { return readOps + writeOps; }

std::uint64_t IoCounters::totalBytes() const noexcept { return readBytes() + writeBytes(); }

/* ----------------------------- IoStatsSnapshot Methods ----------------------------- */

std::string IoStatsSnapshot::toString() const {
  return fmt::format("{}: r_ops={} w_ops={} r_sect={} w_sect={} io_ms={}", device.data(),
                     counters.readOps, counters.writeOps, counters.readSectors,
                     counters.writeSectors, counters.ioTimeMs);
}

/* ----------------------------- IoStatsDelta Methods ----------------------------- */

bool IoStatsDelta::isIdle() const noexcept { return totalIops < 0.1 && utilizationPct < 1.0; }

bool IoStatsDelta::isHighUtilization() const noexcept { return utilizationPct > 80.0; }

std::string IoStatsDelta::formatThroughput() const {
  return fmt::format("r={} w={}", formatBytesPerSec(readBytesPerSec),
                     formatBytesPerSec(writeBytesPerSec));
}

std::string IoStatsDelta::toString() const {
  std::string out;

  out += fmt::format("{}: ", device.data());
  out += fmt::format("{:.1f} r/s {:.1f} w/s | ", readIops, writeIops);
  out += fmt::format("r={} w={} | ", formatBytesPerSec(readBytesPerSec),
                     formatBytesPerSec(writeBytesPerSec));
  out += fmt::format("r_lat={:.2f}ms w_lat={:.2f}ms | ", avgReadLatencyMs, avgWriteLatencyMs);
  out += fmt::format("util={:.1f}% qd={:.1f}", utilizationPct, avgQueueDepth);

  return out;
}

/* ----------------------------- API ----------------------------- */

IoStatsSnapshot getIoStatsSnapshot(const char* device) noexcept {
  IoStatsSnapshot snap{};
  if (device == nullptr || device[0] == '\0') {
    return snap;
  }

  copyToBuffer(snap.device.data(), IOSTAT_DEVICE_NAME_SIZE, device);

  char path[PATH_BUF_SIZE];
  std::snprintf(path, sizeof(path), "%s/%s/stat", SYS_BLOCK, device);

  char buf[STAT_BUF_SIZE];
  if (readFileToBuffer(path, buf, sizeof(buf)) == 0) {
    return snap;
  }

  if (!parseStatFile(buf, &snap.counters)) {
    return snap;
  }

  snap.timestampNs = getMonotonicNs();
  return snap;
}

IoStatsDelta computeIoStatsDelta(const IoStatsSnapshot& before,
                                 const IoStatsSnapshot& after) noexcept {
  IoStatsDelta delta{};

  // Verify same device
  if (std::strcmp(before.device.data(), after.device.data()) != 0) {
    return delta;
  }

  // Verify valid timestamps and ordering
  if (before.timestampNs == 0 || after.timestampNs == 0 ||
      after.timestampNs <= before.timestampNs) {
    return delta;
  }

  copyToBuffer(delta.device.data(), IOSTAT_DEVICE_NAME_SIZE, before.device.data());

  // Calculate interval
  const std::uint64_t INTERVAL_NS = after.timestampNs - before.timestampNs;
  delta.intervalSec = static_cast<double>(INTERVAL_NS) / NS_PER_SEC;

  if (delta.intervalSec < 0.001) {
    // Too short interval, avoid division issues
    return delta;
  }

  const IoCounters& B = before.counters;
  const IoCounters& A = after.counters;

  // Calculate deltas (handle counter wrap gracefully)
  const auto safeDelta = [](std::uint64_t after, std::uint64_t before) -> std::uint64_t {
    return (after >= before) ? (after - before) : after; // Assume wrap on underflow
  };

  const std::uint64_t D_READ_OPS = safeDelta(A.readOps, B.readOps);
  const std::uint64_t D_WRITE_OPS = safeDelta(A.writeOps, B.writeOps);
  const std::uint64_t D_READ_SECT = safeDelta(A.readSectors, B.readSectors);
  const std::uint64_t D_WRITE_SECT = safeDelta(A.writeSectors, B.writeSectors);
  const std::uint64_t D_READ_MS = safeDelta(A.readTimeMs, B.readTimeMs);
  const std::uint64_t D_WRITE_MS = safeDelta(A.writeTimeMs, B.writeTimeMs);
  const std::uint64_t D_IO_MS = safeDelta(A.ioTimeMs, B.ioTimeMs);
  const std::uint64_t D_WEIGHTED_MS = safeDelta(A.weightedIoTimeMs, B.weightedIoTimeMs);
  const std::uint64_t D_READ_MERGES = safeDelta(A.readMerges, B.readMerges);
  const std::uint64_t D_WRITE_MERGES = safeDelta(A.writeMerges, B.writeMerges);
  const std::uint64_t D_DISCARD_OPS = safeDelta(A.discardOps, B.discardOps);
  const std::uint64_t D_DISCARD_SECT = safeDelta(A.discardSectors, B.discardSectors);

  // IOPS
  delta.readIops = static_cast<double>(D_READ_OPS) / delta.intervalSec;
  delta.writeIops = static_cast<double>(D_WRITE_OPS) / delta.intervalSec;
  delta.totalIops = delta.readIops + delta.writeIops;

  // Throughput
  delta.readBytesPerSec = static_cast<double>(D_READ_SECT * SECTOR_SIZE) / delta.intervalSec;
  delta.writeBytesPerSec = static_cast<double>(D_WRITE_SECT * SECTOR_SIZE) / delta.intervalSec;
  delta.totalBytesPerSec = delta.readBytesPerSec + delta.writeBytesPerSec;

  // Average latency (time spent / ops completed)
  if (D_READ_OPS > 0) {
    delta.avgReadLatencyMs = static_cast<double>(D_READ_MS) / static_cast<double>(D_READ_OPS);
  }
  if (D_WRITE_OPS > 0) {
    delta.avgWriteLatencyMs = static_cast<double>(D_WRITE_MS) / static_cast<double>(D_WRITE_OPS);
  }

  // Utilization: % of wall time spent doing I/O
  // io_time_ms is cumulative ms device was busy
  const double WALL_MS = delta.intervalSec * MS_PER_SEC;
  if (WALL_MS > 0) {
    delta.utilizationPct = (static_cast<double>(D_IO_MS) / WALL_MS) * 100.0;
    if (delta.utilizationPct > 100.0) {
      delta.utilizationPct = 100.0; // Cap at 100%
    }
  }

  // Average queue depth: weighted_io_time / wall_time
  // weighted_io_time accumulates in_flight * ms for each ms
  if (WALL_MS > 0) {
    delta.avgQueueDepth = static_cast<double>(D_WEIGHTED_MS) / WALL_MS;
  }

  // Merge percentages
  const std::uint64_t TOTAL_READ_REQS = D_READ_OPS + D_READ_MERGES;
  const std::uint64_t TOTAL_WRITE_REQS = D_WRITE_OPS + D_WRITE_MERGES;
  if (TOTAL_READ_REQS > 0) {
    delta.readMergesPct =
        (static_cast<double>(D_READ_MERGES) / static_cast<double>(TOTAL_READ_REQS)) * 100.0;
  }
  if (TOTAL_WRITE_REQS > 0) {
    delta.writeMergesPct =
        (static_cast<double>(D_WRITE_MERGES) / static_cast<double>(TOTAL_WRITE_REQS)) * 100.0;
  }

  // Discard stats
  delta.discardIops = static_cast<double>(D_DISCARD_OPS) / delta.intervalSec;
  delta.discardBytesPerSec = static_cast<double>(D_DISCARD_SECT * SECTOR_SIZE) / delta.intervalSec;

  return delta;
}

} // namespace storage

} // namespace seeker