/**
 * @file SecurityStatus_uTest.cpp
 * @brief Unit tests for seeker::system::SecurityStatus.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not specific LSM config.
 *  - Systems without SELinux/AppArmor will have NOT_PRESENT modes (valid).
 */

#include "src/system/inc/SecurityStatus.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

using seeker::system::apparmorAvailable;
using seeker::system::ApparmorMode;
using seeker::system::ApparmorStatus;
using seeker::system::getApparmorStatus;
using seeker::system::getSecurityStatus;
using seeker::system::getSelinuxStatus;
using seeker::system::LSM_NAME_SIZE;
using seeker::system::LsmInfo;
using seeker::system::MAX_LSMS;
using seeker::system::SECURITY_CONTEXT_SIZE;
using seeker::system::SecurityStatus;
using seeker::system::selinuxAvailable;
using seeker::system::SelinuxMode;
using seeker::system::SelinuxStatus;
using seeker::system::toString;

class SecurityStatusTest : public ::testing::Test {
protected:
  SecurityStatus status_{};

  void SetUp() override { status_ = getSecurityStatus(); }
};

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default SelinuxMode is NOT_PRESENT. */
TEST(SelinuxModeDefaultTest, DefaultIsNotPresent) {
  const SelinuxMode MODE{};
  EXPECT_EQ(MODE, SelinuxMode::NOT_PRESENT);
}

/** @test Default ApparmorMode is NOT_PRESENT. */
TEST(ApparmorModeDefaultTest, DefaultIsNotPresent) {
  const ApparmorMode MODE{};
  EXPECT_EQ(MODE, ApparmorMode::NOT_PRESENT);
}

/** @test Default SelinuxStatus is zeroed. */
TEST(SelinuxStatusDefaultTest, DefaultIsZeroed) {
  const SelinuxStatus DEFAULT{};

  EXPECT_EQ(DEFAULT.mode, SelinuxMode::NOT_PRESENT);
  EXPECT_FALSE(DEFAULT.mcsEnabled);
  EXPECT_FALSE(DEFAULT.mlsEnabled);
  EXPECT_FALSE(DEFAULT.booleansPending);
  EXPECT_EQ(DEFAULT.policyType[0], '\0');
  EXPECT_EQ(DEFAULT.currentContext[0], '\0');
  EXPECT_EQ(DEFAULT.policyVersion, 0U);
  EXPECT_EQ(DEFAULT.denialCount, 0U);
}

/** @test Default ApparmorStatus is zeroed. */
TEST(ApparmorStatusDefaultTest, DefaultIsZeroed) {
  const ApparmorStatus DEFAULT{};

  EXPECT_EQ(DEFAULT.mode, ApparmorMode::NOT_PRESENT);
  EXPECT_EQ(DEFAULT.profilesLoaded, 0U);
  EXPECT_EQ(DEFAULT.profilesEnforce, 0U);
  EXPECT_EQ(DEFAULT.profilesComplain, 0U);
}

/** @test Default LsmInfo is zeroed. */
TEST(LsmInfoDefaultTest, DefaultIsZeroed) {
  const LsmInfo DEFAULT{};

  EXPECT_EQ(DEFAULT.name[0], '\0');
  EXPECT_FALSE(DEFAULT.active);
}

/** @test Default SecurityStatus is zeroed. */
TEST(SecurityStatusDefaultTest, DefaultIsZeroed) {
  const SecurityStatus DEFAULT{};

  EXPECT_EQ(DEFAULT.selinux.mode, SelinuxMode::NOT_PRESENT);
  EXPECT_EQ(DEFAULT.apparmor.mode, ApparmorMode::NOT_PRESENT);
  EXPECT_EQ(DEFAULT.lsmCount, 0U);
  EXPECT_FALSE(DEFAULT.seccompAvailable);
  EXPECT_FALSE(DEFAULT.landLockAvailable);
  EXPECT_FALSE(DEFAULT.yamaPtrace);
}

/* ----------------------------- SelinuxMode Method Tests ----------------------------- */

/** @test toString covers all SelinuxMode values. */
TEST(SelinuxModeTest, ToStringCoversAllValues) {
  EXPECT_STREQ(toString(SelinuxMode::NOT_PRESENT), "not present");
  EXPECT_STREQ(toString(SelinuxMode::DISABLED), "disabled");
  EXPECT_STREQ(toString(SelinuxMode::PERMISSIVE), "permissive");
  EXPECT_STREQ(toString(SelinuxMode::ENFORCING), "enforcing");
}

/** @test toString handles invalid SelinuxMode values. */
TEST(SelinuxModeTest, ToStringHandlesInvalid) {
  const auto INVALID = static_cast<SelinuxMode>(255);
  const char* RESULT = toString(INVALID);
  EXPECT_NE(RESULT, nullptr);
  EXPECT_GT(std::strlen(RESULT), 0U);
}

