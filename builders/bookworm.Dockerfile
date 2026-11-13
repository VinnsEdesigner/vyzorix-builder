# The shared builder base.
#
# Debian bookworm on purpose: the device rootfs is bookworm (glibc 2.36), and an
# artifact built on a newer suite links symbols the rootfs does not have — a
# failure that appears nowhere but on the device.
# Every language image is FROM this, so the floor is stated once.
FROM debian:bookworm

RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl tar binutils \
    && rm -rf /var/lib/apt/lists/*

# The glibc floor every artifact must not exceed. The package layer asserts it
# per binary; this is the release it is asserted against.
ENV VYZORIX_GLIBC_FLOOR=2.36
