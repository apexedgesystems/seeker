# ==============================================================================
# seeker/Core.cmake - Foundation utilities for Seeker CMake infrastructure
# ==============================================================================

include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# _seeker_check_arguments(<helper> <prefix> [REQUIRED <keywords...>])
#
# Validate the result of cmake_parse_arguments(<prefix> ...) in <helper>. The
# configure fails on arguments that match no keyword, on keywords given without
# a value and on REQUIRED keywords that were not given; every message names
# <helper>. A list keyword takes each following word up to the next keyword of
# the same helper, so a misspelled keyword after a list is one of its values.
# ------------------------------------------------------------------------------
function (_seeker_check_arguments _helper _prefix)
  cmake_parse_arguments(_CHK "" "" "REQUIRED" ${ARGN})
  if (DEFINED _CHK_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "_seeker_check_arguments: unknown arguments: ${_CHK_UNPARSED_ARGUMENTS}")
  endif ()

  if (DEFINED ${_prefix}_UNPARSED_ARGUMENTS)
    list(JOIN ${_prefix}_UNPARSED_ARGUMENTS " " _unknown)
    message(FATAL_ERROR "${_helper}: unknown arguments: ${_unknown}")
  endif ()

  if (DEFINED ${_prefix}_KEYWORDS_MISSING_VALUES)
    list(JOIN ${_prefix}_KEYWORDS_MISSING_VALUES " " _empty)
    message(FATAL_ERROR "${_helper}: no value given for ${_empty}")
  endif ()

  foreach (_keyword IN LISTS _CHK_REQUIRED)
    if (NOT DEFINED ${_prefix}_${_keyword})
      message(FATAL_ERROR "${_helper}: ${_keyword} is required")
    endif ()
  endforeach ()
endfunction ()

# ------------------------------------------------------------------------------
# _seeker_reject_extra_arguments(<helper> [<extra>...])
#
# Fail the configure when a helper that takes only positional parameters is
# given more arguments than it declares. Callers pass ${ARGN}.
# ------------------------------------------------------------------------------
function (_seeker_reject_extra_arguments _helper)
  if (ARGC GREATER 1)
    list(JOIN ARGN " " _extra)
    message(FATAL_ERROR "${_helper}: unexpected arguments: ${_extra}")
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# _seeker_skip(<target> <reason>)
#
# Record that a target factory did not create <target> on this platform (a
# bare-metal build or a requirement that does not hold). The helpers that check
# a target name with _seeker_target_exists (seeker_standard_optins,
# seeker_nvml_enable, seeker_clang_tidy_cuda, the CUDA sidecar's CORE) skip a
# recorded name and fail on any other name that is not a target. Prints the
# reason when SEEKER_TARGETS_VERBOSE is ON.
# ------------------------------------------------------------------------------
function (_seeker_skip _target _reason)
  set_property(GLOBAL APPEND PROPERTY SEEKER_SKIPPED_TARGETS "${_target}")
  if (SEEKER_TARGETS_VERBOSE)
    message(STATUS "[seeker] SKIP ${_target} (${_reason})")
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# _seeker_target_exists(<helper> <target> <out_var>)
#
# Set <out_var> to TRUE when <target> exists and to FALSE when a factory skipped
# it (_seeker_skip). Any other name fails the configure with <helper>'s name.
# ------------------------------------------------------------------------------
function (_seeker_target_exists _helper _target _out_var)
  if (TARGET "${_target}")
    set(${_out_var}
        TRUE
        PARENT_SCOPE
    )
    return()
  endif ()

  get_property(_skipped GLOBAL PROPERTY SEEKER_SKIPPED_TARGETS)
  if (NOT "${_target}" IN_LIST _skipped)
    message(FATAL_ERROR "${_helper}: '${_target}' is not a target")
  endif ()
  set(${_out_var}
      FALSE
      PARENT_SCOPE
  )
endfunction ()

