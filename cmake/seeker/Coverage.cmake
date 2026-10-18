# ==============================================================================
# seeker/Coverage.cmake - Code coverage infrastructure (LLVM source-based)
# ==============================================================================
#
# A mapping pairs a test executable with a library it covers. seeker_add_gtest
# maps a test to each shared library in its LINK that this project builds from
# C or C++ sources (or to its COVERAGE_FOR library), instruments both and adds
# a <test>_coverage CTest test labelled Coverage. Header-only (INTERFACE) and
# CUDA-only libraries are not mapped: the instrumentation covers C and C++
# compiles only, so they carry no coverage mapping of their own; their inline
# code is reported where a mapped library or test compiles it. Production
# sources compiled into a test are reported with the libraries that test maps;
# a test that maps no library is not part of coverage. Static and object
# libraries are not mapped. The coverage-report target writes one report per
# library, html/, summary.txt and lcov.info under <output>/<library>/, read
# from the library's file and every test executable mapped to it, so each
# test's direct sources are kept. When a report cannot be produced the others
# are still written and the target then fails, naming each failed library.
# ==============================================================================

include_guard(GLOBAL)
include(seeker/Core)

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------

option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

set(SEEKER_COVERAGE_LLVM_VERSION
    "21"
    CACHE STRING "LLVM tools version suffix (e.g., 21 for llvm-cov-21)"
)

set(SEEKER_COVERAGE_OUTPUT_DIR
    "${CMAKE_BINARY_DIR}/coverage"
    CACHE PATH "Coverage report output directory"
)

set(SEEKER_COVERAGE_IGNORE_REGEX
    ".*/build/_deps/.*|.*_uTest.*|.*_dTest.*|.*_pTest.*|/usr/local/.*"
    CACHE STRING "Regex for files to ignore in coverage reports"
)

# ------------------------------------------------------------------------------
# Internal state
# ------------------------------------------------------------------------------

define_property(
  GLOBAL
  PROPERTY SEEKER_COVERAGE_MAPPINGS
  BRIEF_DOCS "List of test:library mappings for coverage"
  FULL_DOCS "Semicolon-separated TEST_TARGET:LIBRARY_TARGET pairs"
)
set_property(GLOBAL PROPERTY SEEKER_COVERAGE_MAPPINGS "")

# ------------------------------------------------------------------------------
# seeker_coverage_init()
#
# With ENABLE_COVERAGE, locate llvm-profdata and llvm-cov
# (SEEKER_COVERAGE_LLVM_VERSION), create the seeker_coverage_flags interface
# target (Clang source-based instrumentation, Debug builds) and generate the
# coverage-report and coverage-clean targets at the end of the configure.
# Coverage is turned off with a warning for cross, bare-metal and non-Clang
# builds; the configure fails when the LLVM tools are missing. Call once after
# project().
# ------------------------------------------------------------------------------
function (seeker_coverage_init)
  _seeker_reject_extra_arguments(seeker_coverage_init ${ARGN})
  if (NOT ENABLE_COVERAGE)
    return()
  endif ()

  # Coverage requires native execution
  if (CMAKE_CROSSCOMPILING)
    message(WARNING "[Coverage] Disabled for cross-compilation")
    set(ENABLE_COVERAGE
        OFF
        PARENT_SCOPE
    )
    return()
  endif ()

  if (CMAKE_SYSTEM_NAME STREQUAL "Generic")
    message(WARNING "[Coverage] Disabled for bare-metal builds")
    set(ENABLE_COVERAGE
        OFF
        PARENT_SCOPE
    )
    return()
  endif ()

  # LLVM coverage requires Clang
  if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "[Coverage] Requires Clang (found: ${CMAKE_CXX_COMPILER_ID})")
    set(ENABLE_COVERAGE
        OFF
        PARENT_SCOPE
    )
    return()
  endif ()

  # Locate LLVM tools
  set(_ver "${SEEKER_COVERAGE_LLVM_VERSION}")

  find_program(SEEKER_LLVM_PROFDATA NAMES "llvm-profdata-${_ver}" "llvm-profdata")
  find_program(SEEKER_LLVM_COV NAMES "llvm-cov-${_ver}" "llvm-cov")

  if (NOT SEEKER_LLVM_PROFDATA OR NOT SEEKER_LLVM_COV)
    message(FATAL_ERROR "[Coverage] llvm-profdata/llvm-cov not found")
  endif ()

  # Interface library for coverage flags
  if (NOT TARGET seeker_coverage_flags)
    add_library(seeker_coverage_flags INTERFACE)
    add_library(seeker::coverage_flags ALIAS seeker_coverage_flags)

    target_compile_options(
      seeker_coverage_flags
      INTERFACE $<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:CXX>>: -fprofile-instr-generate
                -fcoverage-mapping> $<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:C>>:
                -fprofile-instr-generate -fcoverage-mapping>
    )

    # Link options conditional on Clang linker (CUDA links with GCC)
    target_link_options(
      seeker_coverage_flags
      INTERFACE
      $<$<AND:$<CONFIG:Debug>,$<LINK_LANG_AND_ID:CXX,Clang>>:
      -fprofile-instr-generate
      -fcoverage-mapping>
      $<$<AND:$<CONFIG:Debug>,$<LINK_LANG_AND_ID:C,Clang>>:
      -fprofile-instr-generate
      -fcoverage-mapping>
    )

  endif ()

  message(STATUS "[Coverage] Enabled (LLVM ${_ver})")
  message(STATUS "[Coverage]   profdata: ${SEEKER_LLVM_PROFDATA}")
  message(STATUS "[Coverage]   cov:      ${SEEKER_LLVM_COV}")
  message(STATUS "[Coverage]   output:   ${SEEKER_COVERAGE_OUTPUT_DIR}")

  # Generate targets at end of configure
  cmake_language(DEFER CALL _seeker_coverage_finalize)
