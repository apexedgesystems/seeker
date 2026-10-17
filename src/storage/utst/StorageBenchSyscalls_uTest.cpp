/**
 * @file StorageBenchSyscalls_uTest.cpp
 * @brief Tests of the system calls the storage benchmarks make, observed
 *        through linker-wrapped write, read, pread, pwrite, fdatasync, fsync,
 *        clock_gettime and posix_fadvise.
 *
 * Notes:
 *  - The test target compiles StorageBench.cpp itself and links with
 *    --wrap for each call above (src/storage/utst/CMakeLists.txt): --wrap
 *    rewrites references only inside the link, so the benchmarks cannot come
 *    from the shared library.
 *  - While a test runs a benchmark the wrappers are armed: writes and reads
 *    move no data, syncs succeed unless a test injects EIO, and the clock
 *    advances 1 us per call, so results do not depend on the host. Disarmed,
 *    the wrappers call the real functions.
 *  - open, close and unlink stay real: each benchmark creates and removes its
 *    (empty) file in the test's scratch tree.
 */

#include "src/storage/inc/StorageBench.hpp"
#include "src/helpers/utst/inc/ScratchTree.hpp"

#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/types.h>
#include <time.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <numeric>
#include <system_error>
#include <utility>
#include <vector>

using seeker::storage::BENCH_PATH_SIZE;
using seeker::storage::BenchConfig;
using seeker::storage::BenchResult;
using seeker::storage::runFsyncBench;
using seeker::storage::runRandReadBench;
using seeker::storage::runRandWriteBench;
using seeker::storage::runSeqReadBench;
using seeker::storage::runSeqWriteBench;
using seeker::storage::SYNC_INTERVAL_BYTES;
using seeker::test_support::ScratchTreeTest;

namespace {

constexpr std::size_t KIB = 1024;
constexpr std::size_t MIB = 1024 * KIB;

/// Synthetic clock step per clock_gettime call while armed.
constexpr std::uint64_t CLOCK_STEP_NS = 1000;

/// A posix_fadvise call and what had happened before it.
struct Eviction {
  int fd{-1};
  off_t offset{-1};
  off_t len{-1};
  int advice{-1};
  std::size_t syncsBefore{0}; ///< fdatasync and fsync calls before it
  std::size_t readsBefore{0}; ///< read and pread calls before it
  int lastSyncFd{-1};         ///< File descriptor of the last sync before it
};

/// What the wrapped calls saw while armed.
struct Wrapped {
  bool armed{false};
  std::uint64_t clockNs{0};
  std::size_t writtenBytes{0};        ///< Bytes passed to write and pwrite
  std::size_t unsyncedBytes{0};       ///< Written since the last successful sync
  std::vector<std::size_t> flushes{}; ///< Unsynced bytes at each fdatasync call
  std::size_t fdatasyncCalls{0};
  std::size_t failFdatasyncAt{0}; ///< 1-based fdatasync call that fails with EIO; 0 for none
  std::size_t fsyncCalls{0};
  std::size_t failFsyncAt{0}; ///< 1-based fsync call that fails with EIO; 0 for none
  std::size_t readCalls{0};
  std::size_t readBytes{0}; ///< Bytes requested by read and pread
  int lastSyncFd{-1};
  std::vector<Eviction> evictions{};
};

Wrapped wrapped{};

/// Sync accounting shared by the fdatasync and fsync wrappers.
int recordSync(int fd, std::size_t calls, std::size_t failAt) {
  wrapped.lastSyncFd = fd;
  if (calls == failAt) {
    errno = EIO;
    return -1;
  }
  wrapped.unsyncedBytes = 0;
  return 0;
}

} // namespace

/* ----------------------------- Wrapped Calls ----------------------------- */

