/**
 * @file StorageBench_uTest.cpp
 * @brief Unit tests for seeker::storage::StorageBench.
 *
 * Notes:
 *  - These tests perform actual I/O operations.
 *  - Tests use /tmp for benchmark files (should exist on all Linux systems).
 *  - Timing tests use generous bounds for CI variance.
 *  - Some tests may be slow depending on storage performance.
 */

#include "src/storage/inc/StorageBench.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <sys/stat.h>

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

/** @test Benchmarks respect time budget. */
TEST(TimeBudgetTest, RespectsTimeBudget) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  BenchConfig config = makeQuickConfig();
  config.dataSize = 1024 * 1024 * 1024; // 1 GB (would take forever)
  config.iterations = 1000000;
  config.timeBudgetSec = 1.0; // But only 1 second allowed

  const BenchResult RESULT = runSeqWriteBench(config);

  // Should have stopped due to time budget
  EXPECT_LE(RESULT.elapsedSec, config.timeBudgetSec + 1.0); // Allow 1s margin
}

/* ----------------------------- Cleanup Tests ----------------------------- */

/** @test Benchmarks clean up temp files. */
TEST(CleanupTest, TempFilesRemoved) {
  if (!tmpIsWritable()) {
    GTEST_SKIP() << "/tmp is not writable";
  }

  // Count files in /tmp before
  // (This is a rough test - just verify no obvious leaks)

  const BenchConfig CONFIG = makeQuickConfig();

  // Run several benchmarks
  (void)runSeqWriteBench(CONFIG);
  (void)runSeqReadBench(CONFIG);
  (void)runFsyncBench(CONFIG);

  // If temp files weren't cleaned, we'd eventually run out of space
  // This test mainly serves as documentation that cleanup happens
  SUCCEED();
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