endfunction ()

# ------------------------------------------------------------------------------
# seeker_coverage_register(<test_target> <library_target>)
#
# Register a test:library mapping for the report. seeker_add_gtest calls it for
# each library it instruments; a no-op without ENABLE_COVERAGE. A library that
# coverage cannot map (see _seeker_coverage_maps) fails the configure.
# ------------------------------------------------------------------------------
function (seeker_coverage_register _test _lib)
  _seeker_reject_extra_arguments(seeker_coverage_register ${ARGN})
  if (NOT ENABLE_COVERAGE)
    return()
  endif ()

  _seeker_coverage_maps(${_lib} _maps)
  if (NOT _maps)
    message(FATAL_ERROR "seeker_coverage_register: '${_lib}' is not a shared library "
                        "this project builds from C or C++ sources"
    )
  endif ()

  set_property(GLOBAL APPEND PROPERTY SEEKER_COVERAGE_MAPPINGS "${_test}:${_lib}")
endfunction ()

# ------------------------------------------------------------------------------
# _seeker_coverage_maps(<target> <out_var>)
#
# Set <out_var> to TRUE when coverage maps <target>: a shared library this
# project builds (not imported, not an alias) with at least one C or C++ source.
# ------------------------------------------------------------------------------
function (_seeker_coverage_maps _target _out_var)
  set(_maps FALSE)
  if (TARGET "${_target}")
    get_target_property(_aliased ${_target} ALIASED_TARGET)
    get_target_property(_imported ${_target} IMPORTED)
    get_target_property(_type ${_target} TYPE)
    if (NOT _aliased
        AND NOT _imported
        AND _type STREQUAL "SHARED_LIBRARY"
    )
      get_target_property(_sources ${_target} SOURCES)
      foreach (_src IN LISTS _sources)
        get_filename_component(_ext "${_src}" LAST_EXT)
        string(SUBSTRING "${_ext}" 1 -1 _ext)
        if (_ext IN_LIST CMAKE_CXX_SOURCE_FILE_EXTENSIONS OR _ext IN_LIST
                                                             CMAKE_C_SOURCE_FILE_EXTENSIONS
        )
          set(_maps TRUE)
          break()
        endif ()
      endforeach ()
    endif ()
  endif ()
  set(${_out_var}
      ${_maps}
      PARENT_SCOPE
  )
endfunction ()

