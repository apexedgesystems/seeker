# ==============================================================================
# seeker/All.cmake - Entry point for the Seeker CMake helpers
# ==============================================================================
#
# Usage:
#   include(seeker/All)
#
# Provides the seeker_* functions of the modules it includes. Include this
# once per CMakeLists.txt.
# ==============================================================================

include_guard(GLOBAL)

# Foundation utilities (must be first)
include(seeker/Core)

# Build acceleration (ccache, fast linker, split DWARF)
include(seeker/BuildAcceleration)

# CUDA integration (before Targets, which depends on it)
include(seeker/Cuda)

# Target factories
include(seeker/Targets)

# Coverage infrastructure
include(seeker/Coverage)

# Testing infrastructure
include(seeker/Testing)

# Tooling (docs, UPX, clang-tidy)
include(seeker/Tooling)

# Configure-time summary
include(seeker/Summary)
