/**
 * @file CpuIsolation_uTest.cpp
 * @brief Unit tests for seeker::cpu isolation API.
 *
 * Notes:
 *  - Parser tests use crafted strings, not actual system files.
 *  - Config query tests verify structure, not specific values (system-dependent).
 */

#include "src/cpu/inc/CpuIsolation.hpp"

#include <gtest/gtest.h>

namespace cpu = seeker::cpu;

/* ----------------------------- parseCpuList Tests ----------------------------- */

/** @test Parse empty string returns empty set. */
TEST(CpuIsolationParseCpuList, EmptyString) {
  const cpu::CpuSet RESULT = cpu::parseCpuList("");
  EXPECT_TRUE(RESULT.empty());
  EXPECT_EQ(RESULT.count(), 0);
}

/** @test Parse nullptr returns empty set. */
TEST(CpuIsolationParseCpuList, Nullptr) {
  const cpu::CpuSet RESULT = cpu::parseCpuList(nullptr);
  EXPECT_TRUE(RESULT.empty());
}

/** @test Parse single CPU. */
TEST(CpuIsolationParseCpuList, SingleCpu) {
  const cpu::CpuSet RESULT = cpu::parseCpuList("3");
  EXPECT_EQ(RESULT.count(), 1);
  EXPECT_TRUE(RESULT.test(3));
  EXPECT_FALSE(RESULT.test(0));
  EXPECT_FALSE(RESULT.test(2));
  EXPECT_FALSE(RESULT.test(4));
}

/** @test Parse CPU range. */
TEST(CpuIsolationParseCpuList, Range) {
  const cpu::CpuSet RESULT = cpu::parseCpuList("2-5");
  EXPECT_EQ(RESULT.count(), 4);
  EXPECT_FALSE(RESULT.test(1));
  EXPECT_TRUE(RESULT.test(2));
  EXPECT_TRUE(RESULT.test(3));
  EXPECT_TRUE(RESULT.test(4));
  EXPECT_TRUE(RESULT.test(5));
  EXPECT_FALSE(RESULT.test(6));
}

/** @test Parse comma-separated list. */
TEST(CpuIsolationParseCpuList, CommaSeparated) {
  const cpu::CpuSet RESULT = cpu::parseCpuList("0,2,4,6");
  EXPECT_EQ(RESULT.count(), 4);
  EXPECT_TRUE(RESULT.test(0));
  EXPECT_FALSE(RESULT.test(1));
  EXPECT_TRUE(RESULT.test(2));
  EXPECT_FALSE(RESULT.test(3));
  EXPECT_TRUE(RESULT.test(4));
  EXPECT_FALSE(RESULT.test(5));
  EXPECT_TRUE(RESULT.test(6));
}

/** @test Parse mixed list with ranges and singles. */
TEST(CpuIsolationParseCpuList, MixedFormat) {
  const cpu::CpuSet RESULT = cpu::parseCpuList("0,2-4,6,8-10");
  EXPECT_EQ(RESULT.count(), 8);
  EXPECT_TRUE(RESULT.test(0));
  EXPECT_FALSE(RESULT.test(1));
  EXPECT_TRUE(RESULT.test(2));
  EXPECT_TRUE(RESULT.test(3));
  EXPECT_TRUE(RESULT.test(4));
  EXPECT_FALSE(RESULT.test(5));
  EXPECT_TRUE(RESULT.test(6));
  EXPECT_FALSE(RESULT.test(7));
  EXPECT_TRUE(RESULT.test(8));
  EXPECT_TRUE(RESULT.test(9));
  EXPECT_TRUE(RESULT.test(10));
}

/** @test Parse handles leading/trailing whitespace. */
TEST(CpuIsolationParseCpuList, Whitespace) {
  const cpu::CpuSet RESULT = cpu::parseCpuList("  1, 3 , 5  ");
  EXPECT_EQ(RESULT.count(), 3);
  EXPECT_TRUE(RESULT.test(1));
  EXPECT_TRUE(RESULT.test(3));
  EXPECT_TRUE(RESULT.test(5));
}