# ------------------------------------------------------------------------------
# Internal: Generate coverage targets
# ------------------------------------------------------------------------------
function (_seeker_coverage_finalize)
  if (NOT ENABLE_COVERAGE)
    return()
  endif ()

  get_property(_mappings GLOBAL PROPERTY SEEKER_COVERAGE_MAPPINGS)
  if (NOT _mappings)
    message(STATUS "[Coverage] No mappings: no test links a library coverage maps")
    return()
  endif ()

  list(LENGTH _mappings _count)
  message(STATUS "[Coverage] Generating targets for ${_count} mapping(s)")

  file(MAKE_DIRECTORY "${SEEKER_COVERAGE_OUTPUT_DIR}")

  # Write manifest
  set(_manifest "${SEEKER_COVERAGE_OUTPUT_DIR}/manifest.cmake")
  _seeker_coverage_write_manifest("${_manifest}" "${_mappings}")

  # Write generator script
  set(_generator "${SEEKER_COVERAGE_OUTPUT_DIR}/generate_report.cmake")
  _seeker_coverage_write_generator("${_generator}")

  # Target: coverage-report
  add_custom_target(
    coverage-report
    COMMAND ${CMAKE_COMMAND} -DMANIFEST=${_manifest} -P "${_generator}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "[Coverage] Generating reports..."
    VERBATIM
  )

  # Target: coverage-clean
  add_custom_target(
    coverage-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${SEEKER_COVERAGE_OUTPUT_DIR}"
    COMMAND find "${CMAKE_BINARY_DIR}" -name "*.profraw" -delete
    COMMENT "[Coverage] Cleaning artifacts..."
    VERBATIM
  )
endfunction ()

# ------------------------------------------------------------------------------
# Internal: Write manifest (each mapped target's file comes from $<TARGET_FILE>)
# ------------------------------------------------------------------------------
function (_seeker_coverage_write_manifest _file _mappings)
  set(_content "# Coverage manifest (auto-generated)\n")
  string(APPEND _content "set(COVERAGE_MAPPINGS\n")
  set(_targets "")
  foreach (_m IN LISTS _mappings)
    string(APPEND _content "  \"${_m}\"\n")
    string(REPLACE ":" ";" _pair "${_m}")
    list(APPEND _targets ${_pair})
  endforeach ()
  string(APPEND _content ")\n")
  list(REMOVE_DUPLICATES _targets)
  foreach (_t IN LISTS _targets)
    string(APPEND _content "set(COVERAGE_FILE_${_t} \"$<TARGET_FILE:${_t}>\")\n")
  endforeach ()
  string(APPEND _content "set(LLVM_PROFDATA \"${SEEKER_LLVM_PROFDATA}\")\n")
  string(APPEND _content "set(LLVM_COV \"${SEEKER_LLVM_COV}\")\n")
  string(APPEND _content "set(OUTPUT_DIR \"${SEEKER_COVERAGE_OUTPUT_DIR}\")\n")
  string(APPEND _content "set(IGNORE_REGEX \"${SEEKER_COVERAGE_IGNORE_REGEX}\")\n")
  string(APPEND _content "set(BUILD_DIR \"${CMAKE_BINARY_DIR}\")\n")
  file(
    GENERATE
    OUTPUT "${_file}"
    CONTENT "${_content}"
  )
endfunction ()

