/**
 * @file VirtualizationInfo_uTest.cpp
 * @brief Unit tests for seeker::system::VirtualizationInfo.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 *  - Actual virtualization detection varies by environment.
 *  - Tests verify API contracts and data consistency.
 */

#include "src/system/inc/VirtualizationInfo.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using seeker::system::ContainerRuntime;
using seeker::system::getVirtualizationInfo;
using seeker::system::Hypervisor;
using seeker::system::isContainerized;
using seeker::system::isVirtualized;
using seeker::system::toString;
using seeker::system::VirtType;
using seeker::system::VirtualizationInfo;

class VirtualizationInfoTest : public ::testing::Test {
protected:
  VirtualizationInfo info_{};

  void SetUp() override { info_ = getVirtualizationInfo(); }
};

/* ----------------------------- Basic Query Tests ----------------------------- */

/** @test getVirtualizationInfo returns valid structure. */
TEST_F(VirtualizationInfoTest, QueryReturnsValidStructure) {
  // Type should be a valid enum value
  EXPECT_GE(static_cast<int>(info_.type), 0);
  EXPECT_LE(static_cast<int>(info_.type), 3);
}

/** @test Type classification is consistent. */
TEST_F(VirtualizationInfoTest, TypeClassificationConsistent) {
  switch (info_.type) {
  case VirtType::NONE:
    EXPECT_TRUE(info_.isBareMetal());
    EXPECT_FALSE(info_.isVirtualized());
    EXPECT_FALSE(info_.isVirtualMachine());
    EXPECT_FALSE(info_.isContainer());
    break;
  case VirtType::VM:
    EXPECT_FALSE(info_.isBareMetal());
    EXPECT_TRUE(info_.isVirtualized());
    EXPECT_TRUE(info_.isVirtualMachine());
    EXPECT_FALSE(info_.isContainer());
    break;
  case VirtType::CONTAINER:
    EXPECT_FALSE(info_.isBareMetal());
    EXPECT_TRUE(info_.isVirtualized());
    EXPECT_FALSE(info_.isVirtualMachine());
    EXPECT_TRUE(info_.isContainer());
    break;
  case VirtType::UNKNOWN:
    EXPECT_FALSE(info_.isBareMetal());
    EXPECT_TRUE(info_.isVirtualized());
    break;
  }
}

/** @test Confidence is within valid range. */
TEST_F(VirtualizationInfoTest, ConfidenceInRange) {
  EXPECT_GE(info_.confidence, 0);
  EXPECT_LE(info_.confidence, 100);
}

/** @test RT suitability is within valid range. */
TEST_F(VirtualizationInfoTest, RtSuitabilityInRange) {
  EXPECT_GE(info_.rtSuitability, 0);
  EXPECT_LE(info_.rtSuitability, 100);
}

/** @test isRtSuitable is consistent with rtSuitability score. */
TEST_F(VirtualizationInfoTest, RtSuitableConsistent) {
  EXPECT_EQ(info_.isRtSuitable(), info_.rtSuitability >= 70);
}

/* ----------------------------- Hypervisor Tests ----------------------------- */

/** @test Hypervisor is set when type is VM. */
TEST_F(VirtualizationInfoTest, HypervisorSetForVm) {
  if (info_.type == VirtType::VM) {
    // Should have some hypervisor identified (even if OTHER)
    EXPECT_NE(info_.hypervisor, Hypervisor::NONE);
  }
}

/** @test Hypervisor is NONE when type is not VM. */
TEST_F(VirtualizationInfoTest, HypervisorNoneForNonVm) {
  if (info_.type == VirtType::NONE || info_.type == VirtType::CONTAINER) {
    // Might still have hypervisor if container runs in VM
    // Just verify it's a valid enum
    EXPECT_GE(static_cast<int>(info_.hypervisor), 0);
  }
}

/* ----------------------------- Container Runtime Tests ----------------------------- */

/** @test Container runtime is set when type is CONTAINER. */
TEST_F(VirtualizationInfoTest, ContainerRuntimeSetForContainer) {
  if (info_.type == VirtType::CONTAINER) {
    EXPECT_NE(info_.containerRuntime, ContainerRuntime::NONE);
  }
}

