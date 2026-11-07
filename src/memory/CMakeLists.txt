# ==============================================================================
# Module: seeker_memory
#
# Memory diagnostics: NUMA topology, hugepages, memory stats, locking, EDAC,
# page sizes.
# ==============================================================================

include(seeker/All)
seeker_module(seeker_memory)

# ------------------------------------------------------------------------------
# Sources (6 modules)
# ------------------------------------------------------------------------------

set(CPP_SOURCES
    "${SRC_DIR}/PageSizes.cpp" "${SRC_DIR}/MemoryStats.cpp" "${SRC_DIR}/NumaTopology.cpp"
    "${SRC_DIR}/HugepageStatus.cpp" "${SRC_DIR}/MemoryLocking.cpp" "${SRC_DIR}/EdacStatus.cpp"
)

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------

seeker_add_library(
  NAME ${LIB_NAME}
  TYPE SHARED
  INC "${INC_DIR}"
  SRC ${CPP_SOURCES}
  DEPS_PUBLIC seeker_helpers fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins
# ------------------------------------------------------------------------------

seeker_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Documentation
# ------------------------------------------------------------------------------

seeker_install_docs("${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION "api/memory")

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

seeker_add_test_subdirs()
