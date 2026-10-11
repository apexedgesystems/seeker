/**
 * @file ScratchDirectory_uTest.cpp
 * @brief Unit tests for seeker::test_support::ScratchDirectory.
 *
 * Notes:
 *  - Each owner under test is created beneath the fixture's own tree, so every
 *    candidate, sentinel and leftover these tests make goes with that tree.
 *  - The permission case skips as root, where the kernel ignores mode bits.
 */
#include "src/helpers/utst/inc/ScratchDirectory.hpp"

#include "src/helpers/utst/inc/ScratchTree.hpp"

#include <gtest/gtest.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

using seeker::test_support::ScratchDirectory;
using seeker::test_support::ScratchTreeTest;

namespace {

/// Restores the process's working directory when destroyed.
class RestoreCurrentPath {
public:
  RestoreCurrentPath() : original_(std::filesystem::current_path()) {}
  ~RestoreCurrentPath() {
    std::error_code ec;
    std::filesystem::current_path(original_, ec);
  }
  RestoreCurrentPath(const RestoreCurrentPath&) = delete;
  RestoreCurrentPath& operator=(const RestoreCurrentPath&) = delete;

private:
  std::filesystem::path original_;
};

/// Whole file, read in binary mode.
std::string readBytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// The serial at the end of an owner's directory name ("label.pid-serial").
unsigned serialOf(const std::filesystem::path& owned) {
  const std::string NAME = owned.filename().string();
  return static_cast<unsigned>(std::stoul(NAME.substr(NAME.rfind('-') + 1)));
}

/// The name an owner with the same label would get for `serial`.
std::filesystem::path siblingWithSerial(const std::filesystem::path& owned, unsigned serial) {
  const std::string NAME = owned.filename().string();
  return owned.parent_path() / (NAME.substr(0, NAME.rfind('-') + 1) + std::to_string(serial));
}

} // namespace

/// Owners under test live beneath the fixture's tree.
class ScratchDirectoryTest : public ScratchTreeTest {};

/* ----------------------------- Ownership Tests ----------------------------- */

/** @test The directory is claimed under the parent and named label.pid-serial */
TEST_F(ScratchDirectoryTest, ClaimsADirectoryNamedAfterTheLabel) {
  const ScratchDirectory OWNER(root_, "Suite.Case");
  ASSERT_TRUE(OWNER.valid()) << OWNER.error();
  EXPECT_EQ(OWNER.path().parent_path(), root_);
  EXPECT_TRUE(std::filesystem::is_directory(OWNER.path()));
  EXPECT_EQ(OWNER.path().filename().string().rfind("Suite.Case.", 0), 0U);
  EXPECT_TRUE(OWNER.error().empty());
}

/** @test Two owners with the same label get distinct directories */
TEST_F(ScratchDirectoryTest, SameLabelTwiceClaimsDistinctDirectories) {
  const ScratchDirectory A(root_, "same");
  const ScratchDirectory B(root_, "same");
  ASSERT_TRUE(A.valid()) << A.error();
  ASSERT_TRUE(B.valid()) << B.error();
  EXPECT_NE(A.path(), B.path());
  std::ofstream(A.path() / "mine") << "a";
  EXPECT_FALSE(std::filesystem::exists(B.path() / "mine"));
}

/** @test An existing candidate is skipped, never removed: its contents survive */
TEST_F(ScratchDirectoryTest, ExistingCandidateIsSkippedNotRemoved) {
  const ScratchDirectory FIRST(root_, "stale");
  ASSERT_TRUE(FIRST.valid()) << FIRST.error();
  const unsigned NEXT = serialOf(FIRST.path()) + 1;
  const std::filesystem::path STALE = siblingWithSerial(FIRST.path(), NEXT);
  ASSERT_TRUE(std::filesystem::create_directory(STALE));
  std::ofstream(STALE / "sentinel") << "keep";

  const ScratchDirectory SECOND(root_, "stale");
  ASSERT_TRUE(SECOND.valid()) << SECOND.error();
  EXPECT_NE(SECOND.path(), STALE);
  EXPECT_EQ(serialOf(SECOND.path()), NEXT + 1);
  EXPECT_EQ(readBytes(STALE / "sentinel"), "keep");
}

