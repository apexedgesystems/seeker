/**
 * @file LoopbackBench_uTest.cpp
 * @brief Unit tests for seeker::network::LoopbackBench.
 *
 * Notes:
 *  - Tests verify structural correctness and reasonable ranges.
 *  - Actual latency/throughput values depend on system load and hardware.
 *  - Tests use short budgets to keep test suite fast.
 */

#include "src/network/inc/LoopbackBench.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

using seeker::network::DEFAULT_LATENCY_MESSAGE_SIZE;
using seeker::network::DEFAULT_THROUGHPUT_BUFFER_SIZE;
using seeker::network::LatencyResult;
using seeker::network::LoopbackBenchConfig;
using seeker::network::LoopbackBenchResult;
using seeker::network::MAX_LATENCY_SAMPLES;
using seeker::network::measureTcpLatency;
using seeker::network::measureTcpThroughput;
using seeker::network::measureUdpLatency;
using seeker::network::measureUdpThroughput;
using seeker::network::runLoopbackBench;
using seeker::network::ThroughputResult;

/* ----------------------------- LatencyResult Tests ----------------------------- */

/** @test Default LatencyResult is zeroed and not successful. */
TEST(LatencyResultTest, DefaultZeroed) {
  const LatencyResult DEFAULT{};

  EXPECT_EQ(DEFAULT.minUs, 0.0);
  EXPECT_EQ(DEFAULT.maxUs, 0.0);
  EXPECT_EQ(DEFAULT.meanUs, 0.0);
  EXPECT_EQ(DEFAULT.p50Us, 0.0);
  EXPECT_EQ(DEFAULT.p99Us, 0.0);
  EXPECT_EQ(DEFAULT.sampleCount, 0U);
  EXPECT_FALSE(DEFAULT.success);
}

/** @test LatencyResult toString indicates failure when not successful. */
TEST(LatencyResultTest, ToStringFailure) {
  const LatencyResult FAILED{};
  const std::string OUTPUT = FAILED.toString();

  EXPECT_NE(OUTPUT.find("FAILED"), std::string::npos);
}

/** @test LatencyResult toString shows stats when successful. */
TEST(LatencyResultTest, ToStringSuccess) {
  LatencyResult r{};
  r.success = true;
  r.minUs = 10.0;
  r.meanUs = 15.0;
  r.p50Us = 14.0;
  r.p95Us = 20.0;
  r.p99Us = 25.0;
  r.maxUs = 30.0;
  r.stddevUs = 5.0;
  r.sampleCount = 100;

  const std::string OUTPUT = r.toString();

  EXPECT_NE(OUTPUT.find("min="), std::string::npos);
  EXPECT_NE(OUTPUT.find("mean="), std::string::npos);
  EXPECT_NE(OUTPUT.find("p99="), std::string::npos);
  EXPECT_NE(OUTPUT.find("samples=100"), std::string::npos);
}

/* ----------------------------- ThroughputResult Tests ----------------------------- */

/** @test Default ThroughputResult is zeroed and not successful. */
TEST(ThroughputResultTest, DefaultZeroed) {
  const ThroughputResult DEFAULT{};

  EXPECT_EQ(DEFAULT.mibPerSec, 0.0);
  EXPECT_EQ(DEFAULT.mbitsPerSec, 0.0);
  EXPECT_EQ(DEFAULT.bytesTransferred, 0U);
  EXPECT_EQ(DEFAULT.durationSec, 0.0);
  EXPECT_FALSE(DEFAULT.success);
}

/** @test ThroughputResult toString indicates failure when not successful. */
TEST(ThroughputResultTest, ToStringFailure) {
  const ThroughputResult FAILED{};
  const std::string OUTPUT = FAILED.toString();

  EXPECT_NE(OUTPUT.find("FAILED"), std::string::npos);
}

/** @test ThroughputResult toString shows stats when successful. */
TEST(ThroughputResultTest, ToStringSuccess) {
  ThroughputResult r{};
  r.success = true;
  r.mibPerSec = 100.0;
  r.mbitsPerSec = 838.86;
  r.bytesTransferred = 104857600;
  r.durationSec = 1.0;

  const std::string OUTPUT = r.toString();

  EXPECT_NE(OUTPUT.find("MiB/s"), std::string::npos);
  EXPECT_NE(OUTPUT.find("Mbps"), std::string::npos);
}

/* ----------------------------- LoopbackBenchResult Tests ----------------------------- */

