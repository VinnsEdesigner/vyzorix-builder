# The Rust builder image: a musl static binary.
#
# The musl target makes the binary fully static, so the glibc floor never
# applies and the closure is empty — the whole reason the adapter prefers it.
FROM rust:1-bookworm AS build

ARG RUST_TARGET=aarch64-unknown-linux-musl
ARG BUILD_CMD="cargo build --release --target ${RUST_TARGET}"

RUN rustup target add ${RUST_TARGET}
WORKDIR /src
COPY . /src
RUN mkdir -p /out && /bin/sh -c "$BUILD_CMD"
ARG OUT_NAME=app.tar.gz
RUN tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -czf /out/${OUT_NAME} -C /out .

FROM scratch
COPY --from=build /out/ /
