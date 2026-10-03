/**
 * @file StorageBench_uTest.cpp
 * @brief Unit tests for seeker::storage::StorageBench.
 *
 * Notes:
 *  - These tests perform actual I/O operations.
 *  - Tests use /tmp for benchmark files (should exist on all Linux systems).
 *  - Time-budget tests allow BUDGET_SLACK_SEC past the budget; its comment
 *    records the measurement behind the value.
 *  - Some tests may be slow depending on storage performance.
 */

#include "src/storage/inc/StorageBench.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

using seeker::storage::BENCH_PATH_SIZE;
using seeker::storage::BenchConfig;
using seeker::storage::BenchResult;
using seeker::storage::BenchSuite;
using seeker::storage::DEFAULT_DATA_SIZE;
using seeker::storage::DEFAULT_IO_SIZE;
using seeker::storage::DEFAULT_ITERATIONS;
using seeker::storage::runBenchSuite;
using seeker::storage::runFsyncBench;
using seeker::storage::runRandReadBench;
using seeker::storage::runRandWriteBench;
using seeker::storage::runSeqReadBench;
using seeker::storage::runSeqWriteBench;

namespace {

/// Time a budget-limited run may take past its budget: the one I/O and the
/// one sync of at most 4 MiB that the budget contract allows, plus closing
/// and deleting the file. A 4 MiB fdatasync took at most 195 ms in the dev
/// container's /tmp during parallel builds on the host (432 syncs over nine
/// runs) and 146 ms on an SD card; no single fdatasync of any size took more
/// than 411 ms in the container. The slack covers that worst stall plus one
/// write and the cleanup.
constexpr double BUDGET_SLACK_SEC = 0.5;

/// Budget for the time-budget tests. A setup gets half of it, so a setup that
/// ends in a sync as slow as the worst stall above still leaves the measured
/// phase time to run.
constexpr double TEST_BUDGET_SEC = 1.0;

/// More data than any testbed writes in TEST_BUDGET_SEC (4 KiB writes reach
/// about 3 GB/s even on tmpfs), so the budget, not the size, ends the run.
constexpr std::size_t UNREACHABLE_WRITE_SIZE = 16ULL * 1024 * 1024 * 1024;

/// More data than a setup writes in half of TEST_BUDGET_SEC: about 1.5 GB at
/// the 3 GB/s of 4 KiB writes to tmpfs, far less on a disk syncing every
/// 4 MiB. The budget, not the size, ends every setup.
constexpr std::size_t SETUP_HEAVY_DATA_SIZE = 2ULL * 1024 * 1024 * 1024;

/// More latency iterations than fit in TEST_BUDGET_SEC.
constexpr std::size_t UNREACHABLE_ITERATIONS = 100000000;

/// Budget that stops every setup partway in the cleanup test.
constexpr double CUT_SETUP_BUDGET_SEC = 0.2;

/// Signature shared by the single-benchmark functions.
using BenchFn = BenchResult (*)(const BenchConfig&) noexcept;

/// Benchmark result together with the wall time of the whole call.
struct TimedResult {
  BenchResult result{};
  double wallSec{0.0};
};

/// Run one benchmark and measure the wall time of the call, setup included.
TimedResult runTimed(BenchFn bench, const BenchConfig& config) {
  const auto START = std::chrono::steady_clock::now();
  TimedResult timed{};
  timed.result = bench(config);
  timed.wallSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - START).count();
  return timed;
}

/// Names of the entries in a directory, without "." and "..".
std::vector<std::string> listEntries(const char* dir) {
  std::vector<std::string> names;
  ::DIR* handle = ::opendir(dir);
  if (handle == nullptr) {
    return names;
  }
  while (const ::dirent* entry = ::readdir(handle)) {
    if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0) {
      names.emplace_back(entry->d_name);
    }
  }
  ::closedir(handle);
  return names;
}

/// Private directory under /tmp, removed with anything left in it.
class ScratchDir {
public:
  ScratchDir() {
    std::snprintf(path_.data(), path_.size(), "/tmp/storagebench_utest_XXXXXX");
    valid_ = (::mkdtemp(path_.data()) != nullptr);
  }

  ~ScratchDir() {
    if (!valid_) {
      return;
    }
    for (const std::string& name : listEntries(path_.data())) {
      ::unlink((std::string(path_.data()) + "/" + name).c_str());
    }
    ::rmdir(path_.data());
  }

  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;