extern "C" {

ssize_t __real_write(int fd, const void* buf, std::size_t count);
ssize_t __real_read(int fd, void* buf, std::size_t count);
ssize_t __real_pread(int fd, void* buf, std::size_t count, off_t offset);
ssize_t __real_pwrite(int fd, const void* buf, std::size_t count, off_t offset);
int __real_fdatasync(int fd);
int __real_fsync(int fd);
int __real_clock_gettime(clockid_t clockId, struct timespec* ts);
int __real_posix_fadvise(int fd, off_t offset, off_t len, int advice);

ssize_t __wrap_write(int fd, const void* buf, std::size_t count) {
  if (!wrapped.armed) {
    return __real_write(fd, buf, count);
  }
  wrapped.writtenBytes += count;
  wrapped.unsyncedBytes += count;
  return static_cast<ssize_t>(count);
}

ssize_t __wrap_pwrite(int fd, const void* buf, std::size_t count, off_t offset) {
  if (!wrapped.armed) {
    return __real_pwrite(fd, buf, count, offset);
  }
  wrapped.writtenBytes += count;
  wrapped.unsyncedBytes += count;
  return static_cast<ssize_t>(count);
}

ssize_t __wrap_read(int fd, void* buf, std::size_t count) {
  if (!wrapped.armed) {
    return __real_read(fd, buf, count);
  }
  ++wrapped.readCalls;
  wrapped.readBytes += count;
  return static_cast<ssize_t>(count);
}

ssize_t __wrap_pread(int fd, void* buf, std::size_t count, off_t offset) {
  if (!wrapped.armed) {
    return __real_pread(fd, buf, count, offset);
  }
  ++wrapped.readCalls;
  wrapped.readBytes += count;
  return static_cast<ssize_t>(count);
}

int __wrap_fdatasync(int fd) {
  if (!wrapped.armed) {
    return __real_fdatasync(fd);
  }
  ++wrapped.fdatasyncCalls;
  wrapped.flushes.push_back(wrapped.unsyncedBytes);
  return recordSync(fd, wrapped.fdatasyncCalls, wrapped.failFdatasyncAt);
}

int __wrap_fsync(int fd) {
  if (!wrapped.armed) {
    return __real_fsync(fd);
  }
  ++wrapped.fsyncCalls;
  return recordSync(fd, wrapped.fsyncCalls, wrapped.failFsyncAt);
}

int __wrap_clock_gettime(clockid_t clockId, struct timespec* ts) {
  if (!wrapped.armed) {
    return __real_clock_gettime(clockId, ts);
  }
  wrapped.clockNs += CLOCK_STEP_NS;
  ts->tv_sec = static_cast<time_t>(wrapped.clockNs / 1000000000ULL);
  ts->tv_nsec = static_cast<long>(wrapped.clockNs % 1000000000ULL);
  return 0;
}

int __wrap_posix_fadvise(int fd, off_t offset, off_t len, int advice) {
  if (!wrapped.armed) {
    return __real_posix_fadvise(fd, offset, len, advice);
  }
  wrapped.evictions.push_back(Eviction{fd, offset, len, advice,
                                       wrapped.fdatasyncCalls + wrapped.fsyncCalls,
                                       wrapped.readCalls, wrapped.lastSyncFd});
  return 0;
}

} // extern "C"

/* ----------------------------- Fixtures ----------------------------- */

namespace {

/// Signature shared by the single-benchmark functions.
using BenchFn = BenchResult (*)(const BenchConfig&) noexcept;

/// A benchmark function and its name for traces.
using NamedBench = std::pair<const char*, BenchFn>;

/// Longest file name a benchmark appends to its directory: the pattern
/// "%s/storagebench_%d_%lu.tmp" of makeTempPath() in StorageBench.cpp, with
/// an int pid of at most 10 digits and a counter below 1000000 (6 digits).
constexpr std::size_t LONGEST_BENCH_FILE_NAME = sizeof("/storagebench__.tmp") - 1 + 10 + 6;

} // namespace

