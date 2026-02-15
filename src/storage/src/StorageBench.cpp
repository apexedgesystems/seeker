/**
 * @file StorageBench.cpp
 * @brief Implementation of bounded filesystem micro-benchmarks.
 */

#include "src/storage/inc/StorageBench.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include <fmt/core.h>

namespace seeker {

namespace storage {

namespace {

/* ----------------------------- Constants ----------------------------- */

constexpr std::size_t MAX_LATENCY_SAMPLES = 100000;
constexpr double NS_PER_SEC = 1.0e9;
constexpr double NS_PER_US = 1000.0;

/* ----------------------------- Time Helpers ----------------------------- */

/// Get monotonic time in nanoseconds.
inline std::uint64_t getTimeNs() noexcept {
  struct timespec ts{};
  if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL +
         static_cast<std::uint64_t>(ts.tv_nsec);
}

/// Convert nanoseconds to seconds.
inline double nsToSec(std::uint64_t ns) noexcept { return static_cast<double>(ns) / NS_PER_SEC; }

/// Convert nanoseconds to microseconds.
inline double nsToUs(std::uint64_t ns) noexcept { return static_cast<double>(ns) / NS_PER_US; }

/* ----------------------------- File Helpers ----------------------------- */

/// Generate unique temp file path in given directory.
inline void makeTempPath(char* pathBuf, std::size_t bufSize, const char* dir) noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
  std::snprintf(pathBuf, bufSize, "%s/storagebench_%d_%lu.tmp", dir, static_cast<int>(::getpid()),
                static_cast<unsigned long>(getTimeNs() % 1000000));
#pragma GCC diagnostic pop
}

/// Remove file if it exists.
inline void removeFile(const char* path) noexcept { ::unlink(path); }

/// Open file with specified flags. Returns fd or -1 on error.
inline int openFile(const char* path, int flags, bool directIo) noexcept {
  int effectiveFlags = flags | O_CLOEXEC;
#ifdef O_DIRECT
  if (directIo) {
    effectiveFlags |= O_DIRECT;
  }
#else
  (void)directIo;
#endif
  return ::open(path, effectiveFlags, 0644);
}

/// Allocate aligned buffer for O_DIRECT.
/// Caller must free with std::free().
inline void* allocAlignedBuffer(std::size_t size, std::size_t alignment = 4096) noexcept {
  void* ptr = nullptr;
  if (::posix_memalign(&ptr, alignment, size) != 0) {
    return nullptr;
  }
  return ptr;
}

/* ----------------------------- Latency Statistics ----------------------------- */

/// Compute latency statistics from samples (in nanoseconds).
inline void computeLatencyStats(std::vector<std::uint64_t>& samples, BenchResult* result) noexcept {
  if (samples.empty()) {
    return;
  }

  std::sort(samples.begin(), samples.end());

  const std::size_t N = samples.size();

  // Min/Max
  result->minLatencyUs = nsToUs(samples.front());
  result->maxLatencyUs = nsToUs(samples.back());

  // Average
  std::uint64_t sum = 0;
  for (const std::uint64_t S : samples) {
    sum += S;
  }
  result->avgLatencyUs = nsToUs(sum) / static_cast<double>(N);

  // P99
  const std::size_t P99_IDX = (N * 99) / 100;
  result->p99LatencyUs = nsToUs(samples[P99_IDX]);
}

/* ----------------------------- Throughput Formatting ----------------------------- */

inline std::string formatBytesPerSec(double bps) {
  if (bps < 1000.0) {
    return fmt::format("{:.0f} B/s", bps);
  }
  if (bps < 1000000.0) {
    return fmt::format("{:.1f} KB/s", bps / 1000.0);
  }
  if (bps < 1000000000.0) {
    return fmt::format("{:.1f} MB/s", bps / 1000000.0);
  }
  return fmt::format("{:.2f} GB/s", bps / 1000000000.0);
}

} // namespace

/* ----------------------------- BenchConfig Methods ----------------------------- */

void BenchConfig::setDirectory(const char* path) noexcept {
  if (path == nullptr) {
    directory[0] = '\0';
    return;
  }
  std::size_t i = 0;
  while (i < BENCH_PATH_SIZE - 1 && path[i] != '\0') {
    directory[i] = path[i];
    ++i;
  }
  directory[i] = '\0';
}

bool BenchConfig::isValid() const noexcept {
  // Directory must be set
  if (directory[0] == '\0') {
    return false;
  }
  // Verify directory exists
  struct stat st{};
  if (::stat(directory.data(), &st) != 0 || !S_ISDIR(st.st_mode)) {
    return false;
  }
  // I/O size must be reasonable
  if (ioSize < 512 || ioSize > 64 * 1024 * 1024) {
    return false;
  }
  // Data size must be >= I/O size
  if (dataSize < ioSize) {
    return false;
  }
  // Must have at least 1 iteration
  if (iterations < 1) {
    return false;
  }
  return true;
}

/* ----------------------------- BenchResult Methods ----------------------------- */

std::string BenchResult::formatThroughput() const {
  return formatBytesPerSec(throughputBytesPerSec);
}

std::string BenchResult::toString() const {
  if (!success) {
    return "FAILED";
  }

  std::string out;
  out += fmt::format("{} ops in {:.3f}s", operations, elapsedSec);

  if (throughputBytesPerSec > 0) {
    out += fmt::format(" ({} transferred, {})",
                       fmt::format("{:.1f} MB", static_cast<double>(bytesTransferred) / 1000000.0),
                       formatThroughput());
  }

  if (avgLatencyUs > 0) {
    out += fmt::format(" lat: avg={:.1f}us min={:.1f}us max={:.1f}us p99={:.1f}us", avgLatencyUs,
                       minLatencyUs, maxLatencyUs, p99LatencyUs);
  }

  return out;
}

/* ----------------------------- BenchSuite Methods ----------------------------- */

bool BenchSuite::allSuccess() const noexcept {
  return seqWrite.success && seqRead.success && fsyncLatency.success && randRead.success &&
         randWrite.success;
}

std::string BenchSuite::toString() const {
  std::string out;
  out += "Storage Benchmark Results:\n";
  out += fmt::format("  Seq Write:   {}\n", seqWrite.toString());
  out += fmt::format("  Seq Read:    {}\n", seqRead.toString());
  out += fmt::format("  fsync Lat:   {}\n", fsyncLatency.toString());
  out += fmt::format("  Rand Read:   {}\n", randRead.toString());
  out += fmt::format("  Rand Write:  {}\n", randWrite.toString());
  return out;
}

/* ----------------------------- Benchmark Implementations ----------------------------- */

BenchResult runSeqWriteBench(const BenchConfig& config) noexcept {
  BenchResult result{};

  if (!config.isValid()) {
    return result;
  }

  // Allocate aligned buffer
  void* buf = allocAlignedBuffer(config.ioSize);
  if (buf == nullptr) {
    return result;
  }

  // Fill buffer with pattern
  std::memset(buf, 0xAA, config.ioSize);

  // Create temp file
  std::array<char, BENCH_PATH_SIZE> tempPath{};
  makeTempPath(tempPath.data(), tempPath.size(), config.directory.data());

  const int FD = openFile(tempPath.data(), O_WRONLY | O_CREAT | O_TRUNC, config.useDirectIo);
  if (FD < 0) {
    std::free(buf);
    return result;
  }

  const std::uint64_t START = getTimeNs();
  const std::uint64_t DEADLINE =
      START + static_cast<std::uint64_t>(config.timeBudgetSec * NS_PER_SEC);

  std::size_t bytesWritten = 0;
  std::size_t ops = 0;

  while (bytesWritten < config.dataSize) {
    // Check time budget
    if (getTimeNs() > DEADLINE) {
      break;
    }

    const ssize_t WRITTEN = ::write(FD, buf, config.ioSize);
    if (WRITTEN <= 0) {
      break;
    }

    bytesWritten += static_cast<std::size_t>(WRITTEN);
    ++ops;
  }

  // Sync if requested
  if (config.useFsync) {
    ::fsync(FD);
  }

  const std::uint64_t END = getTimeNs();

  ::close(FD);
  removeFile(tempPath.data());
  std::free(buf);

  result.success = (bytesWritten > 0);
  result.elapsedSec = nsToSec(END - START);
  result.operations = ops;
  result.bytesTransferred = bytesWritten;
  result.throughputBytesPerSec = static_cast<double>(bytesWritten) / result.elapsedSec;

  return result;
}

BenchResult runSeqReadBench(const BenchConfig& config) noexcept {
  BenchResult result{};

  if (!config.isValid()) {
    return result;
  }

  void* buf = allocAlignedBuffer(config.ioSize);
  if (buf == nullptr) {
    return result;
  }

  // Create temp file with data
  std::array<char, BENCH_PATH_SIZE> tempPath{};
  makeTempPath(tempPath.data(), tempPath.size(), config.directory.data());

  // Write phase (not timed)
  int fd = openFile(tempPath.data(), O_WRONLY | O_CREAT | O_TRUNC, config.useDirectIo);
  if (fd < 0) {
    std::free(buf);
    return result;
  }

  std::memset(buf, 0x55, config.ioSize);
  std::size_t written = 0;
  while (written < config.dataSize) {
    const ssize_t W = ::write(fd, buf, config.ioSize);
    if (W <= 0) {
      break;
    }
    written += static_cast<std::size_t>(W);
  }
  ::fsync(fd);
  ::close(fd);

  // Drop caches if possible (requires root, will fail silently otherwise)
  ::sync();

  // Read phase (timed)
  fd = openFile(tempPath.data(), O_RDONLY, config.useDirectIo);
  if (fd < 0) {
    removeFile(tempPath.data());
    std::free(buf);
    return result;
  }

  const std::uint64_t START = getTimeNs();
  const std::uint64_t DEADLINE =
      START + static_cast<std::uint64_t>(config.timeBudgetSec * NS_PER_SEC);

  std::size_t bytesRead = 0;
  std::size_t ops = 0;

  while (bytesRead < written) {
    if (getTimeNs() > DEADLINE) {
      break;
    }

    const ssize_t R = ::read(fd, buf, config.ioSize);
    if (R <= 0) {
      break;
    }

    bytesRead += static_cast<std::size_t>(R);
    ++ops;
  }

  const std::uint64_t END = getTimeNs();

  ::close(fd);
  removeFile(tempPath.data());
  std::free(buf);

  result.success = (bytesRead > 0);
  result.elapsedSec = nsToSec(END - START);
  result.operations = ops;
  result.bytesTransferred = bytesRead;
  result.throughputBytesPerSec = static_cast<double>(bytesRead) / result.elapsedSec;

  return result;
}

BenchResult runFsyncBench(const BenchConfig& config) noexcept {
  BenchResult result{};

  if (!config.isValid()) {
    return result;
  }

  // Small buffer for fsync test
  constexpr std::size_t FSYNC_BLOCK = 4096;
  void* buf = allocAlignedBuffer(FSYNC_BLOCK);
  if (buf == nullptr) {
    return result;
  }
  std::memset(buf, 0xCC, FSYNC_BLOCK);

  std::array<char, BENCH_PATH_SIZE> tempPath{};
  makeTempPath(tempPath.data(), tempPath.size(), config.directory.data());

  const int FD = openFile(tempPath.data(), O_WRONLY | O_CREAT | O_TRUNC, config.useDirectIo);
  if (FD < 0) {
    std::free(buf);
    return result;
  }

  std::vector<std::uint64_t> latencies;
  latencies.reserve(std::min(config.iterations, MAX_LATENCY_SAMPLES));

  const std::uint64_t START = getTimeNs();
  const std::uint64_t DEADLINE =
      START + static_cast<std::uint64_t>(config.timeBudgetSec * NS_PER_SEC);

  std::size_t ops = 0;

  for (std::size_t i = 0; i < config.iterations; ++i) {
    if (getTimeNs() > DEADLINE) {
      break;
    }

    // Write a small block
    const ssize_t W = ::write(FD, buf, FSYNC_BLOCK);
    if (W <= 0) {
      break;
    }

    // Measure fsync latency
    const std::uint64_t T0 = getTimeNs();
    const int RC = ::fsync(FD);
    const std::uint64_t T1 = getTimeNs();

    if (RC != 0) {
      break;
    }

    if (latencies.size() < MAX_LATENCY_SAMPLES) {
      latencies.push_back(T1 - T0);
    }
    ++ops;
  }

  const std::uint64_t END = getTimeNs();

  ::close(FD);
  removeFile(tempPath.data());
  std::free(buf);

  result.success = (ops > 0);
  result.elapsedSec = nsToSec(END - START);
  result.operations = ops;
  result.bytesTransferred = ops * FSYNC_BLOCK;

  computeLatencyStats(latencies, &result);

  return result;
}

BenchResult runRandReadBench(const BenchConfig& config) noexcept {
  BenchResult result{};

  if (!config.isValid()) {
    return result;
  }

  constexpr std::size_t RAND_BLOCK = 4096;
  void* buf = allocAlignedBuffer(RAND_BLOCK);
  if (buf == nullptr) {
    return result;
  }

  // Create file with enough data for random access
  std::array<char, BENCH_PATH_SIZE> tempPath{};
  makeTempPath(tempPath.data(), tempPath.size(), config.directory.data());

  int fd = openFile(tempPath.data(), O_RDWR | O_CREAT | O_TRUNC, config.useDirectIo);
  if (fd < 0) {
    std::free(buf);
    return result;
  }

  // Write data (not timed)
  std::memset(buf, 0x77, RAND_BLOCK);
  std::size_t written = 0;
  while (written < config.dataSize) {
    const ssize_t W = ::write(fd, buf, RAND_BLOCK);
    if (W <= 0) {
      break;
    }
    written += static_cast<std::size_t>(W);
  }
  ::fsync(fd);

  const std::size_t NUM_BLOCKS = written / RAND_BLOCK;
  if (NUM_BLOCKS < 2) {
    ::close(fd);
    removeFile(tempPath.data());
    std::free(buf);
    return result;
  }

  // Random read phase
  std::mt19937_64 rng(42); // Fixed seed for reproducibility
  std::uniform_int_distribution<std::size_t> dist(0, NUM_BLOCKS - 1);

  std::vector<std::uint64_t> latencies;
  latencies.reserve(std::min(config.iterations, MAX_LATENCY_SAMPLES));

  const std::uint64_t START = getTimeNs();
  const std::uint64_t DEADLINE =
      START + static_cast<std::uint64_t>(config.timeBudgetSec * NS_PER_SEC);

  std::size_t ops = 0;
  std::size_t bytesRead = 0;

  for (std::size_t i = 0; i < config.iterations; ++i) {
    if (getTimeNs() > DEADLINE) {
      break;
    }

    const off_t OFFSET = static_cast<off_t>(dist(rng) * RAND_BLOCK);

    const std::uint64_t T0 = getTimeNs();
    const ssize_t R = ::pread(fd, buf, RAND_BLOCK, OFFSET);
    const std::uint64_t T1 = getTimeNs();

    if (R <= 0) {
      break;
    }

    if (latencies.size() < MAX_LATENCY_SAMPLES) {
      latencies.push_back(T1 - T0);
    }
    bytesRead += static_cast<std::size_t>(R);
    ++ops;
  }

  const std::uint64_t END = getTimeNs();

  ::close(fd);
  removeFile(tempPath.data());
  std::free(buf);

  result.success = (ops > 0);
  result.elapsedSec = nsToSec(END - START);
  result.operations = ops;
  result.bytesTransferred = bytesRead;
  result.throughputBytesPerSec = static_cast<double>(bytesRead) / result.elapsedSec;

  computeLatencyStats(latencies, &result);

  return result;
}

BenchResult runRandWriteBench(const BenchConfig& config) noexcept {
  BenchResult result{};

  if (!config.isValid()) {
    return result;
  }

  constexpr std::size_t RAND_BLOCK = 4096;
  void* buf = allocAlignedBuffer(RAND_BLOCK);
  if (buf == nullptr) {
    return result;
  }
  std::memset(buf, 0x88, RAND_BLOCK);

  // Create pre-allocated file
  std::array<char, BENCH_PATH_SIZE> tempPath{};
  makeTempPath(tempPath.data(), tempPath.size(), config.directory.data());

  int fd = openFile(tempPath.data(), O_RDWR | O_CREAT | O_TRUNC, config.useDirectIo);
  if (fd < 0) {
    std::free(buf);
    return result;
  }

  // Pre-allocate file
  std::size_t written = 0;
  while (written < config.dataSize) {
    const ssize_t W = ::write(fd, buf, RAND_BLOCK);
    if (W <= 0) {
      break;
    }
    written += static_cast<std::size_t>(W);
  }
  ::fsync(fd);

  const std::size_t NUM_BLOCKS = written / RAND_BLOCK;
  if (NUM_BLOCKS < 2) {
    ::close(fd);
    removeFile(tempPath.data());
    std::free(buf);
    return result;
  }

  // Random write phase
  std::mt19937_64 rng(42);
  std::uniform_int_distribution<std::size_t> dist(0, NUM_BLOCKS - 1);

  std::vector<std::uint64_t> latencies;
  latencies.reserve(std::min(config.iterations, MAX_LATENCY_SAMPLES));

  const std::uint64_t START = getTimeNs();
  const std::uint64_t DEADLINE =
      START + static_cast<std::uint64_t>(config.timeBudgetSec * NS_PER_SEC);

  std::size_t ops = 0;
  std::size_t bytesWritten = 0;

  for (std::size_t i = 0; i < config.iterations; ++i) {
    if (getTimeNs() > DEADLINE) {
      break;
    }

    const off_t OFFSET = static_cast<off_t>(dist(rng) * RAND_BLOCK);

    const std::uint64_t T0 = getTimeNs();
    const ssize_t W = ::pwrite(fd, buf, RAND_BLOCK, OFFSET);
    if (W > 0 && config.useFsync) {
      ::fdatasync(fd); // fdatasync is faster than fsync for data-only sync
    }
    const std::uint64_t T1 = getTimeNs();

    if (W <= 0) {
      break;
    }

    if (latencies.size() < MAX_LATENCY_SAMPLES) {
      latencies.push_back(T1 - T0);
    }
    bytesWritten += static_cast<std::size_t>(W);
    ++ops;
  }

  const std::uint64_t END = getTimeNs();

  ::close(fd);
  removeFile(tempPath.data());
  std::free(buf);

  result.success = (ops > 0);
  result.elapsedSec = nsToSec(END - START);
  result.operations = ops;
  result.bytesTransferred = bytesWritten;
  result.throughputBytesPerSec = static_cast<double>(bytesWritten) / result.elapsedSec;

  computeLatencyStats(latencies, &result);

  return result;
}

BenchSuite runBenchSuite(const BenchConfig& config) noexcept {
  BenchSuite suite{};

  suite.seqWrite = runSeqWriteBench(config);
  suite.seqRead = runSeqReadBench(config);
  suite.fsyncLatency = runFsyncBench(config);
  suite.randRead = runRandReadBench(config);
  suite.randWrite = runRandWriteBench(config);

  return suite;
}

} // namespace storage

} // namespace seeker