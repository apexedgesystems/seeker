/**
 * @file ScratchTree_uTest.cpp
 * @brief Unit tests for seeker::test_support::ScratchTreeTest.
 *
 * Notes:
 *  - Each test checks one promise of the fixture on its own scratch tree.
 *  - Outside targets for the containment cases are disposable sibling trees
 *    owned by a ScratchDirectory, never anything the developer owns.
 *  - ctest runs every test in its own process, so removal is checked inside
 *    one test (TearDown() then SetUp() again), not by a later test.
 *  - The permission case skips as root, where the kernel ignores mode bits.
 */
#include "src/helpers/utst/inc/ScratchTree.hpp"

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using seeker::test_support::ScratchDirectory;
using seeker::test_support::ScratchTreeTest;

namespace {

/// Whole file, read in binary mode.
std::string readBytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// A disposable directory beside the test's tree, holding one sentinel file.
class OutsideTree {
public:
  OutsideTree() : owner_(std::filesystem::path(::testing::TempDir()), "ScratchTreeTest.outside") {
    if (owner_.valid()) {
      std::ofstream(owner_.path() / "sentinel") << "original";
    }
  }
  [[nodiscard]] bool valid() const { return owner_.valid(); }
  [[nodiscard]] const std::filesystem::path& path() const { return owner_.path(); }
  [[nodiscard]] std::string sentinel() const { return readBytes(owner_.path() / "sentinel"); }

private:
  ScratchDirectory owner_;
};

} // namespace

/* ----------------------------- Tree Declaration Tests ----------------------------- */

/** @test The directory exists under testing::TempDir(), made absolute, and is named after the test
 */
TEST_F(ScratchTreeTest, NamesTheDirectoryAfterTheTestUnderTempDir) {
  EXPECT_TRUE(std::filesystem::is_directory(root_));
  EXPECT_TRUE(root_.is_absolute());
  // TempDir() ends with a separator, so parent_path() of the resolved path is the directory itself
  EXPECT_EQ(root_.parent_path(),
            std::filesystem::absolute(std::filesystem::path(::testing::TempDir())).parent_path());
  EXPECT_EQ(root_.filename().string().rfind(
                "ScratchTreeTest.NamesTheDirectoryAfterTheTestUnderTempDir.", 0),
            0U);
}

/** @test put() writes exactly the given bytes, adding no newline */
TEST_F(ScratchTreeTest, PutWritesExactBytes) {
  EXPECT_EQ(readBytes(put("plain", "no newline")), "no newline");
  EXPECT_EQ(readBytes(put("attribute", "500695040\n")), "500695040\n");
  EXPECT_EQ(readBytes(put("empty", "")), "");
}

/** @test An absolute path is mirrored under root_, parents created */
TEST_F(ScratchTreeTest, MirrorsAbsolutePathsUnderTheRoot) {
  EXPECT_EQ(put("/sys/block/sda/size", "1\n"), root_ / "sys/block/sda/size");
  EXPECT_EQ(dir("/sys/devices/system/node"), root_ / "sys/devices/system/node");
  EXPECT_TRUE(std::filesystem::is_regular_file(root_ / "sys/block/sda/size"));
  EXPECT_TRUE(std::filesystem::is_directory(root_ / "sys/devices/system/node"));
}

/** @test link() stores the target text as given, and the link resolves inside the tree */
TEST_F(ScratchTreeTest, LinkTextRoundTripsThroughReadSymlink) {
  constexpr const char* TARGET = "../../../devices/platform/can0";
  dir("/sys/devices/platform/can0");
  const std::filesystem::path LINK = link("/sys/class/net/can0/device", TARGET);
  EXPECT_EQ(LINK, root_ / "sys/class/net/can0/device");
  EXPECT_EQ(std::filesystem::read_symlink(LINK).string(), TARGET);
  EXPECT_TRUE(std::filesystem::is_directory(LINK));
}

/* ----------------------------- Refusal Tests ----------------------------- */

/** @test A path starting with ".." is refused: empty path, nothing created outside the tree */
TEST_F(ScratchTreeTest, RefusesALeadingDotDot) {
  const std::string ESCAPE = root_.filename().string() + ".escape";
  std::filesystem::path result;
  EXPECT_NONFATAL_FAILURE(result = put("../" + ESCAPE, "x"), "would leave");
  EXPECT_TRUE(result.empty());
  EXPECT_FALSE(
      std::filesystem::exists(std::filesystem::symlink_status(root_.parent_path() / ESCAPE)));
}

/** @test A ".." inside a path is refused: empty path, nothing created outside the tree */
TEST_F(ScratchTreeTest, RefusesADotDotInsideThePath) {
  const std::string ESCAPE = root_.filename().string() + ".escape";
  std::filesystem::path result;
  EXPECT_NONFATAL_FAILURE(result = link("/sys/../../" + ESCAPE, "target"), "would leave");
  EXPECT_TRUE(result.empty());
  EXPECT_FALSE(
      std::filesystem::exists(std::filesystem::symlink_status(root_.parent_path() / ESCAPE)));
}

/** @test Two refused calls fail once each and create nothing in the tree */
TEST_F(ScratchTreeTest, TwoRefusedCallsFailOnceEachAndCreateNothing) {
  EXPECT_NONFATAL_FAILURE(dir("../one"), "would leave");
  EXPECT_NONFATAL_FAILURE(dir("../two"), "would leave");
  EXPECT_TRUE(std::filesystem::is_empty(root_));
}

