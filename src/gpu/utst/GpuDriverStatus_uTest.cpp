/**
 * @file GpuDriverStatus_uTest.cpp
 * @brief Unit tests for seeker::gpu::GpuDriverStatus.
 *
 * Notes:
 *  - Tests verify structural invariants, not specific hardware values.
 *  - Tests pass even when no GPU is present (graceful degradation).
 */

#include "src/gpu/inc/GpuDriverStatus.hpp"

#include <gtest/gtest.h>

using seeker::gpu::ComputeMode;
using seeker::gpu::getAllGpuDriverStatus;
using seeker::gpu::getGpuDriverStatus;
using seeker::gpu::getSystemGpuDriverInfo;
using seeker::gpu::GpuDriverStatus;
using seeker::gpu::toString;

/* ----------------------------- ComputeMode Tests ----------------------------- */

/** @test ComputeMode::Default has string representation. */
TEST(ComputeModeTest, DefaultToString) {
  EXPECT_NE(toString(ComputeMode::Default), nullptr);
  EXPECT_STRNE(toString(ComputeMode::Default), "");
}

/** @test ComputeMode::ExclusiveThread has string representation. */
TEST(ComputeModeTest, ExclusiveThreadToString) {
  EXPECT_NE(toString(ComputeMode::ExclusiveThread), nullptr);
}

/** @test ComputeMode::Prohibited has string representation. */
TEST(ComputeModeTest, ProhibitedToString) { EXPECT_NE(toString(ComputeMode::Prohibited), nullptr); }

/** @test ComputeMode::ExclusiveProcess has string representation. */
TEST(ComputeModeTest, ExclusiveProcessToString) {
  EXPECT_NE(toString(ComputeMode::ExclusiveProcess), nullptr);
}

/* ----------------------------- GpuDriverStatus Tests ----------------------------- */

/** @test Default GpuDriverStatus has deviceIndex -1. */
TEST(GpuDriverStatusTest, DefaultDeviceIndex) {
  GpuDriverStatus status{};
  EXPECT_EQ(status.deviceIndex, -1);
}

/** @test Default has empty driver version. */
TEST(GpuDriverStatusTest, DefaultDriverVersion) {
  GpuDriverStatus status{};
  EXPECT_TRUE(status.driverVersion.empty());
}

/** @test Default CUDA versions are zero. */
TEST(GpuDriverStatusTest, DefaultCudaVersions) {
  GpuDriverStatus status{};
  EXPECT_EQ(status.cudaDriverVersion, 0);
  EXPECT_EQ(status.cudaRuntimeVersion, 0);
}

/** @test Default persistence mode is off. */
TEST(GpuDriverStatusTest, DefaultPersistenceOff) {
  GpuDriverStatus status{};
  EXPECT_FALSE(status.persistenceMode);
}

/** @test Default compute mode is Default. */
TEST(GpuDriverStatusTest, DefaultComputeMode) {
  GpuDriverStatus status{};
  EXPECT_EQ(status.computeMode, ComputeMode::Default);
}

/** @test Default is not RT ready. */
TEST(GpuDriverStatusTest, DefaultNotRtReady) {
  GpuDriverStatus status{};
  EXPECT_FALSE(status.isRtReady());
}

/** @test RT ready requires persistence and exclusive process mode. */
TEST(GpuDriverStatusTest, RtReadyRequirements) {
  GpuDriverStatus status{};
  status.persistenceMode = true;
  EXPECT_FALSE(status.isRtReady());

  status.computeMode = ComputeMode::ExclusiveProcess;
  EXPECT_TRUE(status.isRtReady());
}

/** @test versionsCompatible for matching versions. */
TEST(GpuDriverStatusTest, VersionsCompatibleMatching) {
  GpuDriverStatus status{};
  status.cudaDriverVersion = 12040;
  status.cudaRuntimeVersion = 12040;
  EXPECT_TRUE(status.versionsCompatible());
}

/** @test versionsCompatible when driver > runtime. */
TEST(GpuDriverStatusTest, VersionsCompatibleNewer) {
  GpuDriverStatus status{};
  status.cudaDriverVersion = 12050;
  status.cudaRuntimeVersion = 12040;
  EXPECT_TRUE(status.versionsCompatible());
}

/** @test versionsCompatible fails when driver < runtime. */
TEST(GpuDriverStatusTest, VersionsIncompatibleOlder) {
  GpuDriverStatus status{};
  status.cudaDriverVersion = 12030;
  status.cudaRuntimeVersion = 12040;
  EXPECT_FALSE(status.versionsCompatible());
}

/** @test formatCudaVersion formats correctly. */
TEST(GpuDriverStatusTest, FormatCudaVersion) {
  EXPECT_EQ(GpuDriverStatus::formatCudaVersion(12040), "12.4");
  EXPECT_EQ(GpuDriverStatus::formatCudaVersion(11080), "11.8");
  EXPECT_EQ(GpuDriverStatus::formatCudaVersion(10000), "10.0");
}

/** @test formatCudaVersion handles edge cases. */
TEST(GpuDriverStatusTest, FormatCudaVersionEdge) {
  // Version 0 is invalid, implementation returns "unknown"
  EXPECT_EQ(GpuDriverStatus::formatCudaVersion(0), "unknown");
}

/** @test GpuDriverStatus::toString not empty. */
TEST(GpuDriverStatusTest, ToStringNotEmpty) {
  GpuDriverStatus status{};
  EXPECT_FALSE(status.toString().empty());
}

/* ----------------------------- API Tests ----------------------------- */

/** @test getGpuDriverStatus returns default for invalid index. */
TEST(GpuDriverApiTest, InvalidIndexReturnsDefault) {
  GpuDriverStatus status = getGpuDriverStatus(-1);
  EXPECT_EQ(status.deviceIndex, -1);
}

/** @test getAllGpuDriverStatus returns vector. */
TEST(GpuDriverApiTest, GetAllReturnsVector) {
  std::vector<GpuDriverStatus> all = getAllGpuDriverStatus();
  EXPECT_GE(all.size(), 0);
}

/** @test getSystemGpuDriverInfo returns global info. */
TEST(GpuDriverApiTest, SystemInfoReturnsGlobal) {
  GpuDriverStatus info = getSystemGpuDriverInfo();
  // Device index should be -1 for system-wide info
  EXPECT_EQ(info.deviceIndex, -1);
}

/** @test getGpuDriverStatus is deterministic for invalid index. */
TEST(GpuDriverApiTest, DeterministicInvalid) {
  GpuDriverStatus s1 = getGpuDriverStatus(-1);
  GpuDriverStatus s2 = getGpuDriverStatus(-1);
  EXPECT_EQ(s1.deviceIndex, s2.deviceIndex);
}