/// Runs benchmarks in the test's scratch tree with the wrappers armed.
class StorageBenchSyscallsTest : public ScratchTreeTest {
protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(ScratchTreeTest::SetUp());
    ASSERT_LE(root_.string().size() + LONGEST_BENCH_FILE_NAME + 1, BENCH_PATH_SIZE)
        << "benchmark file paths under " << root_ << " would not fit BENCH_PATH_SIZE ("
        << BENCH_PATH_SIZE << "); set a shorter TEST_TMPDIR";
    wrapped = Wrapped{};
  }

  void TearDown() override {
    wrapped.armed = false;
    ScratchTreeTest::TearDown();
  }

  /// A valid configuration for the scratch tree: 20 latency iterations and
  /// the default 30 s budget, which the synthetic clock never reaches.
  [[nodiscard]] BenchConfig makeConfig(std::size_t ioSize, std::size_t dataSize,
                                       bool useFsync) const {
    BenchConfig config{};
    config.setDirectory(root_.c_str());
    config.ioSize = ioSize;
    config.dataSize = dataSize;
    config.iterations = 20;
    config.useFsync = useFsync;
    return config;
  }

  /// Run one benchmark from a clean count, with sync failures injected as given.
  BenchResult run(BenchFn bench, const BenchConfig& config, std::size_t failFdatasyncAt = 0,
                  std::size_t failFsyncAt = 0) {
    wrapped = Wrapped{};
    wrapped.failFdatasyncAt = failFdatasyncAt;
    wrapped.failFsyncAt = failFsyncAt;
    wrapped.armed = true;
    const BenchResult RESULT = bench(config);
    wrapped.armed = false;
    return RESULT;
  }

  /// Entries left in the scratch tree (benchmark files not removed).
  [[nodiscard]] std::size_t entriesLeft() const {
    std::error_code ec;
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(root_, ec),
                                                  std::filesystem::directory_iterator()));
  }
};

class BenchSyncIntervalTest : public StorageBenchSyscallsTest {};
class BenchDataSizeTest : public StorageBenchSyscallsTest {};
class BenchSyncFailureTest : public StorageBenchSyscallsTest {};
class BenchEvictionTest : public StorageBenchSyscallsTest {};

/* ----------------------------- Bytes Between Syncs ----------------------------- */

namespace {

/// Block sizes below, at a non-divisor of, and above SYNC_INTERVAL_BYTES.
constexpr std::array<std::size_t, 4> BLOCK_SIZES{4 * KIB, 3 * MIB, 8 * MIB, 64 * MIB};

/// More than three blocks of any size above, and not a whole number of them.
constexpr std::size_t dataSizeFor(std::size_t ioSize) {
  return std::max<std::size_t>(16 * MIB, 3 * ioSize) + 1;
}

/// Check every sync of a synced sequential write of ioSize blocks.
void expectSyncsBounded(std::size_t ioSize) {
  ASSERT_FALSE(wrapped.flushes.empty());
  const std::size_t BOUND = std::max(SYNC_INTERVAL_BYTES, ioSize);
  for (std::size_t i = 0; i < wrapped.flushes.size(); ++i) {
    EXPECT_LE(wrapped.flushes[i], BOUND) << "sync " << i + 1;
    EXPECT_GT(wrapped.flushes[i], 0U) << "sync " << i + 1 << " flushed nothing";
  }
  // Every written byte is synced exactly once, and none is left unsynced
  EXPECT_EQ(std::accumulate(wrapped.flushes.begin(), wrapped.flushes.end(), std::size_t{0}),
            wrapped.writtenBytes);
  EXPECT_EQ(wrapped.unsyncedBytes, 0U);
}

} // namespace

/** @test Synced sequential writes flush at most SYNC_INTERVAL_BYTES or one larger block per sync */
TEST_F(BenchSyncIntervalTest, SeqWriteFlushesAtMostTheIntervalOrOneBlock) {
  for (const std::size_t IO : BLOCK_SIZES) {
    SCOPED_TRACE(::testing::Message() << "ioSize " << IO);
    const BenchResult RESULT = run(&runSeqWriteBench, makeConfig(IO, dataSizeFor(IO), true));
    ASSERT_TRUE(RESULT.success);
    EXPECT_EQ(RESULT.bytesTransferred, wrapped.writtenBytes);
    expectSyncsBounded(IO);
  }
}