/** @test A retained directory survives its owner and later owners with the same label */
TEST_F(ScratchDirectoryTest, RetainedDirectorySurvivesLaterOwners) {
  std::filesystem::path kept;
  {
    ScratchDirectory owner(root_, "retained");
    ASSERT_TRUE(owner.valid()) << owner.error();
    std::ofstream(owner.path() / "marker") << "evidence";
    owner.retain();
    kept = owner.path();
  }
  EXPECT_EQ(readBytes(kept / "marker"), "evidence");
  {
    const ScratchDirectory LATER(root_, "retained");
    ASSERT_TRUE(LATER.valid()) << LATER.error();
    EXPECT_NE(LATER.path(), kept);
  }
  EXPECT_EQ(readBytes(kept / "marker"), "evidence");
}

/* ----------------------------- Removal Tests ----------------------------- */

/** @test The destructor removes an owned tree; remove() reports success and clears the path */
TEST_F(ScratchDirectoryTest, DestructorAndRemoveDeleteTheTree) {
  std::filesystem::path gone;
  {
    ScratchDirectory owner(root_, "temporary");
    ASSERT_TRUE(owner.valid()) << owner.error();
    std::filesystem::create_directories(owner.path() / "a/b");
    std::ofstream(owner.path() / "a/b/c") << "x";
    gone = owner.path();
  }
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::symlink_status(gone)));

  ScratchDirectory owner(root_, "temporary");
  ASSERT_TRUE(owner.valid()) << owner.error();
  const std::filesystem::path P = owner.path();
  std::error_code ec;
  EXPECT_TRUE(owner.remove(ec));
  EXPECT_FALSE(ec);
  EXPECT_FALSE(owner.valid());
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::symlink_status(P)));
}

/** @test remove() reports a tree it could not delete and keeps owning the path */
TEST_F(ScratchDirectoryTest, RemoveReportsWhatItCannotDelete) {
#ifdef _WIN32
  GTEST_SKIP() << "POSIX permission bits";
#else
  if (::geteuid() == 0) {
    GTEST_SKIP() << "root ignores directory permissions";
  }
  ScratchDirectory owner(root_, "locked");
  ASSERT_TRUE(owner.valid()) << owner.error();
  const std::filesystem::path LOCKED = owner.path() / "locked";
  std::filesystem::create_directory(LOCKED);
  std::ofstream(LOCKED / "child") << "x";
  ASSERT_EQ(::chmod(LOCKED.c_str(), 0500), 0);

  std::error_code ec;
  EXPECT_FALSE(owner.remove(ec));
  EXPECT_TRUE(ec);
  EXPECT_TRUE(owner.valid());
  EXPECT_TRUE(std::filesystem::exists(LOCKED / "child"));

  ASSERT_EQ(::chmod(LOCKED.c_str(), 0700), 0);
  EXPECT_TRUE(owner.remove(ec)) << ec.message();
#endif
}

/* ----------------------------- Parent and Label Tests ----------------------------- */

/** @test A relative parent is resolved at construction: after a change of directory the owner
 *        still removes the tree it claimed and leaves an identically named tree alone */
