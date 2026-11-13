# The Go builder image: a static binary per ABI, nothing to resolve.
#
# Base = bookworm so the image (and any CGO the recipe opts into) matches the
# device. The adapter passes GOARCH/GOOS/CGO_ENABLED as build args.
FROM golang:1.27-bookworm AS build

ARG GOARCH=arm64
ARG GOOS=linux
ARG CGO_ENABLED=0
ARG BUILD_CMD="go build -o /out/app ./..."

WORKDIR /src
COPY . /src
RUN mkdir -p /out && /bin/sh -c "$BUILD_CMD"
# A tarball the engine copies out. The engine reads its name from the plan, so
# the inner filename is the announced one.
ARG OUT_NAME=app.tar.gz
RUN tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -czf /out/${OUT_NAME} -C /out app

FROM scratch
COPY --from=build /out/ /