/* ----------------------------- Containment Tests ----------------------------- */

/** @test A link to an outside directory is created, but no mutation follows it */
TEST_F(ScratchTreeTest, DoesNotMutateThroughAnEscapingDirectoryLink) {
  const OutsideTree OUTSIDE;
  ASSERT_TRUE(OUTSIDE.valid());
  ASSERT_FALSE(link("escape", OUTSIDE.path().string()).empty());
  EXPECT_NONFATAL_FAILURE(dir("escape/new-directory"), "symbolic link");
  EXPECT_NONFATAL_FAILURE(put("escape/sentinel", "overwritten"), "symbolic link");
  EXPECT_NONFATAL_FAILURE(link("escape/new-link", "sentinel"), "symbolic link");
  EXPECT_EQ(OUTSIDE.sentinel(), "original");
  EXPECT_FALSE(std::filesystem::exists(OUTSIDE.path() / "new-directory"));
  EXPECT_FALSE(std::filesystem::is_symlink(OUTSIDE.path() / "new-link"));
}

/** @test A link to an outside file is not written through */
TEST_F(ScratchTreeTest, DoesNotWriteThroughAnEscapingFileLink) {
  const OutsideTree OUTSIDE;
  ASSERT_TRUE(OUTSIDE.valid());
  ASSERT_FALSE(link("leaf", (OUTSIDE.path() / "sentinel").string()).empty());
  EXPECT_NONFATAL_FAILURE(put("leaf", "overwritten"), "symbolic link");
  EXPECT_EQ(OUTSIDE.sentinel(), "original");
}

/** @test A relative link that leaves the tree is refused the same way */
TEST_F(ScratchTreeTest, DoesNotMutateThroughARelativeEscapingLink) {
  const OutsideTree OUTSIDE;
  ASSERT_TRUE(OUTSIDE.valid());
  ASSERT_FALSE(link("up", "../" + OUTSIDE.path().filename().string()).empty());
  EXPECT_NONFATAL_FAILURE(put("up/sentinel", "overwritten"), "symbolic link");
  EXPECT_NONFATAL_FAILURE(dir("up/new-directory"), "symbolic link");
  EXPECT_EQ(OUTSIDE.sentinel(), "original");
  EXPECT_FALSE(std::filesystem::exists(OUTSIDE.path() / "new-directory"));
}

/** @test An internal link is read through by the code under test; its target is written by its real
 * path */
TEST_F(ScratchTreeTest, InternalLinksAreForReadingNotWriting) {
  dir("/sys/devices/platform/can0");
  link("/sys/class/net/can0/device", "../../../devices/platform/can0");
  put("/sys/devices/platform/can0/uevent", "DRIVER=can\n");
  EXPECT_EQ(readBytes(root_ / "sys/class/net/can0/device/uevent"), "DRIVER=can\n");
  EXPECT_NONFATAL_FAILURE(put("/sys/class/net/can0/device/uevent", "x"), "symbolic link");
  EXPECT_EQ(readBytes(root_ / "sys/devices/platform/can0/uevent"), "DRIVER=can\n");
}

/* ----------------------------- Cleanup Tests ----------------------------- */

/** @test TearDown() removes the whole tree: files, directories and links */
TEST_F(ScratchTreeTest, TearDownRemovesTheTree) {
  put("/sys/block/mmcblk0/size", "500695040\n");
  dir("/sys/devices/platform/can0");
  link("/sys/class/net/can0/device", "../../../devices/platform/can0");
  const std::filesystem::path ROOT = root_;
  ScratchTreeTest::TearDown();
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::symlink_status(ROOT)));
  // The fixture's own TearDown() runs after the body and needs its tree back
  ScratchTreeTest::SetUp();
  EXPECT_NE(root_, ROOT);
}

/** @test Cleanup that leaves anything behind fails the test, naming the tree */
TEST_F(ScratchTreeTest, CleanupFailureIsReported) {
#ifdef _WIN32
  GTEST_SKIP() << "POSIX permission bits";
#else
  if (::geteuid() == 0) {
    GTEST_SKIP() << "root ignores directory permissions";
  }
  put("locked/child", "keep me until cleanup");
  const std::filesystem::path LOCKED = root_ / "locked";
  ASSERT_EQ(::chmod(LOCKED.c_str(), 0500), 0);

  ::testing::TestPartResultArray results;
  {
    ::testing::ScopedFakeTestPartResultReporter capture(
        ::testing::ScopedFakeTestPartResultReporter::INTERCEPT_ONLY_CURRENT_THREAD, &results);
    ScratchTreeTest::TearDown();
  }
  const bool REMAINED = std::filesystem::exists(LOCKED / "child");
  ASSERT_EQ(::chmod(LOCKED.c_str(), 0700), 0);
  EXPECT_TRUE(REMAINED) << "the permission failure was not exercised";
  ASSERT_EQ(results.size(), 1);
  EXPECT_NE(std::string(results.GetTestPartResult(0).message()).find("scratch tree not removed"),
            std::string::npos);
  // The fixture's own TearDown() runs after the body, with the permission
  // restored, and removes the tree
#endif
}
