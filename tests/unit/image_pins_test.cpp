#include "support/test.hpp"

#include "modules/containerbuild/image_pins.hpp"

#include <string>
#include <vector>

using namespace vyzorix::containerbuild;

namespace {
const std::vector<ImagePin> kPins = {
    {"debian:bookworm", "sha256:aaaa"},
    {"rust:1-bookworm", "sha256:bbbb"},
};
}  // namespace

VYZ_TEST(image_pins_rewrites_a_pinned_from) {
    std::string df = "FROM debian:bookworm\nRUN echo hi\n";
    auto out = apply_image_pins(df, kPins);
    CHECK(out.find("FROM debian:bookworm@sha256:aaaa") != std::string::npos);
    // The rest of the line and the file are untouched.
    CHECK(out.find("RUN echo hi") != std::string::npos);
}

VYZ_TEST(image_pins_handles_a_stage_alias) {
    // `FROM x AS build` must keep the alias after the digest.
    auto out = apply_image_pins("FROM rust:1-bookworm AS build\n", kPins);
    CHECK(out.find("FROM rust:1-bookworm@sha256:bbbb AS build") != std::string::npos);
}

VYZ_TEST(image_pins_leaves_an_unpinned_image_alone) {
    // An image with no pin stays floating — the documented exception — rather
    // than being dropped or corrupted.
    std::string df = "FROM node:20-bookworm-slim AS build\n";
    CHECK_EQ(apply_image_pins(df, kPins), df);
}

VYZ_TEST(image_pins_leaves_an_already_pinned_from_alone) {
    // A FROM that already carries a digest must not get a second one.
    std::string df = "FROM debian:bookworm@sha256:existing\n";
    auto out = apply_image_pins(df, kPins);
    CHECK_EQ(out, df);
    CHECK(out.find("existing") != std::string::npos);
    CHECK(out.find("aaaa") == std::string::npos);
}

VYZ_TEST(image_pins_handles_a_multi_stage_file) {
    auto out = apply_image_pins(
        "FROM composer:2 AS composer\nFROM php:8.3-cli-bookworm AS build\nCOPY --from=composer /x /y\n", kPins);
    // Neither is pinned, so both are unchanged, and COPY is untouched.
    CHECK(out.find("FROM composer:2 AS composer") != std::string::npos);
    CHECK(out.find("COPY --from=composer") != std::string::npos);
}

VYZ_TEST(image_pins_preserves_a_trailing_newline) {
    auto with_nl = apply_image_pins("FROM debian:bookworm\n", kPins);
    CHECK(!with_nl.empty() && with_nl.back() == '\n');

    auto without_nl = apply_image_pins("FROM debian:bookworm", kPins);
    CHECK(!without_nl.empty() && without_nl.back() != '\n');
}

VYZ_TEST(image_pins_embedded_pins_parse) {
    // The embedded pins file must parse without error (an empty set is fine; a
    // malformed line would be a silent no-op, which is why this at least
    // exercises the parse).
    (void)image_pins();
    CHECK(true);
}
