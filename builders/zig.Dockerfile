# The Zig builder image: a static binary via the zig cross compiler.
#
# Zig ships its own libc and linker, so a musl static binary needs no extra
# toolchain — the whole reason a Zig artifact is self-contained.
FROM debian:bookworm-slim AS build

ARG ZIG_TARGET=aarch64-linux-musl
ARG ZIG_VERSION=0.13.0
ARG BUILD_CMD=""

RUN apt-get update && apt-get install -y --no-install-recommends curl xz-utils ca-certificates findutils \
    && rm -rf /var/lib/apt/lists/* \
    && curl -fsSL "https://ziglang.org/download/${ZIG_VERSION}/zig-linux-x86_64-${ZIG_VERSION}.tar.xz" \
       | tar -xJ -C /opt \
    && ln -s "/opt/zig-linux-x86_64-${ZIG_VERSION}/zig" /usr/local/bin/zig

WORKDIR /src
COPY . /src

# Build for the target, then stage the installed output. Zig 0.13 takes
# -Doptimize=ReleaseSafe; the recipe's own build step overrides this entirely.
RUN mkdir -p /out/stage /out/app && \
    if [ -n "$BUILD_CMD" ]; then \
      /bin/sh -c "$BUILD_CMD"; \
    else \
      zig build -Dtarget=${ZIG_TARGET} -Doptimize=ReleaseSafe --prefix /out/app; \
    fi && \
    cp -a /out/app/. /out/stage/

FROM scratch
COPY --from=build /out/stage/ /out/stage/
