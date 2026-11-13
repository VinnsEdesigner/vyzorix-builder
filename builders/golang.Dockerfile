# The Go builder image: a static binary per ABI, nothing to resolve.
#
# Base = bookworm so the image (and any CGO the recipe opts into) matches the
# device. The adapter passes GOARCH/GOOS/CGO_ENABLED as build args.
FROM golang:1.27-bookworm AS build

ARG GOARCH=arm64
ARG GOOS=linux
ARG CGO_ENABLED=0
# A trailing slash makes -o a directory, so the staged tree is a directory the
# engine can pack (a bare -o would write a single file).
ARG BUILD_CMD="go build -o /out/stage/ ./..."

WORKDIR /src
COPY . /src
# A private module needs a token. It arrives as a BuildKit secret (never a build
# arg, which is visible in image history), mounted only for this RUN. With no
# secret the mount is absent and a public build is unaffected.
RUN --mount=type=secret,id=GITHUB_TOKEN,required=false \
    mkdir -p /out/stage && \
    if [ -f /run/secrets/GITHUB_TOKEN ]; then \
      export GOPRIVATE="${GOPRIVATE:-}" GONOSUMDB="${GONOSUMDB:-}" \
             GONOSUMCHECK=1 GOFLAGS="${GOFLAGS:-} -mod=mod"; \
      git config --global url."https://x-access-token:$(cat /run/secrets/GITHUB_TOKEN)@github.com/".insteadOf "https://github.com/"; \
    fi && \
    /bin/sh -c "$BUILD_CMD"

FROM scratch
COPY --from=build /out/stage/ /out/stage/
