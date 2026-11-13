# The Bun builder image: one self-contained binary via `bun build --compile`.
#
# The runtime is embedded in the output, so the device needs nothing else.
FROM oven/bun:1 AS build

ARG BUN_TARGET=bun-linux-x64
ARG BUILD_CMD=""

WORKDIR /src
COPY . /src

# Install dependencies, then compile the entrypoint into a single binary. The
# recipe may name the entrypoint in BUILD_CMD; otherwise main.ts / index.ts is
# used.
RUN mkdir -p /out/stage && \
    if [ -f package.json ]; then bun install; fi && \
    if [ -n "$BUILD_CMD" ]; then \
      /bin/sh -c "$BUILD_CMD" && \
      bin=$(find /src -maxdepth 1 -type f -executable ! -name '*.ts' ! -name '*.js' ! -name '*.json' | head -1) && \
      cp "$bin" /out/stage/app; \
    else \
      entry=$(ls main.ts index.ts main.js index.js 2>/dev/null | head -1); \
      [ -n "$entry" ] || { echo "no entrypoint (main.ts/index.ts); set build=" >&2; exit 1; }; \
      bun build --compile --target=${BUN_TARGET} "$entry" --outfile /out/stage/app; \
    fi

FROM scratch
COPY --from=build /out/stage/ /out/stage/
