# The Python builder image: the app plus its dependencies.
#
# The device rootfs provides python3 (3.11), so the artifact carries the app and
# a site-packages directory. A dependency with a C extension must arrive as an
# aarch64 manylinux wheel — never compiled — so a foreign ABI needs no qemu.
FROM python:3.11-bookworm AS build

ARG PY_PLATFORM=manylinux2014_aarch64
ARG PY_VER=311
ARG DEB_ARCH=arm64
ARG BUILD_CMD=""

WORKDIR /src
COPY . /src

# Install the project's dependencies (from requirements.txt or the project
# metadata) into the staged site-packages, resolved for the target platform.
# A private index token arrives as a BuildKit secret (never a build arg). With no
# secret the mount is absent and a public install is unaffected.
RUN --mount=type=secret,id=PIP_INDEX_URL,required=false \
    mkdir -p /out/stage/site-packages && \
    if [ -f /run/secrets/PIP_INDEX_URL ]; then \
      export PIP_INDEX_URL="$(cat /run/secrets/PIP_INDEX_URL)"; \
    fi && \
    if [ -f requirements.txt ]; then \
      pip install --no-compile --target /out/stage/site-packages \
        --platform ${PY_PLATFORM} --only-binary=:all: --python-version ${PY_VER} \
        -r requirements.txt; \
    elif [ -f pyproject.toml ] || [ -f setup.py ]; then \
      pip install --no-compile --target /out/stage/site-packages \
        --platform ${PY_PLATFORM} --only-binary=:all: --python-version ${PY_VER} .; \
    fi

# Run the project's own build step, when it has one.
RUN if [ -n "$BUILD_CMD" ]; then /bin/sh -c "$BUILD_CMD"; fi

# Stage the application source, minus the build leftovers pip and the project's
# build step leave behind. Only the source and the resolved dependencies ship.
RUN mkdir -p /out/stage/app && \
    ( cd /src && tar -cf - \
        --exclude='./build' --exclude='./dist' --exclude='./.git' \
        --exclude='./__pycache__' --exclude='*.egg-info' \
        --exclude='./.vyzorix-builder.Dockerfile' --exclude='./.dockerignore' \
        . ) | ( cd /out/stage/app && tar -xf - )

FROM scratch
COPY --from=build /out/stage/ /out/stage/