  [[nodiscard]] bool valid() const { return valid_; }
  [[nodiscard]] const char* path() const { return path_.data(); }

private:
  std::array<char, 64> path_{};
  bool valid_{false};
};

/// Check if /tmp exists and is writable.
bool tmpIsWritable() {
  struct stat st{};
  if (::stat("/tmp", &st) != 0) {
    return false;
  }
  return S_ISDIR(st.st_mode) && (::access("/tmp", W_OK) == 0);
}

/// Create a small config for quick tests.
BenchConfig makeQuickConfig() {
  BenchConfig config{};
  config.setDirectory("/tmp");
  config.ioSize = 4096;
  config.dataSize = 64 * 1024; // 64 KB for quick tests
  config.iterations = 100;
  config.timeBudgetSec = 5.0;
  config.useDirectIo = false;
  config.useFsync = true;
  return config;
}

} // namespace

/* ----------------------------- BenchConfig Tests ----------------------------- */

/** @test setDirectory copies path correctly. */
TEST(BenchConfigTest, SetDirectory) {
  BenchConfig config{};
  config.setDirectory("/tmp");
  EXPECT_STREQ(config.directory.data(), "/tmp");
}

/** @test setDirectory handles null. */
TEST(BenchConfigTest, SetDirectoryNull) {
  BenchConfig config{};
  config.setDirectory("/tmp");
  config.setDirectory(nullptr);
  EXPECT_EQ(config.directory[0], '\0');
}

/** @test setDirectory handles long paths. */
TEST(BenchConfigTest, SetDirectoryLongPath) {
  BenchConfig config{};

  // Create a path longer than buffer
  std::string longPath(BENCH_PATH_SIZE + 100, 'x');
  config.setDirectory(longPath.c_str());

  // Should be truncated but null-terminated
  EXPECT_LT(std::strlen(config.directory.data()), BENCH_PATH_SIZE);
  EXPECT_EQ(config.directory[BENCH_PATH_SIZE - 1], '\0');
}

/** @test isValid returns true for valid config. */
TEST(BenchConfigTest, IsValidWithValidConfig) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  const BenchConfig CONFIG = makeQuickConfig();
  EXPECT_TRUE(CONFIG.isValid());
}

/** @test isValid returns false for empty directory. */
TEST(BenchConfigTest, IsValidEmptyDirectory) {
  BenchConfig config = makeQuickConfig();
  config.directory[0] = '\0';
  EXPECT_FALSE(config.isValid());
}

/** @test isValid returns false for non-existent directory. */
TEST(BenchConfigTest, IsValidNonExistentDirectory) {
  BenchConfig config = makeQuickConfig();
  config.setDirectory("/nonexistent_dir_xyz_123");
  EXPECT_FALSE(config.isValid());
}

/** @test isValid returns false for invalid I/O size. */
TEST(BenchConfigTest, IsValidInvalidIoSize) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  BenchConfig config = makeQuickConfig();

  config.ioSize = 0;
  EXPECT_FALSE(config.isValid());

  config.ioSize = 100; // Less than 512
  EXPECT_FALSE(config.isValid());

  config.ioSize = 128 * 1024 * 1024; // Too large
  EXPECT_FALSE(config.isValid());
}

/** @test isValid returns false when dataSize < ioSize. */
TEST(BenchConfigTest, IsValidDataSizeTooSmall) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  BenchConfig config = makeQuickConfig();
  config.ioSize = 4096;
  config.dataSize = 1024;
  EXPECT_FALSE(config.isValid());
}

/* ----------------------------- BenchResult Tests ----------------------------- */

/** @test Default BenchResult is failed. */
TEST(BenchResultTest, DefaultFailed) {
  const BenchResult DEFAULT{};
  EXPECT_FALSE(DEFAULT.success);
  EXPECT_EQ(DEFAULT.elapsedSec, 0.0);
  EXPECT_EQ(DEFAULT.operations, 0U);
}

/** @test formatThroughput produces readable output. */
TEST(BenchResultTest, FormatThroughput) {
  BenchResult result{};
  result.throughputBytesPerSec = 100000000.0; // 100 MB/s

  const std::string OUTPUT = result.formatThroughput();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("MB/s"), std::string::npos);
}

