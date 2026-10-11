# ==============================================================================
# Unit Tests: seeker_storage (5 modules)
# ==============================================================================

include(seeker/All)

set(UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/BlockDeviceInfo_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/MountInfo_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/IoScheduler_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/IoStats_uTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/StorageBench_uTest.cpp"
)

# The StorageBench suites that run benchmarks do real I/O and check wall time
# against a budget, so they run in the serial Timing lane.
seeker_add_gtest(
  TARGET
  TestSeekerStorage
  SOURCES
  ${UNIT_TEST_SOURCES}
  LINK
  seeker_storage
  fmt::fmt
  seeker_test_support
  LABELS
  diagnostics
  storage
  TIMING_PATTERNS
  "^(SeqWrite|SeqRead|Fsync|RandRead|RandWrite)BenchTest\\."
  "^(BenchSuiteRun|TimeBudget|Cleanup|EdgeCase)Test\\."
)

# ------------------------------------------------------------------------------
# Syscall Interposition (TestSeekerStorageSyscalls)
# ------------------------------------------------------------------------------

# The tests observe the benchmarks' system calls through --wrap, which rewrites
# references only within one link, so StorageBench.cpp is compiled in here.
set(UNIT_TEST_SOURCES_SYSCALLS "${CMAKE_CURRENT_SOURCE_DIR}/StorageBenchSyscalls_uTest.cpp"
                               "${CMAKE_CURRENT_SOURCE_DIR}/../src/StorageBench.cpp"
)

seeker_add_gtest(
  TARGET
  TestSeekerStorageSyscalls
  SOURCES
  ${UNIT_TEST_SOURCES_SYSCALLS}
  LINK
  fmt::fmt
  seeker_scratch_tree
  LINK_OPTIONS
  "LINKER:--wrap=write"
  "LINKER:--wrap=read"
  "LINKER:--wrap=pread"
  "LINKER:--wrap=pwrite"
  "LINKER:--wrap=fdatasync"
  "LINKER:--wrap=fsync"
  "LINKER:--wrap=clock_gettime"
  "LINKER:--wrap=posix_fadvise"
  LABELS
  diagnostics
  storage
)
