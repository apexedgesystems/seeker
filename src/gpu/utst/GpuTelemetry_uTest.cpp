/**
 * @file GpuTelemetry_uTest.cpp
 * @brief Unit tests for seeker::gpu::GpuTelemetry.
 *
 * Notes:
 *  - Tests verify structural invariants, not specific hardware values.
 *  - Tests pass even when no GPU is present (graceful degradation).
 */

#include "src/gpu/inc/GpuTelemetry.hpp"

#include <gtest/gtest.h>

using seeker::gpu::getAllGpuTelemetry;
using seeker::gpu::getGpuTelemetry;
using seeker::gpu::GpuTelemetry;
using seeker::gpu::ThrottleReasons;

/* ----------------------------- ThrottleReasons Tests ----------------------------- */

/** @test Default ThrottleReasons has no throttling. */
TEST(ThrottleReasonsTest, DefaultNoThrottling) {
  ThrottleReasons reasons{};
  EXPECT_FALSE(reasons.isThrottling());
}

/** @test Default has no thermal throttling. */
TEST(ThrottleReasonsTest, DefaultNoThermal) {
  ThrottleReasons reasons{};
  EXPECT_FALSE(reasons.isThermalThrottling());
}

/** @test Default has no power throttling. */
TEST(ThrottleReasonsTest, DefaultNoPower) {
  ThrottleReasons reasons{};
  EXPECT_FALSE(reasons.isPowerThrottling());
}

/** @test swThermal triggers thermal throttling. */
TEST(ThrottleReasonsTest, SwThermalDetected) {
  ThrottleReasons reasons{};
  reasons.swThermal = true;
  EXPECT_TRUE(reasons.isThermalThrottling());
  EXPECT_TRUE(reasons.isThrottling());
}

/** @test hwThermal triggers thermal throttling. */
TEST(ThrottleReasonsTest, HwThermalDetected) {
  ThrottleReasons reasons{};
  reasons.hwThermal = true;
  EXPECT_TRUE(reasons.isThermalThrottling());
  EXPECT_TRUE(reasons.isThrottling());
}

/** @test swPowerCap triggers power throttling. */
TEST(ThrottleReasonsTest, SwPowerDetected) {
  ThrottleReasons reasons{};
  reasons.swPowerCap = true;
  EXPECT_TRUE(reasons.isPowerThrottling());
  EXPECT_TRUE(reasons.isThrottling());
}

/** @test hwPowerBrake triggers power throttling. */
TEST(ThrottleReasonsTest, HwPowerDetected) {
  ThrottleReasons reasons{};
  reasons.hwPowerBrake = true;
  EXPECT_TRUE(reasons.isPowerThrottling());
  EXPECT_TRUE(reasons.isThrottling());
}

/** @test gpuIdle does not trigger isThrottling. */
TEST(ThrottleReasonsTest, IdleNotThrottling) {
  ThrottleReasons reasons{};
  reasons.gpuIdle = true;
  EXPECT_FALSE(reasons.isThrottling());
}

/** @test ThrottleReasons::toString returns "none" for default. */
TEST(ThrottleReasonsTest, DefaultToStringNone) {
  ThrottleReasons reasons{};
  EXPECT_EQ(reasons.toString(), "none");
}

/** @test ThrottleReasons::toString lists active reasons. */
TEST(ThrottleReasonsTest, ToStringListsReasons) {
  ThrottleReasons reasons{};
  reasons.swThermal = true;
  reasons.swPowerCap = true;
  std::string str = reasons.toString();
  EXPECT_NE(str.find("thermal"), std::string::npos);
  EXPECT_NE(str.find("power"), std::string::npos);
}

/* ----------------------------- GpuTelemetry Tests ----------------------------- */

/** @test Default GpuTelemetry has deviceIndex -1. */
TEST(GpuTelemetryTest, DefaultDeviceIndex) {
  GpuTelemetry telem{};
  EXPECT_EQ(telem.deviceIndex, -1);
}

/** @test Default has zero temperature. */
TEST(GpuTelemetryTest, DefaultTemperature) {
  GpuTelemetry telem{};
  EXPECT_EQ(telem.temperatureC, 0);
}

/** @test Default has zero power. */
TEST(GpuTelemetryTest, DefaultPower) {
  GpuTelemetry telem{};
  EXPECT_EQ(telem.powerMilliwatts, 0);
}

/** @test Default has zero clocks. */
TEST(GpuTelemetryTest, DefaultClocks) {
  GpuTelemetry telem{};
  EXPECT_EQ(telem.smClockMHz, 0);
  EXPECT_EQ(telem.memClockMHz, 0);
}

/** @test Default perfState is 0. */
TEST(GpuTelemetryTest, DefaultPerfState) {
  GpuTelemetry telem{};
  EXPECT_EQ(telem.perfState, 0);
}

/** @test isMaxPerformance returns true for P0. */
TEST(GpuTelemetryTest, P0IsMaxPerformance) {
  GpuTelemetry telem{};
  telem.perfState = 0;
  EXPECT_TRUE(telem.isMaxPerformance());
}

/** @test isMaxPerformance returns false for P1+. */
TEST(GpuTelemetryTest, P1NotMaxPerformance) {
  GpuTelemetry telem{};
  telem.perfState = 1;
  EXPECT_FALSE(telem.isMaxPerformance());
}

/** @test isThrottling reflects throttleReasons. */
TEST(GpuTelemetryTest, IsThrottlingReflectsReasons) {
  GpuTelemetry telem{};
  EXPECT_FALSE(telem.isThrottling());
  telem.throttleReasons.hwThermal = true;
  EXPECT_TRUE(telem.isThrottling());
}

/** @test Default fan speed is -1 (unavailable). */
TEST(GpuTelemetryTest, DefaultFanUnavailable) {
  GpuTelemetry telem{};
  EXPECT_EQ(telem.fanSpeedPercent, -1);
}

/** @test GpuTelemetry::toString not empty. */
TEST(GpuTelemetryTest, ToStringNotEmpty) {
  GpuTelemetry telem{};
  EXPECT_FALSE(telem.toString().empty());
}

/* ----------------------------- API Tests ----------------------------- */

/** @test getGpuTelemetry returns default for invalid index. */
TEST(GpuTelemetryApiTest, InvalidIndexReturnsDefault) {
  GpuTelemetry telem = getGpuTelemetry(-1);
  EXPECT_EQ(telem.deviceIndex, -1);
}

/** @test getAllGpuTelemetry returns vector. */
TEST(GpuTelemetryApiTest, GetAllReturnsVector) {
  std::vector<GpuTelemetry> all = getAllGpuTelemetry();
  EXPECT_GE(all.size(), 0);
}

/** @test getGpuTelemetry is deterministic for invalid index. */
TEST(GpuTelemetryApiTest, DeterministicInvalid) {
  GpuTelemetry t1 = getGpuTelemetry(-1);
  GpuTelemetry t2 = getGpuTelemetry(-1);
  EXPECT_EQ(t1.deviceIndex, t2.deviceIndex);
}
