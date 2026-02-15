# ==============================================================================
# Seeker Source Tree
#
# Domain libraries for Linux system diagnostics.
# Each domain is a separate shared library for minimal linking.
# ==============================================================================

add_subdirectory(helpers)
add_subdirectory(cpu)
add_subdirectory(memory)
add_subdirectory(storage)
add_subdirectory(network)
add_subdirectory(timing)
add_subdirectory(system)
add_subdirectory(device)

add_subdirectory(gpu)
