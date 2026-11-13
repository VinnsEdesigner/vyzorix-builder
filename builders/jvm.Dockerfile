# The JVM builder image: a native image by default, a fat jar as the fallback.
#
# A Debian base (eclipse-temurin) is used rather than the GraalVM image, which is
# a minimal UBI whose entrypoint and WORKDIR are awkward to drive from a build.
# GraalVM's native-image is installed on top, so the default mode still produces
# one self-contained binary the device can run without a JRE. JVM_MODE=jar keeps
# a fat jar instead, which needs a JRE on the device; it is the explicit
# fallback for a project native-image cannot handle.
FROM eclipse-temurin:21-jdk AS build

ARG JVM_MODE=native-image
ARG BUILD_CMD=""
ARG JAR_GLOB="*-all.jar"
ARG GRAALVM_VERSION=21.0.2

RUN apt-get update && apt-get install -y --no-install-recommends curl findutils tar gzip ca-certificates && \
    rm -rf /var/lib/apt/lists/* && \
    if [ "$JVM_MODE" = "native-image" ]; then \
      curl -fsSL "https://github.com/graalvm/graalvm-ce-builds/releases/download/jdk-${GRAALVM_VERSION}/native-image-installable-svm-java21-linux-amd64-${GRAALVM_VERSION}.jar" \
        -o /tmp/native-image.jar && \
      "$JAVA_HOME/bin/gu" install --local-file /tmp/native-image.jar && \
      rm /tmp/native-image.jar; \
    fi

WORKDIR /src
COPY . /src

# Run the project's own build first, when it has one (gradle/maven). A plain
# javac project needs none.
RUN if [ -n "$BUILD_CMD" ]; then /bin/sh -c "$BUILD_CMD"; fi

# Stage the artifact. native-image turns the fat jar into a single binary; jar
# mode stages the fat jar itself.
RUN mkdir -p /out/stage && \
    jar=$(find . -name "$JAR_GLOB" | head -1) && \
    [ -n "$jar" ] || { echo "no fat jar matching $JAR_GLOB" >&2; exit 1; } && \
    if [ "$JVM_MODE" = "native-image" ]; then \
      "$JAVA_HOME/bin/native-image" -jar "$jar" -o /out/stage/app; \
    else \
      cp "$jar" /out/stage/app.jar; \
    fi

FROM scratch
COPY --from=build /out/stage/ /out/stage/
