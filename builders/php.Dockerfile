# The PHP builder image: the app plus its Composer dependencies.
#
# PHP is expected on the device rootfs; the artifact carries the app and a
# vendored vendor/ directory, so the device needs no Composer and no network. The
# zip extension and unzip are installed because Composer needs them to download
# packages. A recipe `build` replaces the default `composer install`.
FROM composer:2 AS composer
FROM php:8.3-cli-bookworm AS build

ARG BUILD_CMD=""

# Composer downloads dist archives with zip; without this it fails with "The zip
# extension and unzip/7z commands are both missing".
RUN apt-get update && apt-get install -y --no-install-recommends unzip libzip-dev \
    && docker-php-ext-install zip \
    && rm -rf /var/lib/apt/lists/*

COPY --from=composer /usr/bin/composer /usr/local/bin/composer
WORKDIR /src
COPY . /src

RUN mkdir -p /out/stage && \
    if [ -n "$BUILD_CMD" ]; then \
      /bin/sh -c "$BUILD_CMD"; \
    elif [ -f composer.json ]; then \
      composer install --no-dev --optimize-autoloader --no-interaction; \
    fi && \
    ( cd /src && tar -cf - \
        --exclude='./.git' --exclude='./.vyzorix-builder.Dockerfile' \
        --exclude='./.dockerignore' \
        . ) | ( cd /out/stage && tar -xf - )

FROM scratch
COPY --from=build /out/stage/ /out/stage/
