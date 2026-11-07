# ==============================================================================
# Module: seeker_helpers
#
# Low-level helper utilities for embedded and real-time systems: file I/O,
# string manipulation, formatting and CPU primitives with RT-safety annotations.
# ==============================================================================

include(seeker/All)
seeker_module(seeker_helpers)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

seeker_add_interface_library(
  NAME ${LIB_NAME}
  INC "${INC_DIR}"
  DEPS_INTERFACE fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins
# ------------------------------------------------------------------------------

seeker_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

seeker_add_test_subdirs()