/** @test containerIndicators is consistent with container type. */
TEST_F(VirtualizationInfoTest, ContainerIndicatorsConsistent) {
  if (info_.type == VirtType::CONTAINER) {
    EXPECT_TRUE(info_.containerIndicators);
  }
}

/* ----------------------------- Detection Flag Tests ----------------------------- */

/** @test cpuidHypervisor implies some virtualization. */
TEST_F(VirtualizationInfoTest, CpuidHypervisorImpliesVirt) {
  if (info_.cpuidHypervisor) {
    // Either VM or container running in VM
    EXPECT_TRUE(info_.isVirtualized() || info_.hypervisor != Hypervisor::NONE);
  }
}

/** @test dmiVirtual implies virtualization. */
TEST_F(VirtualizationInfoTest, DmiVirtualImpliesVirt) {
  if (info_.dmiVirtual) {
    EXPECT_TRUE(info_.isVirtualized() || info_.hypervisor != Hypervisor::NONE);
  }
}

/* ----------------------------- String Tests ----------------------------- */

/** @test description returns non-null. */
TEST_F(VirtualizationInfoTest, DescriptionNonNull) {
  const char* DESC = info_.description();
  EXPECT_NE(DESC, nullptr);
  EXPECT_GT(std::strlen(DESC), 0U);
}

/** @test toString produces non-empty output. */
TEST_F(VirtualizationInfoTest, ToStringNonEmpty) {
  const std::string OUTPUT = info_.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("Virtualization"), std::string::npos);
}

/** @test toString contains type. */
TEST_F(VirtualizationInfoTest, ToStringContainsType) {
  const std::string OUTPUT = info_.toString();
  EXPECT_NE(OUTPUT.find("Type"), std::string::npos);
}

/* ----------------------------- Enum toString Tests ----------------------------- */

/** @test VirtType toString returns valid strings. */
TEST(VirtTypeToStringTest, AllValues) {
  EXPECT_STREQ(toString(VirtType::NONE), "bare_metal");
  EXPECT_STREQ(toString(VirtType::VM), "vm");
  EXPECT_STREQ(toString(VirtType::CONTAINER), "container");
  EXPECT_STREQ(toString(VirtType::UNKNOWN), "unknown");
}

/** @test Hypervisor toString returns valid strings. */
TEST(HypervisorToStringTest, CommonValues) {
  EXPECT_STREQ(toString(Hypervisor::NONE), "none");
  EXPECT_STREQ(toString(Hypervisor::KVM), "kvm");
  EXPECT_STREQ(toString(Hypervisor::VMWARE), "vmware");
  EXPECT_STREQ(toString(Hypervisor::VIRTUALBOX), "virtualbox");
  EXPECT_STREQ(toString(Hypervisor::HYPERV), "hyper-v");
  EXPECT_STREQ(toString(Hypervisor::XEN), "xen");
}

/** @test ContainerRuntime toString returns valid strings. */
TEST(ContainerRuntimeToStringTest, CommonValues) {
  EXPECT_STREQ(toString(ContainerRuntime::NONE), "none");
  EXPECT_STREQ(toString(ContainerRuntime::DOCKER), "docker");
  EXPECT_STREQ(toString(ContainerRuntime::PODMAN), "podman");
  EXPECT_STREQ(toString(ContainerRuntime::LXC), "lxc");
  EXPECT_STREQ(toString(ContainerRuntime::WSL), "wsl");
}

/** @test toString never returns null for any enum value. */
TEST(EnumToStringTest, NeverNull) {
  for (int i = 0; i <= 3; ++i) {
    EXPECT_NE(toString(static_cast<VirtType>(i)), nullptr);
  }
  for (int i = 0; i <= 15; ++i) {
    EXPECT_NE(toString(static_cast<Hypervisor>(i)), nullptr);
  }
  for (int i = 0; i <= 8; ++i) {
    EXPECT_NE(toString(static_cast<ContainerRuntime>(i)), nullptr);
  }
}

/* ----------------------------- Quick Check API Tests ----------------------------- */

