# ==============================================================================
# Module: seeker_helpers
#
# Low-level helper utilities for embedded and real-time systems.
# Provides file I/O, string manipulation, formatting, and CPU primitives
# with RT-safety annotations.
# ==============================================================================

include(seeker/All)
seeker_module(seeker_helpers)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

seeker_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}" DEPS_INTERFACE fmt::fmt)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

seeker_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (PROJECT_IS_TOP_LEVEL)
  if (EXISTS "${UTST_DIR}/CMakeLists.txt")
    add_subdirectory(utst)
  endif ()
endif ()
