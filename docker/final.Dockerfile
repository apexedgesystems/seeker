# ==============================================================================
# final.Dockerfile - Artifact packaging and extraction stage
#
# Collects install trees from all builder images into a single lightweight
# image for easy extraction. Based on busybox for minimal size.
#
# Library artifacts (per-platform):
#   /output/seeker-{VER}-x86_64-linux.tar.gz          - x86_64 Linux (no CUDA)
#   /output/seeker-{VER}-x86_64-linux-cuda.tar.gz     - x86_64 Linux + CUDA
#   /output/seeker-{VER}-aarch64-jetson.tar.gz        - Jetson (aarch64 + CUDA)
#   /output/seeker-{VER}-aarch64-rpi.tar.gz           - Raspberry Pi (aarch64)
#   /output/seeker-{VER}-riscv64-linux.tar.gz         - RISC-V 64-bit
#
# Library tarballs contain:
#   lib/              - Shared libraries (.so)
#   include/          - Public headers
#   lib/cmake/seeker  - CMake find_package() config
#   share/doc/        - Documentation
#   .env              - Environment setup (LD_LIBRARY_PATH)
#
# Usage:
#   docker compose build final
#   make artifacts
#
# Extract artifacts:
#   docker create --name tmp seeker.final
#   docker cp tmp:/output/seeker-1.0.0-x86_64-linux.tar.gz .
#   docker rm tmp
# ==============================================================================
FROM busybox:latest

ARG USER
ARG VERSION=1.0.0

LABEL org.opencontainers.image.title="seeker.final" \
      org.opencontainers.image.description="Packaged build artifacts for distribution"

WORKDIR /output

# ==============================================================================
# Collect Library Install Trees from Builders
# ==============================================================================
COPY --from=seeker.builder.cpu:latest      /home/${USER}/workspace/build/native-linux-release/install/      ./cpu/
COPY --from=seeker.builder.cuda:latest     /home/${USER}/workspace/build/native-linux-release/install/      ./cuda/
COPY --from=seeker.builder.jetson:latest   /home/${USER}/workspace/build/jetson-aarch64-release/install/    ./jetson/
COPY --from=seeker.builder.rpi:latest      /home/${USER}/workspace/build/rpi-aarch64-release/install/       ./rpi/
COPY --from=seeker.builder.riscv64:latest  /home/${USER}/workspace/build/riscv64-linux-release/install/     ./riscv64/

# ==============================================================================
# Create Distribution Packages
# ==============================================================================
RUN tar -czf seeker-${VERSION}-x86_64-linux.tar.gz       -C ./cpu . && \
    tar -czf seeker-${VERSION}-x86_64-linux-cuda.tar.gz  -C ./cuda . && \
    tar -czf seeker-${VERSION}-aarch64-jetson.tar.gz     -C ./jetson . && \
    tar -czf seeker-${VERSION}-aarch64-rpi.tar.gz        -C ./rpi . && \
    tar -czf seeker-${VERSION}-riscv64-linux.tar.gz      -C ./riscv64 .

# ==============================================================================
# Default: List Available Artifacts
# ==============================================================================
CMD ["ls", "-la", "/output"]