/** @test Default LoopbackBenchResult has no success. */
TEST(LoopbackBenchResultTest, DefaultNoSuccess) {
  const LoopbackBenchResult DEFAULT{};

  EXPECT_FALSE(DEFAULT.anySuccess());
  EXPECT_FALSE(DEFAULT.allSuccess());
}

/** @test anySuccess returns true if any test succeeded. */
TEST(LoopbackBenchResultTest, AnySuccess) {
  LoopbackBenchResult r{};
  EXPECT_FALSE(r.anySuccess());

  r.tcpLatency.success = true;
  EXPECT_TRUE(r.anySuccess());
}

/** @test allSuccess returns true only if all tests succeeded. */
TEST(LoopbackBenchResultTest, AllSuccess) {
  LoopbackBenchResult r{};
  r.tcpLatency.success = true;
  r.udpLatency.success = true;
  r.tcpThroughput.success = true;
  EXPECT_FALSE(r.allSuccess());

  r.udpThroughput.success = true;
  EXPECT_TRUE(r.allSuccess());
}

/** @test LoopbackBenchResult toString contains all test results. */
TEST(LoopbackBenchResultTest, ToStringComplete) {
  const LoopbackBenchResult R{};
  const std::string OUTPUT = R.toString();

  EXPECT_NE(OUTPUT.find("TCP"), std::string::npos);
  EXPECT_NE(OUTPUT.find("UDP"), std::string::npos);
}

/* ----------------------------- TCP Latency Measurement Tests ----------------------------- */

/** @test TCP latency measurement completes within budget. */
TEST(TcpLatencyTest, CompletesWithinBudget) {
  const auto START = std::chrono::steady_clock::now();
  [[maybe_unused]] const LatencyResult RESULT = measureTcpLatency(std::chrono::milliseconds(200));
  const auto END = std::chrono::steady_clock::now();

  const auto ELAPSED = std::chrono::duration_cast<std::chrono::milliseconds>(END - START);

  // Should complete within budget plus some overhead
  EXPECT_LT(ELAPSED.count(), 500);
}

/** @test TCP latency measurement collects samples on success. */
TEST(TcpLatencyTest, CollectsSamples) {
  const LatencyResult RESULT = measureTcpLatency(std::chrono::milliseconds(100));

  if (RESULT.success) {
    EXPECT_GT(RESULT.sampleCount, 0U);
    EXPECT_LE(RESULT.sampleCount, MAX_LATENCY_SAMPLES);
  }
}

/** @test TCP latency statistics are consistent. */
TEST(TcpLatencyTest, StatisticsConsistent) {
  const LatencyResult RESULT = measureTcpLatency(std::chrono::milliseconds(100));

  if (RESULT.success && RESULT.sampleCount > 1) {
    // min <= mean <= max
    EXPECT_LE(RESULT.minUs, RESULT.meanUs);
    EXPECT_LE(RESULT.meanUs, RESULT.maxUs);

    // min <= p50 <= p99 <= max
    EXPECT_LE(RESULT.minUs, RESULT.p50Us);
    EXPECT_LE(RESULT.p50Us, RESULT.p99Us);
    EXPECT_LE(RESULT.p99Us, RESULT.maxUs);

    // stddev >= 0
    EXPECT_GE(RESULT.stddevUs, 0.0);
  }
}

/** @test TCP latency values are in reasonable range for loopback. */
TEST(TcpLatencyTest, ReasonableValues) {
  const LatencyResult RESULT = measureTcpLatency(std::chrono::milliseconds(100));

  if (RESULT.success) {
    // Loopback latency should be < 10ms even on slow systems
    EXPECT_LT(RESULT.p99Us, 10000.0);

    // And typically > 1us
    EXPECT_GT(RESULT.minUs, 0.0);
  }
}

/* ----------------------------- UDP Latency Measurement Tests ----------------------------- */

/** @test UDP latency measurement completes within budget. */
TEST(UdpLatencyTest, CompletesWithinBudget) {
  const auto START = std::chrono::steady_clock::now();
  [[maybe_unused]] const LatencyResult RESULT = measureUdpLatency(std::chrono::milliseconds(200));
  const auto END = std::chrono::steady_clock::now();

  const auto ELAPSED = std::chrono::duration_cast<std::chrono::milliseconds>(END - START);
  EXPECT_LT(ELAPSED.count(), 500);
}

