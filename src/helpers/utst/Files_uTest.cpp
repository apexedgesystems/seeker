/**
 * @file Files_uTest.cpp
 * @brief Unit tests for the filesystem root of seeker::helpers::files.
 *
 * Notes:
 *  - Tests that change the root derive from FilesystemRootTest: the test's
 *    scratch tree is the root, and TearDown() restores the real root
 *    whatever the test set.
 *  - Live tests read the real /proc and /dev with the root unset.
 */

#include "src/helpers/inc/Files.hpp"
#include "src/helpers/utst/inc/FilesystemRootTest.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using seeker::helpers::files::filesystemRoot;
using seeker::helpers::files::isCharDevice;
using seeker::helpers::files::isDirectory;
using seeker::helpers::files::pathExists;
using seeker::helpers::files::readFileInt;
using seeker::helpers::files::readFileInt64;
using seeker::helpers::files::readFileLine;
using seeker::helpers::files::readFileToBuffer;
using seeker::helpers::files::readFileUint64;
using seeker::helpers::files::RESOLVED_PATH_SIZE;
using seeker::helpers::files::resolvePath;
using seeker::helpers::files::setFilesystemRoot;
using seeker::test_support::FilesystemRootTest;

namespace {

/// A file the kernel always provides, compared byte for byte below.
constexpr const char* LIVE_PROC_FILE = "/proc/version";

/// Whole file read directly, not through the helpers.
std::string readDirect(const char* path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// What readFileToBuffer() keeps of a file: its text without trailing whitespace.
std::string stripTrailing(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' ||
                           text.back() == '\t')) {
    text.pop_back();
  }
  return text;
}

/// Read a file through the helpers into a string.
std::string readHelper(const char* path) {
  std::array<char, 4096> buf{};
  const std::size_t LEN = readFileToBuffer(path, buf.data(), buf.size());
  return std::string(buf.data(), LEN);
}

} // namespace

/* ----------------------------- Default Construction ----------------------------- */

/** @test Outside a root fixture the root is the real one */
TEST(FilesystemRootDefaultTest, RootIsTheRealRoot) { EXPECT_STREQ(filesystemRoot(), ""); }

/* ----------------------------- setFilesystemRoot Tests ----------------------------- */

class SetFilesystemRootTest : public FilesystemRootTest {};

/** @test The fixture makes the test's scratch tree the root */
TEST_F(SetFilesystemRootTest, FixturePointsTheRootAtTheScratchTree) {
  EXPECT_EQ(std::string(filesystemRoot()), root_.string());
}

/** @test "/" selects the real root, the same as "" */
TEST_F(SetFilesystemRootTest, SlashSelectsTheRealRoot) {
  ASSERT_TRUE(setFilesystemRoot("/"));
  EXPECT_STREQ(filesystemRoot(), "");
}

/** @test nullptr and "" select the real root */
TEST_F(SetFilesystemRootTest, NullAndEmptySelectTheRealRoot) {
  ASSERT_TRUE(setFilesystemRoot(nullptr));
  EXPECT_STREQ(filesystemRoot(), "");

  ASSERT_TRUE(setFilesystemRoot(root_.c_str()));
  ASSERT_TRUE(setFilesystemRoot(""));
  EXPECT_STREQ(filesystemRoot(), "");
}

/** @test Trailing slashes are dropped */
TEST_F(SetFilesystemRootTest, DropsTrailingSlashes) {
  const std::string WITH_SLASHES = root_.string() + "//";
  ASSERT_TRUE(setFilesystemRoot(WITH_SLASHES.c_str()));
  EXPECT_EQ(std::string(filesystemRoot()), root_.string());
}

/** @test A relative root is rejected and the current root kept */
TEST_F(SetFilesystemRootTest, RejectsARelativeRoot) {
  EXPECT_FALSE(setFilesystemRoot("relative/dir"));
  EXPECT_EQ(std::string(filesystemRoot()), root_.string());
}