/** @test All SelinuxMode enum values are distinct. */
TEST(SelinuxModeTest, AllValuesDistinct) {
  std::set<std::uint8_t> values;
  values.insert(static_cast<std::uint8_t>(SelinuxMode::NOT_PRESENT));
  values.insert(static_cast<std::uint8_t>(SelinuxMode::DISABLED));
  values.insert(static_cast<std::uint8_t>(SelinuxMode::PERMISSIVE));
  values.insert(static_cast<std::uint8_t>(SelinuxMode::ENFORCING));
  EXPECT_EQ(values.size(), 4U);
}

/* ----------------------------- ApparmorMode Method Tests ----------------------------- */

/** @test toString covers all ApparmorMode values. */
TEST(ApparmorModeTest, ToStringCoversAllValues) {
  EXPECT_STREQ(toString(ApparmorMode::NOT_PRESENT), "not present");
  EXPECT_STREQ(toString(ApparmorMode::DISABLED), "disabled");
  EXPECT_STREQ(toString(ApparmorMode::ENABLED), "enabled");
}

/** @test toString handles invalid ApparmorMode values. */
TEST(ApparmorModeTest, ToStringHandlesInvalid) {
  const auto INVALID = static_cast<ApparmorMode>(255);
  const char* RESULT = toString(INVALID);
  EXPECT_NE(RESULT, nullptr);
  EXPECT_GT(std::strlen(RESULT), 0U);
}

/** @test All ApparmorMode enum values are distinct. */
TEST(ApparmorModeTest, AllValuesDistinct) {
  std::set<std::uint8_t> values;
  values.insert(static_cast<std::uint8_t>(ApparmorMode::NOT_PRESENT));
  values.insert(static_cast<std::uint8_t>(ApparmorMode::DISABLED));
  values.insert(static_cast<std::uint8_t>(ApparmorMode::ENABLED));
  EXPECT_EQ(values.size(), 3U);
}

/* ----------------------------- SelinuxStatus Method Tests ----------------------------- */

/** @test isActive returns true when permissive. */
TEST(SelinuxStatusMethodTest, IsActiveWhenPermissive) {
  SelinuxStatus status{};
  status.mode = SelinuxMode::PERMISSIVE;
  EXPECT_TRUE(status.isActive());
  EXPECT_FALSE(status.isEnforcing());
}

/** @test isActive returns true when enforcing. */
TEST(SelinuxStatusMethodTest, IsActiveWhenEnforcing) {
  SelinuxStatus status{};
  status.mode = SelinuxMode::ENFORCING;
  EXPECT_TRUE(status.isActive());
  EXPECT_TRUE(status.isEnforcing());
}

/** @test isActive returns false when not present. */
TEST(SelinuxStatusMethodTest, NotActiveWhenNotPresent) {
  SelinuxStatus status{};
  status.mode = SelinuxMode::NOT_PRESENT;
  EXPECT_FALSE(status.isActive());
  EXPECT_FALSE(status.isEnforcing());
}

/** @test isActive returns false when disabled. */
TEST(SelinuxStatusMethodTest, NotActiveWhenDisabled) {
  SelinuxStatus status{};
  status.mode = SelinuxMode::DISABLED;
  EXPECT_FALSE(status.isActive());
  EXPECT_FALSE(status.isEnforcing());
}

/* ----------------------------- ApparmorStatus Method Tests ----------------------------- */

/** @test isActive returns true when enabled. */
TEST(ApparmorStatusMethodTest, IsActiveWhenEnabled) {
  ApparmorStatus status{};
  status.mode = ApparmorMode::ENABLED;
  EXPECT_TRUE(status.isActive());
}

/** @test isActive returns false when not present. */
TEST(ApparmorStatusMethodTest, NotActiveWhenNotPresent) {
  ApparmorStatus status{};
  status.mode = ApparmorMode::NOT_PRESENT;
  EXPECT_FALSE(status.isActive());
}

/** @test isActive returns false when disabled. */
TEST(ApparmorStatusMethodTest, NotActiveWhenDisabled) {
  ApparmorStatus status{};
  status.mode = ApparmorMode::DISABLED;
  EXPECT_FALSE(status.isActive());
}

/* ----------------------------- SecurityStatus Method Tests ----------------------------- */

/** @test hasEnforcement detects SELinux enforcing. */
TEST(SecurityStatusMethodTest, HasEnforcementWithSelinux) {
  SecurityStatus status{};
  status.selinux.mode = SelinuxMode::ENFORCING;
  EXPECT_TRUE(status.hasEnforcement());
}