/** @test Parse large CPU numbers within bounds. */
TEST(CpuIsolationParseCpuList, LargeCpuNumbers) {
  const cpu::CpuSet RESULT = cpu::parseCpuList("100,200,500");
  EXPECT_EQ(RESULT.count(), 3);
  EXPECT_TRUE(RESULT.test(100));
  EXPECT_TRUE(RESULT.test(200));
  EXPECT_TRUE(RESULT.test(500));
}

/** @test Parse ignores CPUs beyond MAX_CPUS. */
TEST(CpuIsolationParseCpuList, BeyondMaxCpus) {
  // MAX_CPUS is 1024, so 2000 should be ignored
  const cpu::CpuSet RESULT = cpu::parseCpuList("0,2000");
  EXPECT_EQ(RESULT.count(), 1);
  EXPECT_TRUE(RESULT.test(0));
}

/* ----------------------------- CpuIsolationConfig Methods ----------------------------- */

/** @test isFullyIsolated returns true only when CPU is in all three sets. */
TEST(CpuIsolationConfig, IsFullyIsolated) {
  cpu::CpuIsolationConfig config;
  config.isolcpus.set(2);
  config.isolcpus.set(3);
  config.nohzFull.set(2);
  config.nohzFull.set(4);
  config.rcuNocbs.set(2);
  config.rcuNocbs.set(5);

  // CPU 2 is in all three sets
  EXPECT_TRUE(config.isFullyIsolated(2));

  // Other CPUs missing at least one
  EXPECT_FALSE(config.isFullyIsolated(3)); // Missing nohz_full and rcu_nocbs
  EXPECT_FALSE(config.isFullyIsolated(4)); // Missing isolcpus and rcu_nocbs
  EXPECT_FALSE(config.isFullyIsolated(5)); // Missing isolcpus and nohz_full
  EXPECT_FALSE(config.isFullyIsolated(0)); // Not in any set
}

/** @test hasAnyIsolation detects any configured isolation. */
TEST(CpuIsolationConfig, HasAnyIsolation) {
  cpu::CpuIsolationConfig empty;
  EXPECT_FALSE(empty.hasAnyIsolation());

  cpu::CpuIsolationConfig withIsolcpus;
  withIsolcpus.isolcpus.set(1);
  EXPECT_TRUE(withIsolcpus.hasAnyIsolation());

  cpu::CpuIsolationConfig withNohz;
  withNohz.nohzFull.set(2);
  EXPECT_TRUE(withNohz.hasAnyIsolation());

  cpu::CpuIsolationConfig withRcu;
  withRcu.rcuNocbs.set(3);
  EXPECT_TRUE(withRcu.hasAnyIsolation());
}

/** @test getFullyIsolatedCpus returns intersection of all three sets. */
TEST(CpuIsolationConfig, GetFullyIsolatedCpus) {
  cpu::CpuIsolationConfig config;
  config.isolcpus.set(1);
  config.isolcpus.set(2);
  config.isolcpus.set(3);
  config.nohzFull.set(2);
  config.nohzFull.set(3);
  config.nohzFull.set(4);
  config.rcuNocbs.set(3);
  config.rcuNocbs.set(4);
  config.rcuNocbs.set(5);

  const cpu::CpuSet FULLY = config.getFullyIsolatedCpus();
  EXPECT_EQ(FULLY.count(), 1);
  EXPECT_TRUE(FULLY.test(3));
}

/** @test toString produces non-empty output. */
TEST(CpuIsolationConfig, ToStringNotEmpty) {
  cpu::CpuIsolationConfig config;
  config.isolcpus.set(2);
  config.nohzFull.set(2);
  config.rcuNocbs.set(2);
  config.isolcpusManaged = true;

  const std::string OUTPUT = config.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("isolcpus"), std::string::npos);
  EXPECT_NE(OUTPUT.find("nohz_full"), std::string::npos);
  EXPECT_NE(OUTPUT.find("rcu_nocbs"), std::string::npos);
  EXPECT_NE(OUTPUT.find("managed_irq"), std::string::npos);
}

/* ----------------------------- IsolationValidation ----------------------------- */

