# The .NET builder image: a self-contained publish, no runtime on the device.
#
# `dotnet publish --self-contained` bundles the runtime and the app, so the
# device needs nothing. The RID makes the output architecture-specific.
FROM mcr.microsoft.com/dotnet/sdk:8.0 AS build

ARG DOTNET_RID=linux-x64
ARG BUILD_CMD=""

WORKDIR /src
COPY . /src

# Publish a self-contained, single-directory app for the target RID.
RUN mkdir -p /out/stage && \
    if [ -n "$BUILD_CMD" ]; then \
      /bin/sh -c "$BUILD_CMD"; \
    else \
      proj=$(find . -maxdepth 2 -name '*.csproj' -o -name '*.fsproj' | head -1); \
      [ -n "$proj" ] || { echo "no .csproj/.fsproj found" >&2; exit 1; }; \
      dotnet publish "$proj" -c Release -r ${DOTNET_RID} --self-contained true \
        -p:PublishSingleFile=false -o /out/app; \
    fi && \
    if [ -d /out/app ]; then cp -a /out/app/. /out/stage/; fi

FROM scratch
COPY --from=build /out/stage/ /out/stage/