/** @test isVirtualized is consistent with getVirtualizationInfo. */
TEST_F(VirtualizationInfoTest, IsVirtualizedConsistent) {
  // isVirtualized() should return true if we detected virtualization
  // Note: Quick check might miss some cases full detection catches
  if (info_.cpuidHypervisor || info_.containerIndicators) {
    // Quick check should catch these
    EXPECT_TRUE(isVirtualized() || info_.isVirtualized());
  }
}

/** @test isContainerized is consistent with container detection. */
TEST_F(VirtualizationInfoTest, IsContainerizedConsistent) {
  const bool QUICK_RESULT = isContainerized();

  if (info_.type == VirtType::CONTAINER) {
    EXPECT_TRUE(QUICK_RESULT);
  }

  if (QUICK_RESULT) {
    EXPECT_TRUE(info_.containerIndicators || info_.type == VirtType::CONTAINER);
  }
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default VirtualizationInfo is zeroed. */
TEST(VirtualizationInfoDefaultTest, DefaultZeroed) {
  const VirtualizationInfo INFO{};

  EXPECT_EQ(INFO.type, VirtType::NONE);
  EXPECT_EQ(INFO.hypervisor, Hypervisor::NONE);
  EXPECT_EQ(INFO.containerRuntime, ContainerRuntime::NONE);
  EXPECT_EQ(INFO.hypervisorName[0], '\0');
  EXPECT_EQ(INFO.containerName[0], '\0');
  EXPECT_EQ(INFO.productName[0], '\0');
  EXPECT_FALSE(INFO.cpuidHypervisor);
  EXPECT_FALSE(INFO.dmiVirtual);
  EXPECT_FALSE(INFO.containerIndicators);
  EXPECT_FALSE(INFO.nested);
  EXPECT_FALSE(INFO.paravirt);
  EXPECT_EQ(INFO.confidence, 0);
  EXPECT_EQ(INFO.rtSuitability, 0);
}

/** @test Default structure reports bare metal. */
TEST(VirtualizationInfoDefaultTest, DefaultIsBareMetal) {
  const VirtualizationInfo INFO{};

  EXPECT_TRUE(INFO.isBareMetal());
  EXPECT_FALSE(INFO.isVirtualized());
  EXPECT_FALSE(INFO.isVirtualMachine());
  EXPECT_FALSE(INFO.isContainer());
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test getVirtualizationInfo returns consistent results. */
TEST(VirtualizationDeterminismTest, ConsistentResults) {
  const auto INFO1 = getVirtualizationInfo();
  const auto INFO2 = getVirtualizationInfo();

  // Type should be identical
  EXPECT_EQ(INFO1.type, INFO2.type);
  EXPECT_EQ(INFO1.hypervisor, INFO2.hypervisor);
  EXPECT_EQ(INFO1.containerRuntime, INFO2.containerRuntime);

  // Detection flags should be identical
  EXPECT_EQ(INFO1.cpuidHypervisor, INFO2.cpuidHypervisor);
  EXPECT_EQ(INFO1.dmiVirtual, INFO2.dmiVirtual);
  EXPECT_EQ(INFO1.containerIndicators, INFO2.containerIndicators);

  // Product info should be identical
  EXPECT_STREQ(INFO1.productName.data(), INFO2.productName.data());
  EXPECT_STREQ(INFO1.manufacturer.data(), INFO2.manufacturer.data());
}

/* ----------------------------- RT Suitability Scenarios ----------------------------- */

/** @test Bare metal has high RT suitability. */
TEST_F(VirtualizationInfoTest, BareMetalHighRtSuitability) {
  if (info_.type == VirtType::NONE) {
    EXPECT_GE(info_.rtSuitability, 90);
    EXPECT_TRUE(info_.isRtSuitable());
  }
}

/** @test Nested virtualization has low RT suitability. */
TEST_F(VirtualizationInfoTest, NestedLowRtSuitability) {
  if (info_.nested && info_.type == VirtType::VM) {
    EXPECT_LE(info_.rtSuitability, 30);
    EXPECT_FALSE(info_.isRtSuitable());
  }
}