/** @test A root with no room for its terminator is rejected; one byte shorter fits */
TEST_F(SetFilesystemRootTest, RejectsARootLongerThanAPath) {
  const std::string TOO_LONG = "/" + std::string(RESOLVED_PATH_SIZE - 1, 'a');
  EXPECT_FALSE(setFilesystemRoot(TOO_LONG.c_str()));
  EXPECT_EQ(std::string(filesystemRoot()), root_.string());

  const std::string LONGEST = "/" + std::string(RESOLVED_PATH_SIZE - 2, 'a');
  EXPECT_TRUE(setFilesystemRoot(LONGEST.c_str()));
  EXPECT_EQ(std::strlen(filesystemRoot()), RESOLVED_PATH_SIZE - 1);
}

/* ----------------------------- resolvePath Tests ----------------------------- */

class ResolvePathTest : public FilesystemRootTest {};

/** @test With the real root the path itself comes back, not a copy */
TEST_F(ResolvePathTest, RealRootReturnsThePathItself) {
  ASSERT_TRUE(setFilesystemRoot(nullptr));
  std::array<char, 16> out{};
  EXPECT_EQ(resolvePath(LIVE_PROC_FILE, out.data(), out.size()), LIVE_PROC_FILE);
}

/** @test An absolute path is resolved under the root */
TEST_F(ResolvePathTest, PrefixesAnAbsolutePathWithTheRoot) {
  std::array<char, RESOLVED_PATH_SIZE> out{};
  const char* resolved = resolvePath("/sys/block", out.data(), out.size());
  ASSERT_EQ(resolved, out.data());
  EXPECT_EQ(std::string(resolved), root_.string() + "/sys/block");
}

/** @test A relative path is left alone */
TEST_F(ResolvePathTest, LeavesARelativePathAlone) {
  constexpr const char* RELATIVE = "sys/block";
  std::array<char, RESOLVED_PATH_SIZE> out{};
  EXPECT_EQ(resolvePath(RELATIVE, out.data(), out.size()), RELATIVE);
}

/** @test A resolved path that fits exactly is returned; one byte less is rejected */
TEST_F(ResolvePathTest, RejectsAResultThatDoesNotFit) {
  constexpr const char* PATH = "/ab";
  const std::size_t EXACT = root_.string().size() + std::strlen(PATH) + 1;
  std::vector<char> out(EXACT);

  EXPECT_EQ(resolvePath(PATH, out.data(), EXACT), out.data());
  EXPECT_EQ(resolvePath(PATH, out.data(), EXACT - 1), nullptr);
}

/** @test A null path resolves to null */
TEST_F(ResolvePathTest, NullPathResolvesToNull) {
  std::array<char, 16> out{};
  EXPECT_EQ(resolvePath(nullptr, out.data(), out.size()), nullptr);
}

/* ----------------------------- Helpers Under a Root ----------------------------- */

class FilesUnderRootTest : public FilesystemRootTest {};

/** @test readFileToBuffer reads the tree's file, not the live one */
TEST_F(FilesUnderRootTest, ReadFileToBufferReadsTheTree) {
  put(LIVE_PROC_FILE, "fixture kernel\n");
  EXPECT_EQ(readHelper(LIVE_PROC_FILE), "fixture kernel");
}

/** @test readFileLine returns the first line of the tree's file */
TEST_F(FilesUnderRootTest, ReadFileLineReadsTheTree) {
  put("/sys/fixture/lines", "first line\nsecond line\n");
  std::array<char, 64> line{};
  EXPECT_EQ(readFileLine("/sys/fixture/lines", line), std::strlen("first line"));
  EXPECT_STREQ(line.data(), "first line");
}

/** @test The integer readers parse the tree's files */
TEST_F(FilesUnderRootTest, ReadFileIntFamilyReadsTheTree) {
  put("/sys/fixture/int", "-42\n");
  put("/sys/fixture/int64", "-9000000000\n");
  put("/sys/fixture/uint64", "18000000000000000000\n");

  EXPECT_EQ(readFileInt("/sys/fixture/int"), -42);
  EXPECT_EQ(readFileInt64("/sys/fixture/int64"), -9000000000LL);
  EXPECT_EQ(readFileUint64("/sys/fixture/uint64"), 18000000000000000000ULL);
}

