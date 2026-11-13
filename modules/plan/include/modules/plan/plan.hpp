// plan — what an adapter decides: the image, args and outputs for one artifact.
#pragma once

#include <string>
#include <vector>

namespace vyzorix::plan {

// Output is one artifact a plan produces. The engine turns it into the final
// tarball: an output either names a staging directory the image leaves unpacked
// (the normal case, which the engine asserts and packs deterministically), or a
// pre-made archive (the Containerfile escape hatch, which packs its own).
struct Output {
    std::string name;         // artifact name, manifest-compatible
    std::string arch;         // "arm64-v8a" | ... | "all"
    std::string kind;         // "dir" | "webroot"
    std::string staging_dir;  // image path of the unpacked tree; the engine packs it
    std::string archive;      // image path of a pre-made tarball (escape hatch)
    std::string file_name;    // the explicit artifact filename the engine must write
};

// BuildPlan is everything the engine needs to run one artifact's build.
struct BuildPlan {
    std::string lang;                           // the adapter's language; selects the bundled Dockerfile
    std::string image;                          // the tag the built image is given
    std::string context;                        // project-relative build context ("." = the project root)
    std::vector<std::string> args;              // computed cross-toolchain args
    std::string containerfile;                  // when non-empty, use this instead
    std::string libc_floor;                     // the glibc floor the engine asserts on a staged tree
    std::string app;                            // the app name, for the entrypoint's device path
    std::string cmd;                            // the entrypoint the engine asserts exists in the artifact
    std::string platform;                       // e.g. "linux/arm64"; the docker --platform value (contract R9)
    std::vector<Output> outputs;                // what to collect
};

}  // namespace vyzorix::plan