# ------------------------------------------------------------------------------
# Internal: Write report generator script
# ------------------------------------------------------------------------------
function (_seeker_coverage_write_generator _file)
  set(_script
      [=[
# Coverage report generator (auto-generated)
cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED MANIFEST)
  message(FATAL_ERROR "MANIFEST not defined")
endif ()
include("${MANIFEST}")

# Find .profraw files
file(GLOB_RECURSE _profraw "${BUILD_DIR}/*.profraw")
list(LENGTH _profraw _count)

if (_count EQUAL 0)
  message(FATAL_ERROR "[Coverage] No .profraw files found. Run tests first.")
endif ()

message(STATUS "[Coverage] Found ${_count} .profraw file(s)")

# Merge into single profdata
set(_profdata "${OUTPUT_DIR}/merged.profdata")
execute_process(
  COMMAND "${LLVM_PROFDATA}" merge -sparse ${_profraw} -o "${_profdata}"
  RESULT_VARIABLE _rc
)
if (NOT _rc EQUAL 0)
  message(FATAL_ERROR "[Coverage] profdata merge failed")
endif ()

message(STATUS "[Coverage] Merged: ${_profdata}")

# One report per library, read from the library and every test mapped to it,
# so the code each test compiles directly is kept. A failed step is recorded
# and the other libraries are still reported. stderr of show and export is
# printed only on failure.
set(_libs "")
foreach (_mapping IN LISTS COVERAGE_MAPPINGS)
  string(REPLACE ":" ";" _parts "${_mapping}")
  list(GET _parts 0 _test)
  list(GET _parts 1 _lib)
  if (NOT _lib IN_LIST _libs)
    list(APPEND _libs ${_lib})
  endif ()
  list(APPEND _tests_of_${_lib} ${_test})
endforeach ()

set(_failed "")
list(LENGTH _libs _total)
foreach (_lib IN LISTS _libs)
  list(REMOVE_DUPLICATES _tests_of_${_lib})
  set(_lib_file "${COVERAGE_FILE_${_lib}}")
  set(_binaries "")
  foreach (_test IN LISTS _tests_of_${_lib})
    list(APPEND _binaries "${COVERAGE_FILE_${_test}}")
  endforeach ()

  set(_missing "")
  foreach (_file IN LISTS _binaries _lib_file)
    if (NOT EXISTS "${_file}")
      list(APPEND _missing "${_file}")
    endif ()
  endforeach ()
  if (_missing)
    list(JOIN _missing " " _missing_list)
    list(APPEND _failed "${_lib}: ${_missing_list} does not exist")
    continue()
  endif ()

  # The first test executable is the binary; the others and the library are objects
  list(POP_FRONT _binaries _main)
  set(_cov_args "${_main}" "-instr-profile=${_profdata}" "-ignore-filename-regex=${IGNORE_REGEX}")
  foreach (_object IN LISTS _binaries _lib_file)
    list(APPEND _cov_args "-object=${_object}")
  endforeach ()

  set(_out "${OUTPUT_DIR}/${_lib}")
  file(MAKE_DIRECTORY "${_out}/html")

  # HTML report
  execute_process(
    COMMAND "${LLVM_COV}" show ${_cov_args} "-format=html" "-output-dir=${_out}/html"
    RESULT_VARIABLE _rc
    OUTPUT_QUIET
    ERROR_VARIABLE _err
  )
  if (NOT _rc EQUAL 0)
    list(APPEND _failed "${_lib}: llvm-cov show exited ${_rc}: ${_err}")
    continue()
  endif ()

  # Text summary
  execute_process(
    COMMAND "${LLVM_COV}" report ${_cov_args}
    RESULT_VARIABLE _rc
    OUTPUT_FILE "${_out}/summary.txt"
  )
  if (NOT _rc EQUAL 0)
    list(APPEND _failed "${_lib}: llvm-cov report exited ${_rc}")
    continue()
  endif ()

  # LCOV for CI
  execute_process(
    COMMAND "${LLVM_COV}" export ${_cov_args} "-format=lcov"
    RESULT_VARIABLE _rc
    OUTPUT_FILE "${_out}/lcov.info"
    ERROR_VARIABLE _err
  )
  if (NOT _rc EQUAL 0)
    list(APPEND _failed "${_lib}: llvm-cov export exited ${_rc}: ${_err}")
    continue()
  endif ()

  list(JOIN _tests_of_${_lib} ", " _test_list)
  message(STATUS "[Coverage] Generated: ${_out}/html/index.html (${_test_list})")
endforeach ()

if (_failed)
  list(LENGTH _failed _failed_count)
  list(JOIN _failed "\n  " _failed_list)
  message(FATAL_ERROR "[Coverage] ${_failed_count} of ${_total} report(s) failed:\n  ${_failed_list}")
endif ()

message(STATUS "[Coverage] Reports: ${OUTPUT_DIR}")
]=]
  )
  file(WRITE "${_file}" "${_script}")
endfunction ()