/** @test hasEnforcement detects AppArmor enforcement. */
TEST(SecurityStatusMethodTest, HasEnforcementWithApparmor) {
  SecurityStatus status{};
  status.apparmor.mode = ApparmorMode::ENABLED;
  status.apparmor.profilesEnforce = 5;
  EXPECT_TRUE(status.hasEnforcement());
}

/** @test hasEnforcement returns false when permissive only. */
TEST(SecurityStatusMethodTest, NoEnforcementWhenPermissive) {
  SecurityStatus status{};
  status.selinux.mode = SelinuxMode::PERMISSIVE;
  EXPECT_FALSE(status.hasEnforcement());
}

/** @test hasEnforcement returns false by default. */
TEST(SecurityStatusMethodTest, NoEnforcementDefault) {
  const SecurityStatus DEFAULT{};
  EXPECT_FALSE(DEFAULT.hasEnforcement());
}

/** @test activeLsmList returns "none" when empty. */
TEST(SecurityStatusMethodTest, ActiveLsmListEmpty) {
  const SecurityStatus DEFAULT{};
  const std::string RESULT = DEFAULT.activeLsmList();
  EXPECT_EQ(RESULT, "none");
}

/** @test activeLsmList includes active LSMs. */
TEST(SecurityStatusMethodTest, ActiveLsmListWithEntries) {
  SecurityStatus status{};
  std::strcpy(status.lsms[0].name.data(), "capability");
  status.lsms[0].active = true;
  std::strcpy(status.lsms[1].name.data(), "yama");
  status.lsms[1].active = true;
  status.lsmCount = 2;

  const std::string RESULT = status.activeLsmList();
  EXPECT_NE(RESULT.find("capability"), std::string::npos);
  EXPECT_NE(RESULT.find("yama"), std::string::npos);
}

/* ----------------------------- API Tests ----------------------------- */

/** @test selinuxAvailable returns boolean. */
TEST(SecurityStatusApiTest, SelinuxAvailableReturnsBoolean) {
  const bool RESULT = selinuxAvailable();
  (void)RESULT;
  SUCCEED();
}

/** @test apparmorAvailable returns boolean. */
TEST(SecurityStatusApiTest, ApparmorAvailableReturnsBoolean) {
  const bool RESULT = apparmorAvailable();
  (void)RESULT;
  SUCCEED();
}

/** @test getSelinuxStatus returns valid struct. */
TEST(SecurityStatusApiTest, GetSelinuxStatusReturnsValid) {
  const SelinuxStatus STATUS = getSelinuxStatus();
  EXPECT_GE(static_cast<std::uint8_t>(STATUS.mode), 0U);
  EXPECT_LE(static_cast<std::uint8_t>(STATUS.mode), 3U);

  if (STATUS.isActive()) {
    EXPECT_GT(STATUS.policyVersion, 0U);
  }
}

/** @test getApparmorStatus returns valid struct. */
TEST(SecurityStatusApiTest, GetApparmorStatusReturnsValid) {
  const ApparmorStatus STATUS = getApparmorStatus();
  EXPECT_GE(static_cast<std::uint8_t>(STATUS.mode), 0U);
  EXPECT_LE(static_cast<std::uint8_t>(STATUS.mode), 2U);

  if (STATUS.isActive()) {
    EXPECT_EQ(STATUS.profilesLoaded, STATUS.profilesEnforce + STATUS.profilesComplain);
  }
}

/** @test getSecurityStatus returns valid struct. */
TEST_F(SecurityStatusTest, ReturnsValidStruct) {
  EXPECT_LE(status_.lsmCount, MAX_LSMS);

  for (std::size_t i = 0; i < status_.lsmCount; ++i) {
    EXPECT_GT(std::strlen(status_.lsms[i].name.data()), 0U);
    EXPECT_TRUE(status_.lsms[i].active);
  }
}

/** @test getSecurityStatus is consistent with availability checks. */
TEST_F(SecurityStatusTest, ConsistentWithAvailability) {
  if (selinuxAvailable()) {
    EXPECT_NE(status_.selinux.mode, SelinuxMode::NOT_PRESENT);
  }

  if (apparmorAvailable()) {
    EXPECT_NE(status_.apparmor.mode, ApparmorMode::NOT_PRESENT);
  }
}

/* ----------------------------- Constants Tests ----------------------------- */

/** @test LSM_NAME_SIZE is reasonable. */
TEST(SecurityStatusConstantsTest, LsmNameSizeReasonable) {
  EXPECT_GE(LSM_NAME_SIZE, 16U);
  EXPECT_LE(LSM_NAME_SIZE, 128U);
}

/** @test SECURITY_CONTEXT_SIZE is reasonable. */
TEST(SecurityStatusConstantsTest, SecurityContextSizeReasonable) {
  EXPECT_GE(SECURITY_CONTEXT_SIZE, 128U);
  EXPECT_LE(SECURITY_CONTEXT_SIZE, 1024U);
}