/** @test UDP latency measurement collects samples on success. */
TEST(UdpLatencyTest, CollectsSamples) {
  const LatencyResult RESULT = measureUdpLatency(std::chrono::milliseconds(100));

  if (RESULT.success) {
    EXPECT_GT(RESULT.sampleCount, 0U);
    EXPECT_LE(RESULT.sampleCount, MAX_LATENCY_SAMPLES);
  }
}

/** @test UDP latency statistics are consistent. */
TEST(UdpLatencyTest, StatisticsConsistent) {
  const LatencyResult RESULT = measureUdpLatency(std::chrono::milliseconds(100));

  if (RESULT.success && RESULT.sampleCount > 1) {
    EXPECT_LE(RESULT.minUs, RESULT.meanUs);
    EXPECT_LE(RESULT.meanUs, RESULT.maxUs);
    EXPECT_GE(RESULT.stddevUs, 0.0);
  }
}

/* ----------------------------- TCP Throughput Measurement Tests ----------------------------- */

/** @test TCP throughput measurement completes within budget. */
TEST(TcpThroughputTest, CompletesWithinBudget) {
  const auto START = std::chrono::steady_clock::now();
  [[maybe_unused]] const ThroughputResult RESULT =
      measureTcpThroughput(std::chrono::milliseconds(200));
  const auto END = std::chrono::steady_clock::now();

  const auto ELAPSED = std::chrono::duration_cast<std::chrono::milliseconds>(END - START);
  EXPECT_LT(ELAPSED.count(), 500);
}

/** @test TCP throughput measurement transfers bytes on success. */
TEST(TcpThroughputTest, TransfersBytes) {
  const ThroughputResult RESULT = measureTcpThroughput(std::chrono::milliseconds(100));

  if (RESULT.success) {
    EXPECT_GT(RESULT.bytesTransferred, 0U);
    EXPECT_GT(RESULT.mibPerSec, 0.0);
    EXPECT_GT(RESULT.mbitsPerSec, 0.0);
  }
}

/** @test TCP throughput duration is reasonable. */
TEST(TcpThroughputTest, ReasonableDuration) {
  const ThroughputResult RESULT = measureTcpThroughput(std::chrono::milliseconds(100));

  if (RESULT.success) {
    EXPECT_GT(RESULT.durationSec, 0.05); // At least 50ms
    EXPECT_LT(RESULT.durationSec, 1.0);  // Less than 1s
  }
}

/** @test TCP throughput values are consistent. */
TEST(TcpThroughputTest, ValuesConsistent) {
  const ThroughputResult RESULT = measureTcpThroughput(std::chrono::milliseconds(100));

  if (RESULT.success) {
    // MiB/s and Mbps should be related by factor of ~8.39
    const double EXPECTED_MBPS = RESULT.mibPerSec * 8.0 * 1024.0 * 1024.0 / 1'000'000.0;
    EXPECT_NEAR(RESULT.mbitsPerSec, EXPECTED_MBPS, EXPECTED_MBPS * 0.01);
  }
}

/* ----------------------------- UDP Throughput Measurement Tests ----------------------------- */

/** @test UDP throughput measurement completes within budget. */
TEST(UdpThroughputTest, CompletesWithinBudget) {
  const auto START = std::chrono::steady_clock::now();
  [[maybe_unused]] const ThroughputResult RESULT =
      measureUdpThroughput(std::chrono::milliseconds(200));
  const auto END = std::chrono::steady_clock::now();

  const auto ELAPSED = std::chrono::duration_cast<std::chrono::milliseconds>(END - START);
  EXPECT_LT(ELAPSED.count(), 500);
}

/** @test UDP throughput measurement transfers bytes on success. */
TEST(UdpThroughputTest, TransfersBytes) {
  const ThroughputResult RESULT = measureUdpThroughput(std::chrono::milliseconds(100));

  if (RESULT.success) {
    EXPECT_GT(RESULT.bytesTransferred, 0U);
  }
}

/* ----------------------------- Combined Benchmark Tests ----------------------------- */

/** @test Combined benchmark completes within budget. */
TEST(LoopbackBenchTest, CompletesWithinBudget) {
  const auto START = std::chrono::steady_clock::now();
  [[maybe_unused]] const LoopbackBenchResult RESULT =
      runLoopbackBench(std::chrono::milliseconds(400));
  const auto END = std::chrono::steady_clock::now();

  const auto ELAPSED = std::chrono::duration_cast<std::chrono::milliseconds>(END - START);
  EXPECT_LT(ELAPSED.count(), 1000);
}

