# The Node/TS server builder image: an app plus its production dependencies.
#
# The artifact carries the app and `node_modules` (production only). Node is
# expected on the device rootfs; a project that must bundle its own runtime is
# the Containerfile escape hatch. Architecture-free: JavaScript is portable, so
# the manifest omits `arch` unless a native module is present.
FROM node:20-bookworm-slim AS build

ARG BUILD_CMD=""
ARG DIST=dist

WORKDIR /src
COPY . /src

# Install production dependencies only. `npm ci` needs a lockfile; `npm install`
# is the fallback for a project without one. A private registry token arrives as
# a BuildKit secret (never a build arg); with no secret the mount is absent.
RUN --mount=type=secret,id=NPM_TOKEN,required=false \
    if [ -f /run/secrets/NPM_TOKEN ]; then \
      export NODE_AUTH_TOKEN="$(cat /run/secrets/NPM_TOKEN)"; \
      printf '//registry.npmjs.org/:_authToken=%s\n' "$NODE_AUTH_TOKEN" > /root/.npmrc; \
    fi && \
    if [ -f package-lock.json ]; then npm ci --omit=dev; \
    elif [ -f package.json ]; then npm install --omit=dev; fi

# Run the project's own build, when it has one (TypeScript -> JavaScript, etc.).
RUN if [ -n "$BUILD_CMD" ]; then /bin/sh -c "$BUILD_CMD"; fi

# Stage the app and its dependencies. node_modules is required at run time; the
# compiled output (DIST) is staged too when the build produced one.
RUN mkdir -p /out/stage && \
    ( cd /src && tar -cf - \
        --exclude='./.git' --exclude='./.vyzorix-builder.Dockerfile' \
        --exclude='./.dockerignore' \
        . ) | ( cd /out/stage && tar -xf - )

FROM scratch
COPY --from=build /out/stage/ /out/stage/