/** @test MAX_LSMS is reasonable. */
TEST(SecurityStatusConstantsTest, MaxLsmsReasonable) {
  EXPECT_GE(MAX_LSMS, 4U);
  EXPECT_LE(MAX_LSMS, 32U);
}

/* ----------------------------- toString Tests ----------------------------- */

/** @test SelinuxMode toString returns non-null for all values. */
TEST(SecurityStatusToStringTest, SelinuxModeNotNull) {
  for (int i = 0; i < 8; ++i) {
    const char* RESULT = toString(static_cast<SelinuxMode>(i));
    EXPECT_NE(RESULT, nullptr);
  }
}

/** @test ApparmorMode toString returns non-null for all values. */
TEST(SecurityStatusToStringTest, ApparmorModeNotNull) {
  for (int i = 0; i < 8; ++i) {
    const char* RESULT = toString(static_cast<ApparmorMode>(i));
    EXPECT_NE(RESULT, nullptr);
  }
}

/** @test SelinuxStatus::toString produces output. */
TEST(SecurityStatusToStringTest, SelinuxStatusProducesOutput) {
  SelinuxStatus status{};
  status.mode = SelinuxMode::ENFORCING;
  const std::string OUTPUT = status.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("SELinux"), std::string::npos);
}

/** @test ApparmorStatus::toString produces output. */
TEST(SecurityStatusToStringTest, ApparmorStatusProducesOutput) {
  ApparmorStatus status{};
  status.mode = ApparmorMode::ENABLED;
  status.profilesLoaded = 10;
  const std::string OUTPUT = status.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("AppArmor"), std::string::npos);
}

/** @test LsmInfo::toString produces output. */
TEST(SecurityStatusToStringTest, LsmInfoProducesOutput) {
  LsmInfo info{};
  std::strcpy(info.name.data(), "capability");
  info.active = true;
  const std::string OUTPUT = info.toString();
  EXPECT_NE(OUTPUT.find("capability"), std::string::npos);
}

/** @test SecurityStatus::toString produces output. */
TEST_F(SecurityStatusTest, ToStringProducesOutput) {
  const std::string OUTPUT = status_.toString();
  EXPECT_FALSE(OUTPUT.empty());
  EXPECT_NE(OUTPUT.find("LSM"), std::string::npos);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test selinuxAvailable returns consistent results. */
TEST(SecurityStatusDeterminismTest, SelinuxAvailableDeterministic) {
  const bool FIRST = selinuxAvailable();
  const bool SECOND = selinuxAvailable();
  EXPECT_EQ(FIRST, SECOND);
}

/** @test apparmorAvailable returns consistent results. */
TEST(SecurityStatusDeterminismTest, ApparmorAvailableDeterministic) {
  const bool FIRST = apparmorAvailable();
  const bool SECOND = apparmorAvailable();
  EXPECT_EQ(FIRST, SECOND);
}

/** @test getSelinuxStatus returns consistent results. */
TEST(SecurityStatusDeterminismTest, GetSelinuxStatusDeterministic) {
  const SelinuxStatus FIRST = getSelinuxStatus();
  const SelinuxStatus SECOND = getSelinuxStatus();
  EXPECT_EQ(FIRST.mode, SECOND.mode);
  EXPECT_EQ(FIRST.policyVersion, SECOND.policyVersion);
  EXPECT_EQ(FIRST.mcsEnabled, SECOND.mcsEnabled);
}

/** @test getApparmorStatus returns consistent results. */
TEST(SecurityStatusDeterminismTest, GetApparmorStatusDeterministic) {
  const ApparmorStatus FIRST = getApparmorStatus();
  const ApparmorStatus SECOND = getApparmorStatus();
  EXPECT_EQ(FIRST.mode, SECOND.mode);
  EXPECT_EQ(FIRST.profilesLoaded, SECOND.profilesLoaded);
}

/** @test getSecurityStatus returns consistent results. */
TEST(SecurityStatusDeterminismTest, GetSecurityStatusDeterministic) {
  const SecurityStatus FIRST = getSecurityStatus();
  const SecurityStatus SECOND = getSecurityStatus();
  EXPECT_EQ(FIRST.lsmCount, SECOND.lsmCount);
  EXPECT_EQ(FIRST.seccompAvailable, SECOND.seccompAvailable);
  EXPECT_EQ(FIRST.landLockAvailable, SECOND.landLockAvailable);
}

/** @test toString returns same pointer for same enum value. */
TEST(SecurityStatusDeterminismTest, ToStringEnumDeterministic) {
  const char* FIRST = toString(SelinuxMode::ENFORCING);
  const char* SECOND = toString(SelinuxMode::ENFORCING);
  EXPECT_EQ(FIRST, SECOND);
}