/** @test toString for failed result returns FAILED. */
TEST(BenchResultTest, ToStringFailed) {
  const BenchResult FAILED{};
  EXPECT_EQ(FAILED.toString(), "FAILED");
}

/** @test toString for success includes metrics. */
TEST(BenchResultTest, ToStringSuccess) {
  BenchResult result{};
  result.success = true;
  result.elapsedSec = 1.5;
  result.operations = 100;

  const std::string OUTPUT = result.toString();
  EXPECT_NE(OUTPUT.find("100 ops"), std::string::npos);
}

/* ----------------------------- BenchSuite Tests ----------------------------- */

/** @test Default BenchSuite is all failed. */
TEST(BenchSuiteTest, DefaultAllFailed) {
  const BenchSuite DEFAULT{};
  EXPECT_FALSE(DEFAULT.allSuccess());
}

/** @test toString produces output for all benchmarks. */
TEST(BenchSuiteTest, ToStringIncludesAll) {
  BenchSuite suite{};
  suite.seqWrite.success = true;
  suite.seqRead.success = true;
  suite.fsyncLatency.success = true;
  suite.randRead.success = true;
  suite.randWrite.success = true;

  const std::string OUTPUT = suite.toString();
  EXPECT_NE(OUTPUT.find("Seq Write"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Seq Read"), std::string::npos);
  EXPECT_NE(OUTPUT.find("fsync"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Rand Read"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Rand Write"), std::string::npos);
}

/* ----------------------------- Sequential Write Benchmark Tests ----------------------------- */

/** @test runSeqWriteBench fails with invalid config. */
TEST(SeqWriteBenchTest, FailsWithInvalidConfig) {
  BenchConfig config{}; // Invalid: no directory
  const BenchResult RESULT = runSeqWriteBench(config);
  EXPECT_FALSE(RESULT.success);
}

/** @test runSeqWriteBench succeeds with valid config. */
TEST(SeqWriteBenchTest, SucceedsWithValidConfig) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  const BenchConfig CONFIG = makeQuickConfig();
  const BenchResult RESULT = runSeqWriteBench(CONFIG);

  EXPECT_TRUE(RESULT.success);
  EXPECT_GT(RESULT.operations, 0U);
  EXPECT_GT(RESULT.bytesTransferred, 0U);
  EXPECT_GT(RESULT.throughputBytesPerSec, 0.0);
  EXPECT_GT(RESULT.elapsedSec, 0.0);
}

/** @test runSeqWriteBench transfers expected amount of data. */
TEST(SeqWriteBenchTest, TransfersExpectedData) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  BenchConfig config = makeQuickConfig();
  config.dataSize = 32 * 1024; // 32 KB
  config.timeBudgetSec = 30.0; // Generous time

  const BenchResult RESULT = runSeqWriteBench(config);

  EXPECT_TRUE(RESULT.success);
  // Should transfer approximately the requested amount (may be slightly more due to alignment)
  EXPECT_GE(RESULT.bytesTransferred, config.dataSize);
}

/* ----------------------------- Sequential Read Benchmark Tests ----------------------------- */

/** @test runSeqReadBench fails with invalid config. */
TEST(SeqReadBenchTest, FailsWithInvalidConfig) {
  BenchConfig config{};
  const BenchResult RESULT = runSeqReadBench(config);
  EXPECT_FALSE(RESULT.success);
}

/** @test runSeqReadBench succeeds with valid config. */
TEST(SeqReadBenchTest, SucceedsWithValidConfig) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  const BenchConfig CONFIG = makeQuickConfig();
  const BenchResult RESULT = runSeqReadBench(CONFIG);

  EXPECT_TRUE(RESULT.success);
  EXPECT_GT(RESULT.operations, 0U);
  EXPECT_GT(RESULT.bytesTransferred, 0U);
  EXPECT_GT(RESULT.throughputBytesPerSec, 0.0);
}

/* ----------------------------- fsync Benchmark Tests ----------------------------- */

/** @test runFsyncBench fails with invalid config. */
TEST(FsyncBenchTest, FailsWithInvalidConfig) {
  BenchConfig config{};
  const BenchResult RESULT = runFsyncBench(config);
  EXPECT_FALSE(RESULT.success);
}

