#ifndef SEEKER_HELPERS_FILESYSTEM_ROOT_TEST_HPP
#define SEEKER_HELPERS_FILESYSTEM_ROOT_TEST_HPP
/**
 * @file FilesystemRootTest.hpp
 * @brief gtest fixture that makes a test's scratch tree the file helpers'
 *        filesystem root.
 *
 * For code that reads fixed absolute paths (/proc, /sys) through the file
 * helpers: the test declares the tree with put(), dir() and link() at the
 * paths the code opens, and the code reads it instead of the live system.
 * SetUp() points setFilesystemRoot() at root_ once the tree is claimed;
 * TearDown() restores the real root before the tree is removed, so no root
 * outlives its test. The test executable and the seeker libraries share one
 * root only under ordinary shared linkage with default symbol visibility
 * (see Files.hpp).
 */

#include "src/helpers/inc/Files.hpp"
#include "src/helpers/utst/inc/ScratchTree.hpp"

#include <gtest/gtest.h>

namespace seeker {
namespace test_support {

/// The scratch tree is the process's filesystem root for one test.
class FilesystemRootTest : public ScratchTreeTest {
protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(ScratchTreeTest::SetUp());
    ASSERT_TRUE(seeker::helpers::files::setFilesystemRoot(root_.c_str()));
  }

  void TearDown() override {
    // nullptr always succeeds: back to the real root
    (void)seeker::helpers::files::setFilesystemRoot(nullptr);
    ScratchTreeTest::TearDown();
  }
};

} // namespace test_support
} // namespace seeker

#endif // SEEKER_HELPERS_FILESYSTEM_ROOT_TEST_HPP
