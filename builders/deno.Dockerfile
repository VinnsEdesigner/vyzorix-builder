# The Deno builder image: `deno compile` produces one self-contained binary.
#
# The runtime and the script are embedded, so the device needs nothing else. The
# binary is architecture-specific, so the adapter passes DENO_ARCH.
FROM denoland/deno:bin-2.1.4 AS deno
FROM debian:bookworm-slim AS build

ARG DENO_ARCH=x86_64
ARG BUILD_CMD=""

COPY --from=deno /deno /usr/local/bin/deno
RUN apt-get update && apt-get install -y --no-install-recommends findutils ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

# Compile to a single binary. The recipe may name the entrypoint and permissions
# in BUILD_CMD (e.g. "deno compile --allow-net server.ts -o app"); otherwise the
# conventional main.ts / main.js is used with --allow-all.
RUN mkdir -p /out/stage && \
    if [ -n "$BUILD_CMD" ]; then \
      /bin/sh -c "$BUILD_CMD" && \
      bin=$(find /src -maxdepth 1 -type f -executable ! -name '*.ts' ! -name '*.js' ! -name '*.json' | head -1) && \
      cp "$bin" /out/stage/app; \
    else \
      entry=$(ls main.ts main.js index.ts index.js 2>/dev/null | head -1); \
      [ -n "$entry" ] || { echo "no entrypoint (main.ts/main.js); set build=" >&2; exit 1; }; \
      deno compile --allow-all --target ${DENO_ARCH}-unknown-linux-gnu -o /out/stage/app "$entry"; \
    fi

FROM scratch
COPY --from=build /out/stage/ /out/stage/
