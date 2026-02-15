# ==============================================================================
# dev/cuda.Dockerfile - Interactive CUDA development shell
#
# CUDA-enabled development environment. Use this for GPU kernel development
# and CUDA debugging.
#
# Usage:
#   make shell-dev-cuda          # Interactive shell
#   docker compose run dev-cuda  # Via compose
# ==============================================================================
FROM nvidia/cuda:13.1.0-devel-ubuntu24.04

ARG USER
ARG UID
ARG GID

LABEL org.opencontainers.image.title="seeker.dev.cuda" \
      org.opencontainers.image.description="CUDA development environment for Seeker"

USER root
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# ==============================================================================
# Base Tooling Overlay
# ==============================================================================
# Overlay our base tools (Clang, CMake, formatters, etc.) onto the NVIDIA image.
COPY --from=seeker.base:latest / /

# CUDA paths (ensure nvcc and libs are found)
ENV PATH="/usr/local/cuda/bin:${PATH}"
ENV LD_LIBRARY_PATH="/usr/local/cuda/lib64:${LD_LIBRARY_PATH}"

# ==============================================================================
# Environment Re-export
# ==============================================================================
# COPY --from overlays files but does NOT preserve ENV declarations from the
# source image. We must re-declare all environment variables from seeker.base.
ENV PIP_NO_CACHE_DIR=off \
    PIP_DISABLE_PIP_VERSION_CHECK=on \
    PIP_DEFAULT_TIMEOUT=100
ENV CONTAINER=yes
ENV OMP_NUM_THREADS=1 \
    OPENBLAS_NUM_THREADS=1 \
    OMP_MAX_ACTIVE_LEVELS=1
ENV CCACHE_DIR=/ccache \
    CCACHE_MAXSIZE=5G \
    CCACHE_COMPRESS=1

# ==============================================================================
# NVML Stub Linking
# ==============================================================================
# Link NVML stub so builds succeed without a GPU present.
RUN ln -sf /usr/local/cuda/targets/x86_64-linux/lib/stubs/libnvidia-ml.so \
           /usr/local/cuda/targets/x86_64-linux/lib/libnvidia-ml.so && \
    ldconfig

# ==============================================================================
# User Setup
# ==============================================================================
RUN setup-user.sh "${USER}" "${UID}" "${GID}" && \
    setup-prompt.sh "${USER}" "${UID}" "${GID}" "32" "CUDA"

# ==============================================================================
# Validation
# ==============================================================================
RUN nvcc --version && \
    echo "CUDA dev image validation: OK"

USER ${USER}
WORKDIR /home/${USER}