/** @test The sequential-read setup syncs the same way with useFsync off */
TEST_F(BenchSyncIntervalTest, SeqReadSetupFlushesAtMostTheIntervalOrOneBlock) {
  for (const std::size_t IO : BLOCK_SIZES) {
    SCOPED_TRACE(::testing::Message() << "ioSize " << IO);
    const BenchResult RESULT = run(&runSeqReadBench, makeConfig(IO, dataSizeFor(IO), false));
    ASSERT_TRUE(RESULT.success);
    EXPECT_EQ(RESULT.bytesTransferred, wrapped.writtenBytes);
    expectSyncsBounded(IO);
  }
}

/* ----------------------------- Bytes Within dataSize ----------------------------- */

/** @test The sequential benchmarks issue and report whole blocks and never pass dataSize */
TEST_F(BenchDataSizeTest, SeqWriteAndReadMoveWholeBlocksWithinDataSize) {
  const std::array<std::pair<std::size_t, std::size_t>, 3> CASES{{
      {4 * KIB, 4 * KIB + 1},
      {4 * KIB, 1000000},
      {8 * MIB, 8 * MIB + 1},
  }};
  for (const auto& [io, data] : CASES) {
    SCOPED_TRACE(::testing::Message() << "ioSize " << io << " dataSize " << data);
    const std::size_t WHOLE_BLOCKS = (data / io) * io;

    // The bytes issued to the kernel are the whole blocks, and what is reported
    const BenchResult WRITE = run(&runSeqWriteBench, makeConfig(io, data, true));
    EXPECT_EQ(WRITE.bytesTransferred, WHOLE_BLOCKS);
    EXPECT_EQ(wrapped.writtenBytes, WHOLE_BLOCKS);

    const BenchResult READ = run(&runSeqReadBench, makeConfig(io, data, true));
    EXPECT_EQ(READ.bytesTransferred, WHOLE_BLOCKS);
    EXPECT_EQ(wrapped.writtenBytes, WHOLE_BLOCKS) << "setup";
    EXPECT_EQ(wrapped.readBytes, WHOLE_BLOCKS);
    EXPECT_EQ(wrapped.readBytes, READ.bytesTransferred);
  }
}

/* ----------------------------- Failed Syncs ----------------------------- */

namespace {

/// A 6 MiB setup or write of 4 KiB blocks syncs twice: at the interval and at the end.
constexpr std::size_t TWO_SYNC_DATA_SIZE = 6 * MIB;

} // namespace

/** @test A failed interval or final sync fails the sequential write and every setup */
TEST_F(BenchSyncFailureTest, FailedSetupOrSeqWriteSyncFailsTheBenchmark) {
  const std::array<NamedBench, 4> BENCHES{{
      {"runSeqWriteBench", &runSeqWriteBench},
      {"runSeqReadBench", &runSeqReadBench},
      {"runRandReadBench", &runRandReadBench},
      {"runRandWriteBench", &runRandWriteBench},
  }};
  for (const auto& [name, bench] : BENCHES) {
    SCOPED_TRACE(name);
    const BenchConfig CONFIG = makeConfig(4 * KIB, TWO_SYNC_DATA_SIZE, true);

    const BenchResult CONTROL = run(bench, CONFIG);
    EXPECT_TRUE(CONTROL.success) << "no failure injected";
    ASSERT_GE(wrapped.fdatasyncCalls, 2U);

    for (const std::size_t FAIL_AT : {std::size_t{1}, std::size_t{2}}) {
      SCOPED_TRACE(::testing::Message() << "EIO on fdatasync " << FAIL_AT);
      const BenchResult RESULT = run(bench, CONFIG, FAIL_AT);
      EXPECT_FALSE(RESULT.success);
      EXPECT_EQ(RESULT.operations, 0U);
      EXPECT_EQ(RESULT.bytesTransferred, 0U);
      EXPECT_EQ(wrapped.fdatasyncCalls, FAIL_AT) << "the benchmark went on after the failure";
      EXPECT_EQ(entriesLeft(), 0U);
    }
  }
}

