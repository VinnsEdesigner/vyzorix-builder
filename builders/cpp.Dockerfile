# The C/C++ builder image: cross-compile, then bundle the shared-library closure.
#
# A bookworm base so the glibc matches the device, and a cross toolchain so no
# qemu is needed. The image leaves an unpacked tree: the engine asserts the glibc
# floor and writes the deterministic tarball, so that logic lives in one tested
# place rather than in every Dockerfile.
FROM debian:bookworm

# The cross toolchain package names, passed explicitly by the adapter rather than
# derived here — a wrong derivation silently installs the native compiler.
ARG CROSS_GCC=gcc-x86-64-linux-gnu
ARG CROSS_GXX=g++-x86-64-linux-gnu
ARG CROSS_BINUTILS=binutils-x86-64-linux-gnu
ARG CROSS_PREFIX=x86_64-linux-gnu-
ARG MULTIARCH=x86_64-linux-gnu
ARG DEB_ARCH=amd64
ARG GLIBC_FLOOR=2.36
ARG BUILD_CMD="make"
ARG LIBS=""

RUN dpkg --add-architecture ${DEB_ARCH} && apt-get update && apt-get install -y --no-install-recommends \
      ${CROSS_GCC} ${CROSS_GXX} ${CROSS_BINUTILS} \
      make cmake ninja-build pkg-config ca-certificates tar \
    && rm -rf /var/lib/apt/lists/*

# Point the project's build at the cross compiler, so a plain `make` works.
ENV CC=${CROSS_PREFIX}gcc
ENV CXX=${CROSS_PREFIX}g++
ENV AR=${CROSS_PREFIX}ar

WORKDIR /src
COPY . /src

# Run the project's build, then stage what it produced. The recipe's build step
# is expected to write into /out/app.
RUN /bin/sh -c "$BUILD_CMD" && \
    mkdir -p /out/stage && \
    if [ -d /out/app ]; then cp -a /out/app/. /out/stage/; fi

# Bundle the shared-library closure of everything staged, minus the glibc core
# the device rootfs already provides. readelf, never ldd: ldd would try to run a
# foreign binary.
RUN set -e; \
    if [ -n "$LIBS" ]; then \
      find /out/stage -type f -executable -exec sh -c 'readelf -d "$1" 2>/dev/null | grep -q NEEDED' _ {} \; -print \
        | xargs -r readelf -d --wide 2>/dev/null \
        | grep -o 'Shared library: \[[^]]*\]' | sed 's/.*\[//; s/\]//' | sort -u > /tmp/needed.txt; \
      while read -r lib; do \
        case "$lib" in libc.so*|libm.so*|libpthread.so*|libdl.so*|librt.so*|libgcc_s.so*|libstdc++.so*|ld-linux*) continue;; esac; \
        src=$(find /usr/${MULTIARCH} /lib/${MULTIARCH} -name "$lib" 2>/dev/null | head -1); \
        if [ -n "$src" ]; then mkdir -p /out/stage/lib && cp -L "$src" /out/stage/lib/; fi; \
      done < /tmp/needed.txt; \
    fi

FROM scratch
COPY --from=0 /out/stage/ /out/stage/