/** @test runFsyncBench succeeds with valid config. */
TEST(FsyncBenchTest, SucceedsWithValidConfig) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  BenchConfig config = makeQuickConfig();
  config.iterations = 50; // Fewer iterations for fsync (slow)

  const BenchResult RESULT = runFsyncBench(config);

  EXPECT_TRUE(RESULT.success);
  EXPECT_GT(RESULT.operations, 0U);
  EXPECT_GT(RESULT.avgLatencyUs, 0.0);
  EXPECT_GE(RESULT.maxLatencyUs, RESULT.minLatencyUs);
  EXPECT_GE(RESULT.p99LatencyUs, RESULT.avgLatencyUs * 0.1); // P99 should be reasonable
}

/** @test runFsyncBench provides latency statistics. */
TEST(FsyncBenchTest, ProvidesLatencyStats) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  BenchConfig config = makeQuickConfig();
  config.iterations = 100;

  const BenchResult RESULT = runFsyncBench(config);

  if (RESULT.success) {
    // Latency stats should be populated
    EXPECT_GT(RESULT.avgLatencyUs, 0.0);
    EXPECT_GT(RESULT.minLatencyUs, 0.0);
    EXPECT_GT(RESULT.maxLatencyUs, 0.0);
    EXPECT_GT(RESULT.p99LatencyUs, 0.0);

    // Min <= Avg <= Max
    EXPECT_LE(RESULT.minLatencyUs, RESULT.avgLatencyUs);
    EXPECT_LE(RESULT.avgLatencyUs, RESULT.maxLatencyUs);
  }
}

/* ----------------------------- Random Read Benchmark Tests ----------------------------- */

/** @test runRandReadBench fails with invalid config. */
TEST(RandReadBenchTest, FailsWithInvalidConfig) {
  BenchConfig config{};
  const BenchResult RESULT = runRandReadBench(config);
  EXPECT_FALSE(RESULT.success);
}

/** @test runRandReadBench succeeds with valid config. */
TEST(RandReadBenchTest, SucceedsWithValidConfig) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  const BenchConfig CONFIG = makeQuickConfig();
  const BenchResult RESULT = runRandReadBench(CONFIG);

  EXPECT_TRUE(RESULT.success);
  EXPECT_GT(RESULT.operations, 0U);
  EXPECT_GT(RESULT.avgLatencyUs, 0.0);
}

/* ----------------------------- Random Write Benchmark Tests ----------------------------- */

/** @test runRandWriteBench fails with invalid config. */
TEST(RandWriteBenchTest, FailsWithInvalidConfig) {
  BenchConfig config{};
  const BenchResult RESULT = runRandWriteBench(config);
  EXPECT_FALSE(RESULT.success);
}

/** @test runRandWriteBench succeeds with valid config. */
TEST(RandWriteBenchTest, SucceedsWithValidConfig) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  BenchConfig config = makeQuickConfig();
  config.iterations = 50; // Fewer iterations (random write + sync is slow)

  const BenchResult RESULT = runRandWriteBench(config);

  EXPECT_TRUE(RESULT.success);
  EXPECT_GT(RESULT.operations, 0U);
  EXPECT_GT(RESULT.avgLatencyUs, 0.0);
}

/* ----------------------------- Benchmark Suite Tests ----------------------------- */

/** @test runBenchSuite runs all benchmarks. */
TEST(BenchSuiteRunTest, RunsAllBenchmarks) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  BenchConfig config = makeQuickConfig();
  config.dataSize = 32 * 1024; // Very small for quick test
  config.iterations = 20;

  const BenchSuite SUITE = runBenchSuite(config);

  // All benchmarks should have been attempted
  // (they may fail on some systems, but should produce results)
  EXPECT_TRUE(SUITE.seqWrite.success || SUITE.seqWrite.elapsedSec == 0.0);
  EXPECT_TRUE(SUITE.seqRead.success || SUITE.seqRead.elapsedSec == 0.0);
}

/* ----------------------------- Time Budget Tests ----------------------------- */

