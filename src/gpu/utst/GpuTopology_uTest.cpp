/**
 * @file GpuTopology_uTest.cpp
 * @brief Unit tests for seeker::gpu::GpuTopology.
 *
 * Notes:
 *  - Tests verify structural invariants, not specific hardware values.
 *  - Tests pass even when no GPU is present (graceful degradation).
 */

#include "src/gpu/inc/GpuTopology.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <string>

using seeker::gpu::getGpuDevice;
using seeker::gpu::getGpuTopology;
using seeker::gpu::GpuDevice;
using seeker::gpu::GpuTopology;
using seeker::gpu::GpuVendor;
using seeker::gpu::toString;

/* ----------------------------- GpuVendor Tests ----------------------------- */

/** @test GpuVendor::Unknown has string representation. */
TEST(GpuVendorTest, UnknownToString) { EXPECT_STREQ(toString(GpuVendor::Unknown), "Unknown"); }

/** @test GpuVendor::Nvidia has string representation. */
TEST(GpuVendorTest, NvidiaToString) { EXPECT_STREQ(toString(GpuVendor::Nvidia), "NVIDIA"); }

/** @test GpuVendor::Amd has string representation. */
TEST(GpuVendorTest, AmdToString) { EXPECT_STREQ(toString(GpuVendor::Amd), "AMD"); }

/** @test GpuVendor::Intel has string representation. */
TEST(GpuVendorTest, IntelToString) { EXPECT_STREQ(toString(GpuVendor::Intel), "Intel"); }

/* ----------------------------- GpuDevice Tests ----------------------------- */

/** @test Default GpuDevice has deviceIndex -1. */
TEST(GpuDeviceTest, DefaultDeviceIndex) {
  GpuDevice dev{};
  EXPECT_EQ(dev.deviceIndex, -1);
}

/** @test Default GpuDevice has Unknown vendor. */
TEST(GpuDeviceTest, DefaultVendor) {
  GpuDevice dev{};
  EXPECT_EQ(dev.vendor, GpuVendor::Unknown);
}

/** @test Default GpuDevice has empty name. */
TEST(GpuDeviceTest, DefaultName) {
  GpuDevice dev{};
  EXPECT_TRUE(dev.name.empty());
}

/** @test GpuDevice::computeCapability formats correctly. */
TEST(GpuDeviceTest, ComputeCapabilityFormat) {
  GpuDevice dev{};
  dev.smMajor = 8;
  dev.smMinor = 9;
  EXPECT_EQ(dev.computeCapability(), "8.9");
}

/** @test GpuDevice::toString produces non-empty string. */
TEST(GpuDeviceTest, ToStringNotEmpty) {
  GpuDevice dev{};
  dev.deviceIndex = 0;
  dev.name = "Test GPU";
  EXPECT_FALSE(dev.toString().empty());
}

/* ----------------------------- GpuTopology Tests ----------------------------- */

class GpuTopologyTest : public ::testing::Test {
protected:
  GpuTopology topo_{};

  void SetUp() override { topo_ = getGpuTopology(); }
};

/** @test Device count is non-negative. */
TEST_F(GpuTopologyTest, DeviceCountNonNegative) { EXPECT_GE(topo_.deviceCount, 0); }

/** @test Device vector size matches deviceCount. */
TEST_F(GpuTopologyTest, DeviceCountMatchesVector) {
  EXPECT_EQ(topo_.deviceCount, static_cast<int>(topo_.devices.size()));
}

/** @test Vendor counts sum to device count. */
TEST_F(GpuTopologyTest, VendorCountsSum) {
  const int SUM = topo_.nvidiaCount + topo_.amdCount + topo_.intelCount;
  // May be less if some devices are Unknown vendor
  EXPECT_LE(SUM, topo_.deviceCount);
}

/** @test hasGpu returns correct value. */
TEST_F(GpuTopologyTest, HasGpuConsistent) { EXPECT_EQ(topo_.hasGpu(), topo_.deviceCount > 0); }

/** @test hasCuda returns correct value. */
TEST_F(GpuTopologyTest, HasCudaConsistent) { EXPECT_EQ(topo_.hasCuda(), topo_.nvidiaCount > 0); }

/** @test Each device has valid index. */
TEST_F(GpuTopologyTest, DeviceIndicesValid) {
  for (int i = 0; i < static_cast<int>(topo_.devices.size()); ++i) {
    EXPECT_EQ(topo_.devices[i].deviceIndex, i);
  }
}

/** @test Every enumerated device has a PCI BDF and a non-empty name. */
TEST_F(GpuTopologyTest, DevicesHaveBdfAndName) {
  if (topo_.devices.empty()) {
    GTEST_SKIP() << "No GPUs present";
  }
  for (const auto& DEV : topo_.devices) {
    EXPECT_FALSE(DEV.pciBdf.empty()) << "GPU " << DEV.deviceIndex;
    EXPECT_FALSE(DEV.name.empty()) << "GPU " << DEV.deviceIndex;
  }
}