/** @test The path checks see the tree, not the live system */
TEST_F(FilesUnderRootTest, PathChecksSeeTheTree) {
  put("/sys/fixture/file", "1\n");
  dir("/sys/fixture/subdir");

  EXPECT_TRUE(pathExists("/sys/fixture/file"));
  EXPECT_FALSE(pathExists("/proc/self/stat")) << "exists live, not in the tree";
  EXPECT_TRUE(isDirectory("/sys/fixture/subdir"));
  EXPECT_FALSE(isDirectory("/sys/fixture/file"));
  EXPECT_FALSE(isCharDevice("/dev/null")) << "a character device live, not in the tree";
}

/** @test A resolved path that does not fit fails like a missing file, never reading the live one */
TEST_F(FilesUnderRootTest, HelpersFailWhenTheResolvedPathDoesNotFit) {
  // Fits as a root, but no live path below appended to it fits RESOLVED_PATH_SIZE
  const std::string LONG_ROOT = "/" + std::string(RESOLVED_PATH_SIZE - 4, 'a');
  ASSERT_TRUE(setFilesystemRoot(LONG_ROOT.c_str()));
  constexpr std::int32_t DEFAULT_INT = 7;

  std::array<char, 64> buf{};
  buf[0] = 'x';
  EXPECT_EQ(readFileToBuffer(LIVE_PROC_FILE, buf.data(), buf.size()), 0U);
  EXPECT_EQ(buf[0], '\0');
  std::array<char, 64> line{};
  EXPECT_EQ(readFileLine(LIVE_PROC_FILE, line), 0U);
  EXPECT_EQ(readFileInt("/proc/sys/kernel/pid_max", DEFAULT_INT), DEFAULT_INT);
  EXPECT_EQ(readFileInt64("/proc/sys/kernel/pid_max", DEFAULT_INT), DEFAULT_INT);
  EXPECT_EQ(readFileUint64("/proc/sys/kernel/pid_max", DEFAULT_INT),
            static_cast<std::uint64_t>(DEFAULT_INT));
  EXPECT_FALSE(pathExists("/proc"));
  EXPECT_FALSE(isDirectory("/proc"));
  EXPECT_FALSE(isCharDevice("/dev/null"));
}

/* ----------------------------- Live System Tests ----------------------------- */

class FilesRootIdentityTest : public FilesystemRootTest {};

/** @test A real /proc file reads the same bytes through the real root, through "/", and directly */
TEST_F(FilesRootIdentityTest, RealRootAndSlashReadTheRealFile) {
  const std::string DIRECT = stripTrailing(readDirect(LIVE_PROC_FILE));
  if (DIRECT.empty()) {
    GTEST_SKIP() << LIVE_PROC_FILE << " is not readable here";
  }

  ASSERT_TRUE(setFilesystemRoot(nullptr));
  const std::string VIA_REAL_ROOT = readHelper(LIVE_PROC_FILE);
  ASSERT_TRUE(setFilesystemRoot("/"));
  const std::string VIA_SLASH = readHelper(LIVE_PROC_FILE);

  EXPECT_EQ(VIA_REAL_ROOT, DIRECT);
  EXPECT_EQ(VIA_SLASH, DIRECT);
}

/** @test With the root unset the helpers read the live system */
TEST(FilesLiveTest, RootUnsetReadsTheLiveSystem) {
  ASSERT_STREQ(filesystemRoot(), "");
  const std::string DIRECT = stripTrailing(readDirect(LIVE_PROC_FILE));
  if (DIRECT.empty()) {
    GTEST_SKIP() << LIVE_PROC_FILE << " is not readable here";
  }

  EXPECT_EQ(readHelper(LIVE_PROC_FILE), DIRECT);
  EXPECT_TRUE(pathExists("/proc/self/stat"));
  EXPECT_TRUE(isDirectory("/proc"));
  EXPECT_EQ(isCharDevice("/dev/null"), std::filesystem::is_character_file("/dev/null"));
}
