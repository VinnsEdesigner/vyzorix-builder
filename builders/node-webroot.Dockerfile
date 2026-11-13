# The Node/TS webroot builder image: a static site, architecture-free.
#
# `npm ci && npm run build` produces dist/ of static files that need no runtime
# on the device, so the artifact is architecture-free and the manifest omits arch.
FROM node:20-bookworm-slim AS build

ARG BUILD_CMD="npm ci && npm run build"
ARG DIST=dist

WORKDIR /src
COPY . /src
RUN /bin/sh -c "$BUILD_CMD"
# The image leaves the built dist at /out/stage; the engine packs it, so the
# tarball holds the dist contents at its root.
RUN mkdir -p /out/stage && cp -a /src/${DIST}/. /out/stage/

FROM scratch
COPY --from=build /out/stage/ /out/stage/
