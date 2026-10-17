#ifndef SEEKER_HELPERS_SCRATCH_TREE_HPP
#define SEEKER_HELPERS_SCRATCH_TREE_HPP
/**
 * @file ScratchTree.hpp
 * @brief gtest fixture base for tests that need a filesystem: one owned
 *        directory per test, the tree declared with put(), dir() and link().
 *
 * Filesystem-tests paradigm, reference revision 5: the gtest adapter over
 * ScratchDirectory. Copied verbatim between repos; only the namespace, the
 * include-guard prefix and the include path change.
 */

#include "src/helpers/utst/inc/ScratchDirectory.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace seeker::test_support {

/**
 * @brief A private directory per test under testing::TempDir() (TEST_TMPDIR,
 *        else TMPDIR, else the platform temp directory), owned by a
 *        ScratchDirectory labelled "<Suite>.<Test>".
 *
 * Derive a fixture from it and declare the tree the code under test reads
 * with put(), dir() and link(). A path is relative to root_; an absolute path
 * is mirrored under root_, so a test writes the path the code opens
 * ("/sys/block/sda/size"). A mutation is refused, as a test failure that
 * creates nothing, when the path has a ".." component or when any component
 * of the result is a symbolic link: the fixture never follows a link, so it
 * cannot write outside its tree through one. Links made with link() are for
 * the code under test to follow; their targets are populated by their real
 * paths. This covers the fixture's own mutations under a trusted temporary
 * parent; it is not a sandbox for the code under test.
 *
 * TearDown() removes the tree and fails the test if anything remains, unless
 * the test failed and TEST_KEEP_TMPDIR is set: then the tree is kept, its
 * path printed, and a later run claims a fresh directory instead of touching
 * it.
 */
class ScratchTreeTest : public ::testing::Test {
protected:
  std::filesystem::path root_{}; ///< The test's private directory.

  void SetUp() override {
    const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
    scratch_.emplace(std::filesystem::path(::testing::TempDir()),
                     std::string(info->test_suite_name()) + "." + info->name());
    ASSERT_TRUE(scratch_->valid()) << scratch_->error();
    root_ = scratch_->path();
  }

  void TearDown() override {
    if (!scratch_ || !scratch_->valid()) {
      return;
    }
    if (HasFailure() && std::getenv("TEST_KEEP_TMPDIR") != nullptr) {
      scratch_->retain();
      std::cerr << "scratch tree kept: " << root_ << '\n';
      return;
    }
    std::error_code ec;
    EXPECT_TRUE(scratch_->remove(ec))
        << "scratch tree not removed: " << root_ << ": " << ec.message();
  }

  /// Creates directory `rel` and its parents; returns its path (empty if
  /// refused).
  std::filesystem::path dir(std::string_view rel) {
    const std::filesystem::path P = under(rel);
    if (P.empty()) {
      return P;
    }
    std::error_code ec;
    std::filesystem::create_directories(P, ec);
    EXPECT_FALSE(ec) << P << ": " << ec.message();
    return P;
  }

  /// Writes `text` as the exact bytes of file `rel`, creating its parents;
  /// returns its path (empty if refused).
  std::filesystem::path put(std::string_view rel, std::string_view text) {
    const std::filesystem::path P = under(rel);
    if (P.empty()) {
      return P;
    }
    std::error_code ec;
    std::filesystem::create_directories(P.parent_path(), ec);
    std::ofstream out(P, std::ios::binary);
    out << text;
    out.close();
    EXPECT_FALSE(out.fail()) << "cannot write " << P;
    return P;
  }

  /// Makes `rel` a symbolic link whose text is `target` (what the code under
  /// test will readlink()); creates its parents; returns the link's path
  /// (empty if refused).
  std::filesystem::path link(std::string_view rel, std::string_view target) {
    const std::filesystem::path P = under(rel);
    if (P.empty()) {
      return P;
    }
    std::error_code ec;
    std::filesystem::create_directories(P.parent_path(), ec);
    std::filesystem::create_symlink(std::filesystem::path(target), P, ec);
    EXPECT_FALSE(ec) << P << " -> " << target << ": " << ec.message();
    return P;
  }

private:
  std::optional<ScratchDirectory> scratch_{};

  /// root_ / rel, with an absolute rel mirrored under root_. A ".." component,
  /// or a symbolic link at any component of the result, fails the test and
  /// yields an empty path, so nothing is created.
  std::filesystem::path under(std::string_view rel) {
    if (root_.empty()) {
      ADD_FAILURE() << "no scratch tree (SetUp failed)";
      return {};
    }
    std::filesystem::path p = root_;
    for (const std::filesystem::path& part : std::filesystem::path(rel).relative_path()) {
      if (part == "..") {
        ADD_FAILURE() << "'" << rel << "' would leave " << root_;
        return {};
      }
      p /= part;
      std::error_code ec;
      if (std::filesystem::is_symlink(std::filesystem::symlink_status(p, ec))) {
        ADD_FAILURE() << "'" << rel << "' passes through the symbolic link " << p
                      << "; mutate its target by its real path";
        return {};
      }
    }
    return p;
  }
};

} // namespace seeker::test_support

#endif // SEEKER_HELPERS_SCRATCH_TREE_HPP
