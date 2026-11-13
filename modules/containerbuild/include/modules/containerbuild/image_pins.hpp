// publish-side and build-side base-image pins.
//
// A base image referenced by a floating tag (`rust:1-bookworm`) is not
// reproducible: the same source can build different bytes on two machines. A pin
// resolves the tag to a digest, so the build is content-addressed (contract R8).
//
// The pins live in builders/pins.txt and are embedded into the binary by
// tooling_embed.py, so the builder applies them without reading a file at run
// time. An image with no pin stays floating, which is a documented exception.
#pragma once

#include <string>
#include <vector>

namespace vyzorix::containerbuild {

// ImagePin is one `<image>:<tag> = sha256:<digest>` entry.
struct ImagePin {
    std::string reference;  // e.g. "debian:bookworm"
    std::string digest;     // e.g. "sha256:abc…"
};

// image_pins returns the compiled-in pins (possibly empty).
[[nodiscard]] const std::vector<ImagePin>& image_pins();

// apply_image_pins rewrites every `FROM <image>:<tag>` in `dockerfile` to
// `FROM <image>:<tag>@<digest>` when a pin exists for that reference. A `FROM`
// that already carries a digest is left alone, and an image with no pin is left
// as-is (the documented exception). It is a pure string function so it is
// unit-testable without Docker.
[[nodiscard]] std::string apply_image_pins(const std::string& dockerfile,
                                           const std::vector<ImagePin>& pins);

}  // namespace vyzorix::containerbuild