TEST_F(ScratchDirectoryTest, RelativeParentKeepsTheClaimedTreeAfterChdir) {
  const RestoreCurrentPath RESTORE;
  const std::filesystem::path A = dir("a");
  const std::filesystem::path B = dir("b");
  std::filesystem::current_path(A);
  ScratchDirectory owner(std::filesystem::path("work"), "case");
  ASSERT_TRUE(owner.valid()) << owner.error();
  const std::filesystem::path CLAIMED = owner.path();
  EXPECT_TRUE(CLAIMED.is_absolute());
  EXPECT_EQ(CLAIMED.parent_path(), A / "work");
  std::ofstream(CLAIMED / "owned") << "owned";
  const std::filesystem::path UNRELATED = B / "work" / CLAIMED.filename();
  std::filesystem::create_directories(UNRELATED);
  std::ofstream(UNRELATED / "sentinel") << "unrelated";

  std::filesystem::current_path(B);
  std::error_code ec;
  EXPECT_TRUE(owner.remove(ec)) << ec.message();
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::symlink_status(CLAIMED)));
  EXPECT_EQ(readBytes(UNRELATED / "sentinel"), "unrelated");
}

/** @test An absolute parent behaves the same across a change of directory (control) */
TEST_F(ScratchDirectoryTest, AbsoluteParentKeepsTheClaimedTreeAfterChdir) {
  const RestoreCurrentPath RESTORE;
  const std::filesystem::path A = dir("a");
  const std::filesystem::path B = dir("b");
  std::filesystem::current_path(A);
  ScratchDirectory owner(A / "work", "case");
  ASSERT_TRUE(owner.valid()) << owner.error();
  const std::filesystem::path CLAIMED = owner.path();
  std::ofstream(CLAIMED / "owned") << "owned";
  const std::filesystem::path UNRELATED = B / "work" / CLAIMED.filename();
  std::filesystem::create_directories(UNRELATED);
  std::ofstream(UNRELATED / "sentinel") << "unrelated";

  std::filesystem::current_path(B);
  std::error_code ec;
  EXPECT_TRUE(owner.remove(ec)) << ec.message();
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::symlink_status(CLAIMED)));
  EXPECT_EQ(readBytes(UNRELATED / "sentinel"), "unrelated");
}

/** @test A parent that is not a directory gives an invalid owner with an error and no path */
TEST_F(ScratchDirectoryTest, UnusableParentGivesAnInvalidOwner) {
  const std::filesystem::path FILE = put("a-file", "not a directory");
  const ScratchDirectory OWNER(FILE, "under-a-file");
  EXPECT_FALSE(OWNER.valid());
  EXPECT_TRUE(OWNER.path().empty());
  EXPECT_FALSE(OWNER.error().empty());
}

/** @test A missing parent is created, and a parent with a space in its name works */
TEST_F(ScratchDirectoryTest, CreatesAMissingParentAndAcceptsSpaces) {
  const std::filesystem::path PARENT = root_ / "new parent/with space";
  const ScratchDirectory OWNER(PARENT, "spaced");
  ASSERT_TRUE(OWNER.valid()) << OWNER.error();
  EXPECT_EQ(OWNER.path().parent_path(), PARENT);
  EXPECT_TRUE(std::filesystem::is_directory(OWNER.path()));
}

/** @test The label is sanitized and bounded; bounded labels still get distinct directories */
TEST_F(ScratchDirectoryTest, LabelIsSanitizedAndBounded) {
  const std::string LONG(2 * ScratchDirectory::LABEL_MAX, 'x');
  const ScratchDirectory A(root_, LONG + "/tail one");
  const ScratchDirectory B(root_, LONG + "/tail two");
  ASSERT_TRUE(A.valid()) << A.error();
  ASSERT_TRUE(B.valid()) << B.error();
  EXPECT_NE(A.path(), B.path());
  const std::string NAME = A.path().filename().string();
  EXPECT_EQ(NAME.find('/'), std::string::npos);
  EXPECT_EQ(NAME.find(' '), std::string::npos);
  EXPECT_LE(NAME.size(), ScratchDirectory::LABEL_MAX + 32);

  const ScratchDirectory EMPTY(root_, "");
  ASSERT_TRUE(EMPTY.valid()) << EMPTY.error();
  EXPECT_EQ(EMPTY.path().filename().string().rfind("scratch.", 0), 0U);
}