# ------------------------------------------------------------------------------
# seeker_guard(<target>)
#
# Fail the configure when <target> does not exist.
# ------------------------------------------------------------------------------
function (seeker_guard _target)
  _seeker_reject_extra_arguments(seeker_guard ${ARGN})
  if (NOT TARGET "${_target}")
    message(FATAL_ERROR "seeker_guard: target '${_target}' does not exist")
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# seeker_module(<name>)
#
# Set the module variables a leaf file uses, in the caller's scope:
#   LIB_NAME    <name>
#   SRC_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/src
#   INC_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/inc
#   UTST_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/utst  unit tests (seeker_add_gtest,
#                                                 registered with CTest)
#   PTST_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/ptst  performance tests
#                                                 (seeker_add_ptest, run by hand)
#   DTST_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/dtst  development tests
#                                                 (seeker_add_devtest, run by hand)
#   TST_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/tst   general test directory
#   README_FILE ${CMAKE_CURRENT_SOURCE_DIR}/README.md
#
# A leaf file adds a test directory only when its CMakeLists.txt exists.
# ------------------------------------------------------------------------------
function (seeker_module _name)
  _seeker_reject_extra_arguments(seeker_module ${ARGN})
  set(LIB_NAME
      "${_name}"
      PARENT_SCOPE
  )
  set(SRC_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
      PARENT_SCOPE
  )
  set(INC_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/inc"
      PARENT_SCOPE
  )
  set(TST_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/tst"
      PARENT_SCOPE
  )
  set(UTST_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/utst"
      PARENT_SCOPE
  )
  set(PTST_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/ptst"
      PARENT_SCOPE
  )
  set(DTST_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/dtst"
      PARENT_SCOPE
  )
  set(README_FILE
      "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
      PARENT_SCOPE
  )
endfunction ()

# ------------------------------------------------------------------------------
# seeker_is_cuda_target(<target> <out_var>)
#
# Set <out_var> to TRUE if <target> has .cu or .cuh sources, FALSE otherwise.
# ------------------------------------------------------------------------------
function (seeker_is_cuda_target _target _out_var)
  _seeker_reject_extra_arguments(seeker_is_cuda_target ${ARGN})
  set(_has_cuda FALSE)

  get_target_property(_sources ${_target} SOURCES)
  if (_sources)
    foreach (_src IN LISTS _sources)
      if (_src MATCHES "\\.(cu|cuh)$")
        set(_has_cuda TRUE)
        break()
      endif ()
    endforeach ()
  endif ()

  set(${_out_var}
      ${_has_cuda}
      PARENT_SCOPE
  )
endfunction ()

# ------------------------------------------------------------------------------
# seeker_standard_optins(<target>)
#
# Attach the opt-in tooling to a library target:
#   - Doxygen docs (seeker_add_doxygen; PROJECT_BUILD_DOCS)
#   - a UPX copy (seeker_add_upx_copy; ENABLE_UPX, release builds, executables
#     and, with UPX_COMPRESS_SHARED, shared libraries)
#   - clang-tidy over the target's .cu files (seeker_clang_tidy_cuda;
#     ENABLE_CLANG_TIDY)
#
# Coverage is not an opt-in: seeker_add_gtest instruments the compiled
# libraries a test links. A target a factory skipped on this platform is
# ignored; any other name that is not a target fails the configure.
# ------------------------------------------------------------------------------
function (seeker_standard_optins _target)
  _seeker_reject_extra_arguments(seeker_standard_optins ${ARGN})
  _seeker_target_exists(seeker_standard_optins "${_target}" _exists)
  if (NOT _exists)
    return()
  endif ()

  # Doxygen documentation
  seeker_add_doxygen(${_target})

  # UPX compression
  seeker_add_upx_copy(${_target})

  # CUDA-specific: clang-tidy on .cu files
  seeker_is_cuda_target(${_target} _has_cuda)
  if (_has_cuda)
    get_target_property(_sources ${_target} SOURCES)
    set(_cu_files)
    foreach (_src IN LISTS _sources)
      if (_src MATCHES "\\.(cu)$")
        list(APPEND _cu_files "${_src}")
      endif ()
    endforeach ()
    if (_cu_files)
      seeker_clang_tidy_cuda(${_target} FILES ${_cu_files} ALLOW_FAILURE)
    endif ()
  endif ()
endfunction ()