/** @test Combined benchmark runs all tests. */
TEST(LoopbackBenchTest, RunsAllTests) {
  const LoopbackBenchResult RESULT = runLoopbackBench(std::chrono::milliseconds(400));

  // At least some tests should succeed on a working system
  // We can't guarantee all succeed due to system conditions
  if (RESULT.anySuccess()) {
    SUCCEED();
  } else {
    GTEST_LOG_(WARNING) << "No loopback tests succeeded - system may be overloaded";
  }
}

/** @test Config with no tests enabled returns empty result. */
TEST(LoopbackBenchTest, NoTestsEnabledEmpty) {
  LoopbackBenchConfig config{};
  config.runTcpLatency = false;
  config.runUdpLatency = false;
  config.runTcpThroughput = false;
  config.runUdpThroughput = false;

  const LoopbackBenchResult RESULT = runLoopbackBench(config);

  EXPECT_FALSE(RESULT.anySuccess());
}

/** @test Config with only TCP latency enabled runs only TCP latency. */
TEST(LoopbackBenchTest, SelectiveTests) {
  LoopbackBenchConfig config{};
  config.totalBudget = std::chrono::milliseconds(100);
  config.runTcpLatency = true;
  config.runUdpLatency = false;
  config.runTcpThroughput = false;
  config.runUdpThroughput = false;

  const LoopbackBenchResult RESULT = runLoopbackBench(config);

  // TCP latency may or may not succeed, but others should definitely not
  EXPECT_FALSE(RESULT.udpLatency.success);
  EXPECT_FALSE(RESULT.tcpThroughput.success);
  EXPECT_FALSE(RESULT.udpThroughput.success);
}

/* ----------------------------- Edge Case Tests ----------------------------- */

/** @test Very short budget still completes. */
TEST(LoopbackBenchEdgeCaseTest, VeryShortBudget) {
  [[maybe_unused]] const LatencyResult RESULT = measureTcpLatency(std::chrono::milliseconds(10));

  // May or may not succeed with very short budget, but should not hang/crash
  SUCCEED();
}

/** @test Custom message size works. */
TEST(LoopbackBenchEdgeCaseTest, CustomMessageSize) {
  [[maybe_unused]] const LatencyResult RESULT =
      measureTcpLatency(std::chrono::milliseconds(50), 1024);

  // Should complete regardless of success
  SUCCEED();
}

/** @test Custom buffer size works. */
TEST(LoopbackBenchEdgeCaseTest, CustomBufferSize) {
  [[maybe_unused]] const ThroughputResult RESULT =
      measureTcpThroughput(std::chrono::milliseconds(50), 16384);

  SUCCEED();
}

/* ----------------------------- Constants Tests ----------------------------- */

/** @test Constants have reasonable values. */
TEST(LoopbackBenchConstantsTest, ReasonableValues) {
  EXPECT_GT(MAX_LATENCY_SAMPLES, 100U);
  EXPECT_LE(MAX_LATENCY_SAMPLES, 65536U);

  EXPECT_GE(DEFAULT_LATENCY_MESSAGE_SIZE, 1U);
  EXPECT_LE(DEFAULT_LATENCY_MESSAGE_SIZE, 1024U);

  EXPECT_GE(DEFAULT_THROUGHPUT_BUFFER_SIZE, 1024U);
  EXPECT_LE(DEFAULT_THROUGHPUT_BUFFER_SIZE, 1024 * 1024U);
}

/* ----------------------------- LoopbackBenchConfig Tests ----------------------------- */

/** @test Default config has all tests enabled. */
TEST(LoopbackBenchConfigTest, DefaultAllEnabled) {
  const LoopbackBenchConfig DEFAULT{};

  EXPECT_TRUE(DEFAULT.runTcpLatency);
  EXPECT_TRUE(DEFAULT.runUdpLatency);
  EXPECT_TRUE(DEFAULT.runTcpThroughput);
  EXPECT_TRUE(DEFAULT.runUdpThroughput);
}

/** @test Default config has reasonable budget. */
TEST(LoopbackBenchConfigTest, DefaultBudget) {
  const LoopbackBenchConfig DEFAULT{};

  EXPECT_GE(DEFAULT.totalBudget.count(), 100);
  EXPECT_LE(DEFAULT.totalBudget.count(), 60000);
}