/** @test A failed per-write sync fails the random-write benchmark, first or later */
TEST_F(BenchSyncFailureTest, FailedRandomWriteSyncFailsTheBenchmark) {
  const BenchConfig CONFIG = makeConfig(4 * KIB, TWO_SYNC_DATA_SIZE, true);
  // The setup's two syncs come first; call 3 is the first per-write sync
  for (const std::size_t FAIL_AT : {std::size_t{3}, std::size_t{10}}) {
    SCOPED_TRACE(::testing::Message() << "EIO on fdatasync " << FAIL_AT);
    const BenchResult RESULT = run(&runRandWriteBench, CONFIG, FAIL_AT);
    EXPECT_FALSE(RESULT.success);
    EXPECT_EQ(RESULT.operations, 0U);
    EXPECT_EQ(RESULT.bytesTransferred, 0U);
    EXPECT_EQ(wrapped.fdatasyncCalls, FAIL_AT);
    EXPECT_EQ(entriesLeft(), 0U);
  }
}

/** @test A failed fsync fails the fsync benchmark, first or later */
TEST_F(BenchSyncFailureTest, FailedFsyncFailsTheFsyncBenchmark) {
  const BenchConfig CONFIG = makeConfig(4 * KIB, TWO_SYNC_DATA_SIZE, true);
  for (const std::size_t FAIL_AT : {std::size_t{1}, std::size_t{10}}) {
    SCOPED_TRACE(::testing::Message() << "EIO on fsync " << FAIL_AT);
    const BenchResult RESULT = run(&runFsyncBench, CONFIG, 0, FAIL_AT);
    EXPECT_FALSE(RESULT.success);
    EXPECT_EQ(RESULT.operations, 0U);
    EXPECT_EQ(RESULT.bytesTransferred, 0U);
    EXPECT_EQ(wrapped.fsyncCalls, FAIL_AT);
    EXPECT_EQ(entriesLeft(), 0U);
  }
}

/* ----------------------------- Page Eviction ----------------------------- */

/** @test Each read benchmark evicts its whole file once, after its last sync and before any read */
TEST_F(BenchEvictionTest, EachReadBenchmarkEvictsItsWholeFileOnceAfterSyncing) {
  const std::array<NamedBench, 2> BENCHES{{
      {"runSeqReadBench", &runSeqReadBench},
      {"runRandReadBench", &runRandReadBench},
  }};
  for (const auto& [name, bench] : BENCHES) {
    SCOPED_TRACE(name);
    const BenchResult RESULT = run(bench, makeConfig(4 * KIB, TWO_SYNC_DATA_SIZE, true));
    ASSERT_TRUE(RESULT.success);
    ASSERT_EQ(wrapped.evictions.size(), 1U);

    const Eviction& eviction = wrapped.evictions.front();
    EXPECT_EQ(eviction.advice, POSIX_FADV_DONTNEED);
    EXPECT_EQ(eviction.offset, 0) << "not a whole-file eviction";
    EXPECT_EQ(eviction.len, 0) << "not a whole-file eviction";
    EXPECT_EQ(eviction.fd, eviction.lastSyncFd) << "evicted a file other than the one synced";
    EXPECT_EQ(eviction.syncsBefore, wrapped.fdatasyncCalls) << "a sync came after the eviction";
    EXPECT_EQ(eviction.readsBefore, 0U) << "a read came before the eviction";
    EXPECT_GT(wrapped.readCalls, 0U);
  }
}

/** @test The write benchmarks never evict */
TEST_F(BenchEvictionTest, WriteBenchmarksDoNotEvict) {
  const std::array<NamedBench, 3> BENCHES{{
      {"runSeqWriteBench", &runSeqWriteBench},
      {"runFsyncBench", &runFsyncBench},
      {"runRandWriteBench", &runRandWriteBench},
  }};
  for (const auto& [name, bench] : BENCHES) {
    SCOPED_TRACE(name);
    const BenchResult RESULT = run(bench, makeConfig(4 * KIB, TWO_SYNC_DATA_SIZE, true));
    ASSERT_TRUE(RESULT.success);
    EXPECT_TRUE(wrapped.evictions.empty());
  }
}