/** @test Every GPU is a PCI display controller (class 0x03); platform DRM nodes are not GPUs. */
TEST_F(GpuTopologyTest, DevicesArePciDisplayControllers) {
  for (const auto& DEV : topo_.devices) {
    std::ifstream classFile("/sys/bus/pci/devices/" + DEV.pciBdf + "/class");
    std::string pciClass;
    ASSERT_TRUE(classFile && std::getline(classFile, pciClass))
        << "GPU " << DEV.deviceIndex << " (" << DEV.pciBdf << ") has no PCI class in sysfs";
    EXPECT_EQ(pciClass.rfind("0x03", 0), 0U) << "GPU " << DEV.deviceIndex << " class " << pciClass;
  }
}

/** @test BDFs use normalized sysfs form ("dddd:bb:dd.f", lowercase hex). */
TEST_F(GpuTopologyTest, BdfNormalized) {
  if (topo_.devices.empty()) {
    GTEST_SKIP() << "No GPUs present";
  }
  for (const auto& DEV : topo_.devices) {
    const std::string& BDF = DEV.pciBdf;
    ASSERT_EQ(BDF.size(), 12U) << BDF;
    EXPECT_EQ(BDF[4], ':') << BDF;
    EXPECT_EQ(BDF[7], ':') << BDF;
    EXPECT_EQ(BDF[10], '.') << BDF;
    for (const char C : BDF) {
      EXPECT_TRUE((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f') || C == ':' || C == '.') << BDF;
    }
    EXPECT_GE(DEV.pciBus, 0);
    EXPECT_LE(DEV.pciBus, 0xFF);
    EXPECT_LE(DEV.pciDevice, 0x1F);
    EXPECT_LE(DEV.pciFunction, 0x7);
  }
}

/** @test BDFs are unique (no device reported twice). */
TEST_F(GpuTopologyTest, BdfUnique) {
  for (std::size_t i = 0; i < topo_.devices.size(); ++i) {
    for (std::size_t j = i + 1; j < topo_.devices.size(); ++j) {
      EXPECT_NE(topo_.devices[i].pciBdf, topo_.devices[j].pciBdf);
    }
  }
}

/** @test NVIDIA devices are listed before other vendors (NVML ordinals first). */
TEST_F(GpuTopologyTest, NvidiaDevicesFirst) {
  if (topo_.nvidiaCount == 0) {
    GTEST_SKIP() << "No NVIDIA GPUs present";
  }
  for (int i = 0; i < topo_.nvidiaCount; ++i) {
    EXPECT_EQ(topo_.devices[static_cast<std::size_t>(i)].vendor, GpuVendor::Nvidia);
  }
}

/** @test Compute capability, when reported, is in a plausible range. */
TEST_F(GpuTopologyTest, ComputeCapabilityRange) {
  for (const auto& DEV : topo_.devices) {
    EXPECT_GE(DEV.smMajor, 0);
    EXPECT_LT(DEV.smMajor, 100);
    EXPECT_GE(DEV.smMinor, 0);
    if (DEV.vendor != GpuVendor::Nvidia) {
      EXPECT_EQ(DEV.smMajor, 0);
    } else if (!DEV.uuid.empty()) {
      // Enumerated through the NVIDIA driver (sysfs entries carry no UUID)
      EXPECT_GE(DEV.smMajor, 3) << "GPU " << DEV.deviceIndex;
    }
  }
}

/** @test getGpuDevice(i) agrees with topology entry i. */
TEST_F(GpuTopologyTest, GetGpuDeviceMatchesTopology) {
  if (topo_.devices.empty()) {
    GTEST_SKIP() << "No GPUs present";
  }
  for (const auto& DEV : topo_.devices) {
    const GpuDevice SINGLE = getGpuDevice(DEV.deviceIndex);
    EXPECT_EQ(SINGLE.deviceIndex, DEV.deviceIndex);
    EXPECT_EQ(SINGLE.pciBdf, DEV.pciBdf);
    EXPECT_EQ(SINGLE.name, DEV.name);
    EXPECT_EQ(SINGLE.vendor, DEV.vendor);
  }
}

/** @test GpuTopology::toString produces non-empty string. */
TEST_F(GpuTopologyTest, ToStringNotEmpty) {
  // toString should work even with 0 devices
  EXPECT_FALSE(topo_.toString().empty());
}

/* ----------------------------- API Tests ----------------------------- */

/** @test getGpuDevice returns default for invalid index. */
TEST(GpuDeviceApiTest, InvalidIndexReturnsDefault) {
  GpuDevice dev = getGpuDevice(-1);
  EXPECT_EQ(dev.deviceIndex, -1);
}

/** @test getGpuDevice returns default for very large index. */
TEST(GpuDeviceApiTest, LargeIndexReturnsDefault) {
  GpuDevice dev = getGpuDevice(999);
  // Either returns default or empty device
  EXPECT_TRUE(dev.name.empty() || dev.deviceIndex == 999);
}

/** @test getGpuTopology is deterministic. */
TEST(GpuTopologyApiTest, Deterministic) {
  GpuTopology topo1 = getGpuTopology();
  GpuTopology topo2 = getGpuTopology();
  EXPECT_EQ(topo1.deviceCount, topo2.deviceCount);
  EXPECT_EQ(topo1.nvidiaCount, topo2.nvidiaCount);
}