/** @test Sequential write with fsync returns within its budget, syncs included. */
TEST(TimeBudgetTest, RespectsTimeBudget) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  BenchConfig config = makeQuickConfig();
  config.dataSize = UNREACHABLE_WRITE_SIZE;
  config.timeBudgetSec = TEST_BUDGET_SEC;

  const TimedResult TIMED = runTimed(&runSeqWriteBench, config);

  EXPECT_TRUE(TIMED.result.success);
  EXPECT_GE(TIMED.result.operations, 1U);
  EXPECT_LT(TIMED.result.bytesTransferred, config.dataSize) << "budget did not end the run";
  EXPECT_LE(TIMED.wallSec, config.timeBudgetSec + BUDGET_SLACK_SEC);
  EXPECT_LE(TIMED.result.elapsedSec, TIMED.wallSec);
}

/** @test Sequential read counts its setup against the budget and reads what setup wrote. */
TEST(TimeBudgetTest, SeqReadSetupWithinBudget) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  BenchConfig config = makeQuickConfig();
  config.dataSize = SETUP_HEAVY_DATA_SIZE;
  config.timeBudgetSec = TEST_BUDGET_SEC;

  const TimedResult TIMED = runTimed(&runSeqReadBench, config);

  EXPECT_TRUE(TIMED.result.success);
  EXPECT_GE(TIMED.result.operations, 1U);
  EXPECT_LT(TIMED.result.bytesTransferred, config.dataSize) << "budget did not cut the setup";
  EXPECT_LE(TIMED.wallSec, config.timeBudgetSec + BUDGET_SLACK_SEC);
}

/** @test Random read and write count their setup against the budget. */
TEST(TimeBudgetTest, RandomSetupWithinBudget) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  BenchConfig config = makeQuickConfig();
  config.dataSize = SETUP_HEAVY_DATA_SIZE;
  config.iterations = UNREACHABLE_ITERATIONS;
  config.timeBudgetSec = TEST_BUDGET_SEC;

  const std::array<std::pair<const char*, BenchFn>, 2> BENCHES{{
      {"runRandReadBench", &runRandReadBench},
      {"runRandWriteBench", &runRandWriteBench},
  }};
  for (const auto& [name, bench] : BENCHES) {
    SCOPED_TRACE(name);
    const TimedResult TIMED = runTimed(bench, config);

    EXPECT_TRUE(TIMED.result.success);
    EXPECT_GE(TIMED.result.operations, 1U);
    EXPECT_LT(TIMED.result.operations, config.iterations) << "budget did not end the run";
    EXPECT_LE(TIMED.wallSec, config.timeBudgetSec + BUDGET_SLACK_SEC);
  }
}

/* ----------------------------- Cleanup Tests ----------------------------- */

/** @test No benchmark leaves a file behind when the budget cuts its setup short. */
TEST(CleanupTest, NoTempFileAfterBudgetCut) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  const ScratchDir SCRATCH;
  ASSERT_TRUE(SCRATCH.valid());

  BenchConfig config = makeQuickConfig();
  config.setDirectory(SCRATCH.path());
  config.dataSize = SETUP_HEAVY_DATA_SIZE;

  // One budget stops each setup partway and the benchmark runs on what was
  // written; a zero budget leaves no setup time, so the random benchmarks take
  // their too-few-blocks exit
  for (const double BUDGET_SEC : {CUT_SETUP_BUDGET_SEC, 0.0}) {
    SCOPED_TRACE(::testing::Message() << "timeBudgetSec=" << BUDGET_SEC);
    config.timeBudgetSec = BUDGET_SEC;

    (void)runBenchSuite(config);
    const std::vector<std::string> LEFT = listEntries(SCRATCH.path());

    EXPECT_TRUE(LEFT.empty()) << "left behind: " << (LEFT.empty() ? "" : LEFT.front());
  }
}

/* ----------------------------- Edge Case Tests ----------------------------- */

/** @test Handles very small data size. */
TEST(EdgeCaseTest, VerySmallDataSize) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  BenchConfig config = makeQuickConfig();
  config.dataSize = config.ioSize; // Minimum valid
  config.iterations = 10;

  const BenchResult RESULT = runSeqWriteBench(config);
  EXPECT_TRUE(RESULT.success);
  EXPECT_GE(RESULT.operations, 1U);
}

/** @test Handles single iteration. */
TEST(EdgeCaseTest, SingleIteration) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  BenchConfig config = makeQuickConfig();
  config.iterations = 1;

  const BenchResult RESULT = runFsyncBench(config);
  EXPECT_TRUE(RESULT.success);
  EXPECT_EQ(RESULT.operations, 1U);
}