# ==============================================================================
# mk/clean.mk - Artifact cleanup
#
# Provides targets for cleaning build artifacts, UPX outputs, documentation,
# and coverage data across all build directories.
# ==============================================================================

ifndef CLEAN_MK_GUARD
CLEAN_MK_GUARD := 1

# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------
# Note: Clean targets operate on ALL build directories to support
# multi-platform workflows (native, jetson, rpi, etc.)

# ------------------------------------------------------------------------------
# C++ Clean Targets
# ------------------------------------------------------------------------------

# Ninja clean across all build directories
clean-ninja:
	$(call log,clean,Running ninja clean in all build directories)
	@if [ -d build ]; then \
	  find build -mindepth 1 -maxdepth 1 -type d -exec sh -c \
	    'printf "  -> %s\n" "{}" && cd "{}" && ninja clean 2>/dev/null || true' \; ; \
	else \
	  printf '  -> build/ not found; skipping\n'; \
	fi

# Remove UPX-compressed outputs across all builds
clean-upx:
	$(call log,clean,Removing UPX artifacts)
	@if [ -d build ]; then \
	  find build -type f \( -name '*.upx' -o -name '*.so.upx' \) -delete 2>/dev/null || true; \
	fi

# Remove generated docs across all builds
clean-docs:
	$(call log,clean,Removing generated documentation)
	@if [ -d build ]; then \
	  find build -mindepth 2 -maxdepth 2 -name docs -type d -exec rm -rf {} + 2>/dev/null || true; \
	fi

# ------------------------------------------------------------------------------
# Aggregate Clean Targets
# ------------------------------------------------------------------------------

# Main clean target
clean: clean-ninja clean-upx clean-docs coverage-clean
	$(call log,clean,Done)

# Deep clean - remove entire build directory
distclean:
	$(call log,clean,Removing build/ and compile_commands.json)
	@rm -rf build/ compile_commands.json

# ------------------------------------------------------------------------------
# Phony Declarations
# ------------------------------------------------------------------------------

.PHONY: clean clean-ninja clean-upx clean-docs distclean

endif  # CLEAN_MK_GUARD
