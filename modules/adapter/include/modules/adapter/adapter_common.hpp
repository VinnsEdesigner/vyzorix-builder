// adapter — shared helpers translating a target into each toolchain's vocabulary.
#pragma once

#include <string>

#include "domain/naming.hpp"
#include "domain/recipe.hpp"
#include "domain/target.hpp"
#include "modules/plan/plan.hpp"

namespace vyzorix::plan::adapter {

// source_context is the artifact's source subdirectory, as the docker build
// context. An artifact with no `path` builds from the project root.
inline std::string source_context(const domain::RecipeArtifact& a) {
    return a.path.empty() ? "." : a.path;
}

// builder_image is the pinned builder image for a language. Images are pinned by
// the suite (the glibc floor's release) so a build is reproducible and links
// against the device's glibc.
inline std::string builder_image(const std::string& lang, const std::string& suite) {
    std::string s = suite.empty() ? "bookworm" : suite;
    return "ghcr.io/vinnsedesigner/vyzorix-builder-" + lang + ":" + s;
}

inline std::string goarch(const std::string& abi) {
    if (abi == "arm64-v8a") return "arm64";
    if (abi == "armeabi-v7a") return "arm";
    return "amd64";
}

inline std::string gcc_tuple(const std::string& debarch) {
    if (debarch == "arm64") return "aarch64-linux-gnu";
    if (debarch == "armhf") return "arm-linux-gnueabihf";
    return "x86_64-linux-gnu";
}

inline std::string rust_triple(const std::string& abi) {
    if (abi == "arm64-v8a") return "aarch64-unknown-linux-musl";
    if (abi == "armeabi-v7a") return "armv7-unknown-linux-musleabihf";
    return "x86_64-unknown-linux-musl";
}

// deno_target is the Rust-style triple deno compile expects, which differs from
// Go's arch names (aarch64, not arm64).
inline std::string deno_target(const std::string& abi) {
    if (abi == "arm64-v8a") return "aarch64-unknown-linux-gnu";
    if (abi == "armeabi-v7a") return "armv7-unknown-linux-gnueabihf";
    return "x86_64-unknown-linux-gnu";
}

inline std::string pyplatform(const std::string& abi) {
    if (abi == "arm64-v8a") return "manylinux2014_aarch64";
    if (abi == "armeabi-v7a") return "manylinux2014_armv7l";
    return "manylinux2014_x86_64";
}

// add_build_arg appends a docker --build-arg pair, but only when the value is
// set: an empty value would pass "KEY=" to the builder and silently override a
// default the Dockerfile relies on.
inline void add_build_arg(BuildPlan& plan, const std::string& key, const std::string& value) {
    if (value.empty()) return;
    plan.args.emplace_back("--build-arg");
    plan.args.emplace_back(key + "=" + value);
}

// The image path every first-class adapter leaves its unpacked artifact in. The
// engine packs it, so the Dockerfiles do not tar and the packing is one tested
// implementation.
inline constexpr const char* kStagingDir = "/out/stage";

// with_cmd records the artifact's entrypoint on the plan, so the engine can
// assert it exists in the staged tree: a broken `cmd` is caught
// at build time, not discovered when the device tries to start the app.
inline void with_cmd(BuildPlan& plan, const domain::RecipeArtifact& a) {
    plan.cmd = a.cmd;
    plan.app = a.app;
}

// docker_platform maps a Debian arch to a docker `--platform` value, so a
// multi-arch host builds the target architecture rather than its own (contract
// R9). It is set on every plan, including the Containerfile escape hatch, which
// cannot otherwise request a platform.
inline std::string docker_platform(const std::string& debarch) {
    if (debarch == "arm64") return "linux/arm64";
    if (debarch == "armhf") return "linux/arm/v7";
    return "linux/amd64";
}

// with_target records the platform on the plan. Call it beside with_cmd.
inline void with_target(BuildPlan& plan, const domain::Target& t) {
    plan.platform = docker_platform(t.debarch);
}

// dir_output is one architecture-specific artifact. The image stages the tree;
// the engine asserts its glibc floor and packs it deterministically.
inline Output dir_output(const domain::RecipeArtifact& a, const domain::Target& t, const std::string& version) {
    Output o;
    o.name = a.name;
    o.arch = t.abi;
    o.kind = "dir";
    o.staging_dir = kStagingDir;
    o.file_name = domain::artifact_file_name(a.app, a.name, t.os, t.abi, version);
    return o;
}

// webroot_output is an architecture-free artifact (static files).
inline Output webroot_output(const domain::RecipeArtifact& a, const domain::Target& t, const std::string& version) {
    Output o;
    o.name = a.name;
    o.arch = "all";
    o.kind = "webroot";
    o.staging_dir = kStagingDir;
    o.file_name = domain::artifact_file_name(a.app, a.name, t.os, "all", version);
    return o;
}

}  // namespace vyzorix::plan::adapter