/** @test validateIsolation passes when all CPUs are fully isolated. */
TEST(IsolationValidation, AllIsolated) {
  cpu::CpuIsolationConfig config;
  config.isolcpus.set(2);
  config.isolcpus.set(3);
  config.nohzFull.set(2);
  config.nohzFull.set(3);
  config.rcuNocbs.set(2);
  config.rcuNocbs.set(3);

  cpu::CpuSet rtCpus;
  rtCpus.set(2);
  rtCpus.set(3);

  const cpu::IsolationValidation RESULT = cpu::validateIsolation(config, rtCpus);
  EXPECT_TRUE(RESULT.isValid());
  EXPECT_TRUE(RESULT.missingIsolcpus.empty());
  EXPECT_TRUE(RESULT.missingNohzFull.empty());
  EXPECT_TRUE(RESULT.missingRcuNocbs.empty());
}

/** @test validateIsolation detects missing isolation. */
TEST(IsolationValidation, MissingIsolation) {
  cpu::CpuIsolationConfig config;
  config.isolcpus.set(2); // Only CPU 2 in isolcpus
  config.nohzFull.set(2);
  config.nohzFull.set(3); // CPUs 2,3 in nohz_full
  // rcu_nocbs empty

  cpu::CpuSet rtCpus;
  rtCpus.set(2);
  rtCpus.set(3);

  const cpu::IsolationValidation RESULT = cpu::validateIsolation(config, rtCpus);
  EXPECT_FALSE(RESULT.isValid());

  // CPU 3 missing from isolcpus
  EXPECT_EQ(RESULT.missingIsolcpus.count(), 1);
  EXPECT_TRUE(RESULT.missingIsolcpus.test(3));

  // nohz_full is complete
  EXPECT_TRUE(RESULT.missingNohzFull.empty());

  // Both CPUs missing from rcu_nocbs
  EXPECT_EQ(RESULT.missingRcuNocbs.count(), 2);
  EXPECT_TRUE(RESULT.missingRcuNocbs.test(2));
  EXPECT_TRUE(RESULT.missingRcuNocbs.test(3));
}

/** @test IsolationValidation toString reports failures. */
TEST(IsolationValidation, ToStringShowsFailures) {
  cpu::IsolationValidation result;
  result.missingIsolcpus.set(5);
  result.missingNohzFull.set(5);

  const std::string OUTPUT = result.toString();
  EXPECT_NE(OUTPUT.find("FAIL"), std::string::npos);
  EXPECT_NE(OUTPUT.find("isolcpus"), std::string::npos);
  EXPECT_NE(OUTPUT.find("nohz_full"), std::string::npos);
}

/* ----------------------------- getCpuIsolationConfig ----------------------------- */

/** @test getCpuIsolationConfig returns valid structure. */
TEST(GetCpuIsolationConfig, ReturnsValidStruct) {
  // This test verifies the function runs without crashing and returns
  // a valid structure. Actual values are system-dependent.
  const cpu::CpuIsolationConfig CONFIG = cpu::getCpuIsolationConfig();

  // Structure should be valid (counts are within bounds)
  EXPECT_LE(CONFIG.isolcpus.count(), cpu::MAX_CPUS);
  EXPECT_LE(CONFIG.nohzFull.count(), cpu::MAX_CPUS);
  EXPECT_LE(CONFIG.rcuNocbs.count(), cpu::MAX_CPUS);

  // toString should not crash
  const std::string OUTPUT = CONFIG.toString();
  EXPECT_FALSE(OUTPUT.empty());
}

/** @test getCpuIsolationConfig is deterministic (same result on repeated calls). */
TEST(GetCpuIsolationConfig, Deterministic) {
  const cpu::CpuIsolationConfig CONFIG1 = cpu::getCpuIsolationConfig();
  const cpu::CpuIsolationConfig CONFIG2 = cpu::getCpuIsolationConfig();

  // Should produce identical results
  EXPECT_EQ(CONFIG1.isolcpus.count(), CONFIG2.isolcpus.count());
  EXPECT_EQ(CONFIG1.nohzFull.count(), CONFIG2.nohzFull.count());
  EXPECT_EQ(CONFIG1.rcuNocbs.count(), CONFIG2.rcuNocbs.count());
  EXPECT_EQ(CONFIG1.isolcpusManaged, CONFIG2.isolcpusManaged);
  EXPECT_EQ(CONFIG1.nohzFullAll, CONFIG2.nohzFullAll);
}