/**
 * @file GpuIsolation_uTest.cpp
 * @brief Unit tests for seeker::gpu::GpuIsolation.
 *
 * Notes:
 *  - Tests verify structural invariants, not specific hardware values.
 *  - Tests pass even when no GPU is present (graceful degradation).
 */

#include "src/gpu/inc/GpuIsolation.hpp"

#include <gtest/gtest.h>

using seeker::gpu::getAllGpuIsolation;
using seeker::gpu::getGpuIsolation;
using seeker::gpu::GpuIsolation;
using seeker::gpu::GpuProcess;
using seeker::gpu::MigInstance;

/* ----------------------------- MigInstance Tests ----------------------------- */

/** @test Default MigInstance has index -1. */
TEST(MigInstanceTest, DefaultIndex) {
  MigInstance inst{};
  EXPECT_EQ(inst.index, -1);
}

/** @test Default has empty name. */
TEST(MigInstanceTest, DefaultName) {
  MigInstance inst{};
  EXPECT_TRUE(inst.name.empty());
}

/** @test Default has zero SMs. */
TEST(MigInstanceTest, DefaultSmCount) {
  MigInstance inst{};
  EXPECT_EQ(inst.smCount, 0);
}

/** @test Default has zero memory. */
TEST(MigInstanceTest, DefaultMemory) {
  MigInstance inst{};
  EXPECT_EQ(inst.memoryBytes, 0);
}

/** @test MigInstance::toString not empty. */
TEST(MigInstanceTest, ToStringNotEmpty) {
  MigInstance inst{};
  inst.index = 0;
  inst.name = "1g.5gb";
  EXPECT_FALSE(inst.toString().empty());
}

/* ----------------------------- GpuProcess Tests ----------------------------- */

/** @test Default GpuProcess has pid 0. */
TEST(GpuProcessTest, DefaultPid) {
  GpuProcess proc{};
  EXPECT_EQ(proc.pid, 0);
}

/** @test Default has empty name. */
TEST(GpuProcessTest, DefaultName) {
  GpuProcess proc{};
  EXPECT_TRUE(proc.name.empty());
}

/** @test Default has zero memory. */
TEST(GpuProcessTest, DefaultMemory) {
  GpuProcess proc{};
  EXPECT_EQ(proc.usedMemoryBytes, 0);
}

/** @test Default type is Unknown. */
TEST(GpuProcessTest, DefaultType) {
  GpuProcess proc{};
  EXPECT_EQ(proc.type, GpuProcess::Type::Unknown);
}

/** @test GpuProcess::toString not empty. */
TEST(GpuProcessTest, ToStringNotEmpty) {
  GpuProcess proc{};
  proc.pid = 1234;
  proc.name = "test_proc";
  EXPECT_FALSE(proc.toString().empty());
}

/* ----------------------------- GpuIsolation Tests ----------------------------- */

/** @test Default GpuIsolation has deviceIndex -1. */
TEST(GpuIsolationTest, DefaultDeviceIndex) {
  GpuIsolation iso{};
  EXPECT_EQ(iso.deviceIndex, -1);
}

/** @test Default has MIG disabled. */
TEST(GpuIsolationTest, DefaultMigDisabled) {
  GpuIsolation iso{};
  EXPECT_FALSE(iso.migModeEnabled);
  EXPECT_FALSE(iso.migModeSupported);
}

/** @test Default has empty MIG instances. */
TEST(GpuIsolationTest, DefaultMigInstancesEmpty) {
  GpuIsolation iso{};
  EXPECT_TRUE(iso.migInstances.empty());
}

/** @test Default has MPS inactive. */
TEST(GpuIsolationTest, DefaultMpsInactive) {
  GpuIsolation iso{};
  EXPECT_FALSE(iso.mpsServerActive);
}

/** @test Default has zero process counts. */
TEST(GpuIsolationTest, DefaultProcessCounts) {
  GpuIsolation iso{};
  EXPECT_EQ(iso.computeProcessCount, 0);
  EXPECT_EQ(iso.graphicsProcessCount, 0);
}

/** @test Default has empty processes vector. */
TEST(GpuIsolationTest, DefaultProcessesEmpty) {
  GpuIsolation iso{};
  EXPECT_TRUE(iso.processes.empty());
}

/** @test Default is not exclusive. */
TEST(GpuIsolationTest, DefaultNotExclusive) {
  GpuIsolation iso{};
  EXPECT_FALSE(iso.isExclusive());
}

/** @test isExclusive true with ExclusiveProcess mode. */
TEST(GpuIsolationTest, ExclusiveWithMode) {
  GpuIsolation iso{};
  iso.computeMode = GpuIsolation::ComputeMode::ExclusiveProcess;
  EXPECT_TRUE(iso.isExclusive());
}

/** @test Default is not RT isolated. */
TEST(GpuIsolationTest, DefaultNotRtIsolated) {
  GpuIsolation iso{};
  EXPECT_FALSE(iso.isRtIsolated());
}

/** @test isRtIsolated true with exclusive mode and no other processes. */
TEST(GpuIsolationTest, RtIsolatedExclusiveNoProcesses) {
  GpuIsolation iso{};
  iso.computeMode = GpuIsolation::ComputeMode::ExclusiveProcess;
  iso.computeProcessCount = 0;
  iso.graphicsProcessCount = 0;
  EXPECT_TRUE(iso.isRtIsolated());
}

/** @test isRtIsolated false with other processes. */
TEST(GpuIsolationTest, NotRtIsolatedWithProcesses) {
  GpuIsolation iso{};
  iso.computeMode = GpuIsolation::ComputeMode::ExclusiveProcess;
  iso.computeProcessCount = 2;
  EXPECT_FALSE(iso.isRtIsolated());
}

/** @test GpuIsolation::toString not empty. */
TEST(GpuIsolationTest, ToStringNotEmpty) {
  GpuIsolation iso{};
  EXPECT_FALSE(iso.toString().empty());
}

/* ----------------------------- API Tests ----------------------------- */

/** @test getGpuIsolation returns default for invalid index. */
TEST(GpuIsolationApiTest, InvalidIndexReturnsDefault) {
  GpuIsolation iso = getGpuIsolation(-1);
  EXPECT_EQ(iso.deviceIndex, -1);
}

/** @test getAllGpuIsolation returns vector. */
TEST(GpuIsolationApiTest, GetAllReturnsVector) {
  std::vector<GpuIsolation> all = getAllGpuIsolation();
  EXPECT_GE(all.size(), 0);
}

/** @test getGpuIsolation is deterministic for invalid index. */
TEST(GpuIsolationApiTest, DeterministicInvalid) {
  GpuIsolation i1 = getGpuIsolation(-1);
  GpuIsolation i2 = getGpuIsolation(-1);
  EXPECT_EQ(i1.deviceIndex, i2.deviceIndex);
}
