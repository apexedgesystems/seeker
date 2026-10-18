# ==============================================================================
# seeker/Testing.cmake - Test infrastructure and coverage support
# ==============================================================================

include_guard(GLOBAL)
include(seeker/Core)

# ------------------------------------------------------------------------------
# seeker_add_gtest(...)
#
# GoogleTest executable in ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tests. Its tests
# are found in its sources (gtest_add_tests) and registered with CTest. CUDA
# sources are compiled through seeker_cuda_sources when the CUDA toolkit is
# found and the CUDA language enabled, and otherwise left out with their tests.
# Skipped on bare-metal builds.
#
# With ENABLE_COVERAGE, the test and each compiled project library in LINK (or
# COVERAGE_FOR) are instrumented, a <TARGET>_coverage test labelled Coverage
# runs the executable, and a test:library mapping is registered for the report.
# Header-only (INTERFACE), imported and alias targets in LINK are skipped.
#
# Timed tests get the Timing label and run serially, unless RESOURCE_LOCK
# already serializes them. TIMING_TESTS names and TIMING_PATTERNS that match no
# test (for example a CUDA test left out without CUDA) select nothing.
#
# Arguments:
#   TARGET           <name>           required
#   SOURCES          <files...>       required
#   CUDA             <files...>       optional, CUDA sources
#   LINK             <targets...>     optional, linked PRIVATE with
#                                     GTest::gtest_main and GTest::gmock
#   LINK_OPTIONS     <options...>     optional, PRIVATE linker options
#   INC              <dir>            optional, PRIVATE include directory
#   LABELS           <labels...>      optional, CTest labels of every test
#   WORKING_DIR      <dir>            optional (default: the binary directory)
#   RESOURCE_LOCK    <name>           optional, CTest RESOURCE_LOCK of every test
#   REQUIRES_THREADS <count>          optional, disables the tests on a host
#                                     with fewer hardware threads
#   TIMING_ALL                        optional flag: every test is timed
#   TIMING_TESTS     <names...>       optional, timed tests (Suite.Test)
#   TIMING_PATTERNS  <regex...>       optional, timed tests by name pattern
#   COVERAGE_FOR     <lib_target>     optional, replaces the libraries taken
#                                     from LINK for coverage
#   NO_COVERAGE                       optional flag: no coverage for this test
# ------------------------------------------------------------------------------
function (seeker_add_gtest)
  cmake_parse_arguments(
    GT "TIMING_ALL;NO_COVERAGE"
    "TARGET;INC;WORKING_DIR;RESOURCE_LOCK;COVERAGE_FOR;REQUIRES_THREADS"
    "SOURCES;CUDA;LINK;LINK_OPTIONS;LABELS;TIMING_TESTS;TIMING_PATTERNS" ${ARGN}
  )
  _seeker_check_arguments(seeker_add_gtest GT REQUIRED TARGET SOURCES)
  if (DEFINED GT_REQUIRES_THREADS AND NOT GT_REQUIRES_THREADS MATCHES "^[1-9][0-9]*$")
    message(
      FATAL_ERROR
        "seeker_add_gtest: REQUIRES_THREADS must be a positive integer, not '${GT_REQUIRES_THREADS}'"
    )
  endif ()

  # Skip on bare-metal
  if (CMAKE_SYSTEM_NAME STREQUAL "Generic")
    return()
  endif ()

  add_executable(${GT_TARGET})
  target_sources(${GT_TARGET} PRIVATE ${GT_SOURCES})

  if (GT_INC)
    target_include_directories(${GT_TARGET} PRIVATE "${GT_INC}")
  endif ()

  # CUDA sources
  if (GT_CUDA
      AND CUDAToolkit_FOUND
      AND CMAKE_CUDA_COMPILER
  )
    seeker_cuda_sources(${GT_TARGET} FILES ${GT_CUDA})
  endif ()

  # GTest linkage
  if (NOT TARGET GTest::gtest_main)
    find_package(GTest QUIET CONFIG)
    if (NOT TARGET GTest::gtest_main)
      find_package(GTest QUIET)
    endif ()
    if (NOT TARGET GTest::gtest_main)
      message(FATAL_ERROR "seeker_add_gtest: GTest::gtest_main not found")
    endif ()
  endif ()

  target_link_libraries(${GT_TARGET} PRIVATE GTest::gtest_main GTest::gmock ${GT_LINK})
  if (GT_LINK_OPTIONS)
    target_link_options(${GT_TARGET} PRIVATE ${GT_LINK_OPTIONS})
  endif ()

  set_target_properties(
    ${GT_TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tests"
  )

  # Coverage instrumentation (auto-detect from LINK or use COVERAGE_FOR override)
  set(_coverage_libs "")
  if (GT_NO_COVERAGE)
    # No instrumentation, no coverage test, no mapping
  elseif (ENABLE_COVERAGE AND TARGET seeker_coverage_flags)
    if (GT_COVERAGE_FOR)
      # Explicit override
      set(_coverage_libs ${GT_COVERAGE_FOR})
    else ()
      # Auto-detect from LINK: filter out imported and aliased targets
      foreach (_lib IN LISTS GT_LINK)
        if (NOT TARGET ${_lib})
          continue()
        endif ()
        # Skip aliased targets (seeker::* or any ALIAS)
        if (_lib MATCHES "^seeker::")
          continue()
        endif ()
        get_target_property(_aliased ${_lib} ALIASED_TARGET)
        if (_aliased)
          continue()
        endif ()
        # Skip imported targets (third-party like fmt::fmt, GTest::*)
        get_target_property(_imported ${_lib} IMPORTED)
        if (_imported)
          continue()
        endif ()
        # Skip header-only targets: no objects of their own to instrument or map
        get_target_property(_type ${_lib} TYPE)
        if (_type STREQUAL "INTERFACE_LIBRARY")
          continue()
        endif ()
        list(APPEND _coverage_libs ${_lib})
      endforeach ()
    endif ()

    # Instrument test executable and libraries
    if (_coverage_libs)
      target_link_libraries(${GT_TARGET} PRIVATE seeker::coverage_flags)

      foreach (_lib IN LISTS _coverage_libs)
        if (TARGET ${_lib})
          get_target_property(_lib_type ${_lib} TYPE)
          if (_lib_type STREQUAL "INTERFACE_LIBRARY")
            target_link_libraries(${_lib} INTERFACE seeker::coverage_flags)
          else ()
            target_link_libraries(${_lib} PRIVATE seeker::coverage_flags)
          endif ()

          # Register mapping for report generation
          if (COMMAND seeker_coverage_register)
            seeker_coverage_register(${GT_TARGET} ${_lib})
          endif ()
        endif ()
      endforeach ()
    endif ()
  endif ()

  # Test discovery
  include(GoogleTest)
  if (GT_WORKING_DIR)
    set(_working_dir "${GT_WORKING_DIR}")
  else ()
    set(_working_dir "${CMAKE_CURRENT_BINARY_DIR}")
  endif ()

  set(_discover_sources ${GT_SOURCES})
  if (GT_CUDA
      AND CUDAToolkit_FOUND
      AND CMAKE_CUDA_COMPILER
  )
    list(APPEND _discover_sources ${GT_CUDA})
  endif ()

  set(_all_tests "")
  gtest_add_tests(
    TARGET ${GT_TARGET}
    SOURCES ${_discover_sources}
    WORKING_DIRECTORY "${_working_dir}"
    TEST_LIST _all_tests
  )

  # Base labels
  if (GT_LABELS)
    set_property(
      TEST ${_all_tests}
      APPEND
      PROPERTY LABELS "${GT_LABELS}"
    )
  endif ()

  # Set working directory for all discovered tests
  set_tests_properties(${_all_tests} PROPERTIES WORKING_DIRECTORY "${_working_dir}")

  # Resource lock for tests that share system resources (ports, files, etc.)
  # Applied to ALL tests in the target, independent of timing sensitivity
  if (GT_RESOURCE_LOCK AND _all_tests)
    set_tests_properties(${_all_tests} PROPERTIES RESOURCE_LOCK "${GT_RESOURCE_LOCK}")
  endif ()

  # Hardware thread requirement - skip tests on systems with insufficient threads
  if (GT_REQUIRES_THREADS AND _all_tests)
    cmake_host_system_information(RESULT _hw_threads QUERY NUMBER_OF_LOGICAL_CORES)
    if (_hw_threads LESS GT_REQUIRES_THREADS)
      message(
        STATUS
          "[Skip] ${GT_TARGET}: requires ${GT_REQUIRES_THREADS} threads, system has ${_hw_threads}"
      )
      set_tests_properties(${_all_tests} PROPERTIES DISABLED TRUE)
    endif ()
  endif ()

  # Timing-sensitive test handling
  set(_timed_tests "")
  if (GT_TIMING_ALL)
    set(_timed_tests "${_all_tests}")
  else ()
    if (GT_TIMING_TESTS)
      foreach (_t IN LISTS GT_TIMING_TESTS)
        list(FIND _all_tests "${_t}" _idx)
        if (_idx GREATER -1)
          list(APPEND _timed_tests "${_t}")
        endif ()
      endforeach ()
    endif ()
    if (GT_TIMING_PATTERNS)
      foreach (_name IN LISTS _all_tests)
        foreach (_rx IN LISTS GT_TIMING_PATTERNS)
          if (_name MATCHES "${_rx}")
            list(APPEND _timed_tests "${_name}")
            break()
          endif ()
        endforeach ()
      endforeach ()
      list(REMOVE_DUPLICATES _timed_tests)
    endif ()
  endif ()

  if (_timed_tests)
    set_property(
      TEST ${_timed_tests}
      APPEND
      PROPERTY LABELS "Timing"
    )
    # Timing tests run serially unless RESOURCE_LOCK was already applied above
    if (NOT GT_RESOURCE_LOCK)
      set_tests_properties(${_timed_tests} PROPERTIES RUN_SERIAL TRUE)
    endif ()
  endif ()

  # Coverage test (when any coverage libraries detected)
  if (_coverage_libs)
    set(_cov_labels "Coverage")
    if (GT_LABELS)
      list(APPEND _cov_labels ${GT_LABELS})
    endif ()

    add_test(NAME ${GT_TARGET}_coverage COMMAND $<TARGET_FILE:${GT_TARGET}>)
    set_tests_properties(
      ${GT_TARGET}_coverage
      PROPERTIES LABELS "${_cov_labels}" WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" ENVIRONMENT
                 "LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/${GT_TARGET}.coverage.profraw"
    )
  endif ()

  # Summary
  list(LENGTH _all_tests _disc_len)
  list(LENGTH _timed_tests _timed_len)
  set(_req_threads_msg "")
  if (GT_REQUIRES_THREADS)
    set(_req_threads_msg " threads=${GT_REQUIRES_THREADS}")
  endif ()
  message(STATUS "[Test] target=${GT_TARGET} tests=${_disc_len} timed=${_timed_len} "
                 "lock='${GT_RESOURCE_LOCK}'${_req_threads_msg}"
  )
endfunction ()

# ------------------------------------------------------------------------------
# seeker_add_ptest(...)
#
# Performance test executable in ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ptests,
# built but not registered with CTest: run it by hand, for example
#   build/native-linux-debug/bin/ptests/<TARGET> --csv results.csv
# Links GoogleTest and, when found, the gperftools profiler and tcmalloc; the
# build RPATH reaches the library output directory. CUDA sources are compiled
# when CUDA is active and left out otherwise. Skipped on bare-metal builds.
#
# Arguments:
#   TARGET          <name>           required
#   SOURCES         <files...>       required
#   CUDA            <files...>       optional, CUDA sources
#   LINK            <targets...>     optional, linked PRIVATE
#   INC             <dir>            optional, PRIVATE include directory
# ------------------------------------------------------------------------------
function (seeker_add_ptest)
  cmake_parse_arguments(PT "" "TARGET;INC" "SOURCES;CUDA;LINK" ${ARGN})
  _seeker_check_arguments(seeker_add_ptest PT REQUIRED TARGET SOURCES)

  # Skip on bare-metal
  if (CMAKE_SYSTEM_NAME STREQUAL "Generic")
    return()
  endif ()

  add_executable(${PT_TARGET})
  target_sources(${PT_TARGET} PRIVATE ${PT_SOURCES})

  if (PT_INC)
    target_include_directories(${PT_TARGET} PRIVATE "${PT_INC}")
  endif ()

  # CUDA sources
  if (PT_CUDA
      AND CUDAToolkit_FOUND
      AND CMAKE_CUDA_COMPILER
  )
    seeker_cuda_sources(${PT_TARGET} FILES ${PT_CUDA})
  endif ()

  # GTest linkage
  if (NOT TARGET GTest::gtest_main)
    find_package(GTest QUIET CONFIG)
    if (NOT TARGET GTest::gtest_main)
      find_package(GTest QUIET)
    endif ()
    if (NOT TARGET GTest::gtest_main)
      message(FATAL_ERROR "seeker_add_ptest: GTest::gtest_main not found")
    endif ()
  endif ()

  # Build link list
  set(_link ${PT_LINK})

  # gperftools (optional)
  find_library(GPERF_PROFILER_LIB NAMES profiler)
  find_library(GPERF_TCMALLOC_LIB NAMES tcmalloc)
  if (GPERF_PROFILER_LIB)
    list(APPEND _link "${GPERF_PROFILER_LIB}")
  endif ()
  if (GPERF_TCMALLOC_LIB)
    list(APPEND _link "${GPERF_TCMALLOC_LIB}")
  endif ()

  target_link_libraries(${PT_TARGET} PRIVATE GTest::gtest_main GTest::gmock ${_link})

  # Output directory
  set_target_properties(
    ${PT_TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ptests"
  )

  # RPATH fixup for ptests layout
  set(_ptests_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ptests")
  set(_lib_dir "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
  if (NOT _lib_dir)
    set(_lib_dir "${CMAKE_BINARY_DIR}/lib")
  endif ()

  file(RELATIVE_PATH _rel_lib "${_ptests_dir}" "${_lib_dir}")
  string(REPLACE "\\" "/" _rel_lib "${_rel_lib}")

  get_target_property(_cur_rpath ${PT_TARGET} BUILD_RPATH)
  if (NOT _cur_rpath)
    set(_cur_rpath "")
  endif ()

  set_target_properties(
    ${PT_TARGET}
    PROPERTIES BUILD_RPATH "${_cur_rpath};\$ORIGIN/${_rel_lib};${_lib_dir}"
               SKIP_BUILD_RPATH OFF
               BUILD_WITH_INSTALL_RPATH OFF
  )

  set_property(
    TARGET ${PT_TARGET}
    APPEND_STRING
    PROPERTY LINK_FLAGS " -Wl,-rpath-link,${_lib_dir}"
  )

  # NOTE: No gtest_add_tests() - ptests are not registered with CTest

  # Summary
  set(_gperf "off")
  if (GPERF_PROFILER_LIB OR GPERF_TCMALLOC_LIB)
    set(_gperf "on")
  endif ()
  message(
    STATUS "[Perf] target=${PT_TARGET} out=ptests/ gperftools=${_gperf} (manual execution only)"
  )
endfunction ()

# ------------------------------------------------------------------------------
# seeker_add_devtest(...)
#
# Development test executable in ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/dtests,
# built but not registered with CTest, for tests that need data files or
# hardware a CTest run does not have: run it by hand, for example
#   build/native-linux-debug/bin/dtests/<TARGET> --gtest_filter="*Pattern*"
# Links GoogleTest. CUDA sources are compiled when CUDA is active and left out
# otherwise. Skipped on bare-metal builds.
#
# Arguments:
#   TARGET          <name>           required
#   SOURCES         <files...>       required
#   CUDA            <files...>       optional, CUDA sources
#   LINK            <targets...>     optional, linked PRIVATE
#   INC             <dir>            optional, PRIVATE include directory
# ------------------------------------------------------------------------------
function (seeker_add_devtest)
  cmake_parse_arguments(DT "" "TARGET;INC" "SOURCES;CUDA;LINK" ${ARGN})
  _seeker_check_arguments(seeker_add_devtest DT REQUIRED TARGET SOURCES)

  # Skip on bare-metal
  if (CMAKE_SYSTEM_NAME STREQUAL "Generic")
    return()
  endif ()

  add_executable(${DT_TARGET})
  target_sources(${DT_TARGET} PRIVATE ${DT_SOURCES})

  if (DT_INC)
    target_include_directories(${DT_TARGET} PRIVATE "${DT_INC}")
  endif ()

  # CUDA sources
  if (DT_CUDA
      AND CUDAToolkit_FOUND
      AND CMAKE_CUDA_COMPILER
  )
    seeker_cuda_sources(${DT_TARGET} FILES ${DT_CUDA})
  endif ()

  # GTest linkage
  if (NOT TARGET GTest::gtest_main)
    find_package(GTest QUIET CONFIG)
    if (NOT TARGET GTest::gtest_main)
      find_package(GTest QUIET)
    endif ()
    if (NOT TARGET GTest::gtest_main)
      message(FATAL_ERROR "seeker_add_devtest: GTest::gtest_main not found")
    endif ()
  endif ()

  target_link_libraries(${DT_TARGET} PRIVATE GTest::gtest_main GTest::gmock ${DT_LINK})

  set_target_properties(
    ${DT_TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/dtests"
  )

  # NOTE: No gtest_add_tests() - dev tests are not registered with CTest

  message(STATUS "[Dev] target=${DT_TARGET} out=dtests/ (manual execution only)")
endfunction ()
