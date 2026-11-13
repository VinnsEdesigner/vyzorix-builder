# The Ruby builder image: the app plus its gems.
#
# Ruby is expected on the device rootfs; the artifact carries the app and a
# vendored gem set, so the device needs no `gem install` and no network. A recipe
# `build` replaces the default `bundle install`.
FROM ruby:3.3-bookworm AS build

ARG BUILD_CMD=""

WORKDIR /src
COPY . /src

RUN mkdir -p /out/stage && \
    if [ -n "$BUILD_CMD" ]; then \
      /bin/sh -c "$BUILD_CMD"; \
    elif [ -f Gemfile ]; then \
      bundle config set --local path /out/stage/vendor && bundle install; \
    fi && \
    ( cd /src && tar -cf - \
        --exclude='./.git' --exclude='./.vyzorix-builder.Dockerfile' \
        --exclude='./.dockerignore' \
        . ) | ( cd /out/stage && tar -xf - )

FROM scratch
COPY --from=build /out/stage/ /out/stage/
