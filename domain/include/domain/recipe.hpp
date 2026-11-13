// domain — the build vocabulary: the recipe and its artifacts. No I/O.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vyzorix::domain {

// A recipe is one artifact to build, decoded from vyzorix.build.toml. The
// fields mirror the recipe schema; the schema is
// authoritative and this struct only carries what the builder acts on.
struct RecipeArtifact {
    std::string app;      // the manifest app this artifact belongs to
    std::string name;     // the artifact name, manifest-compatible
    std::string lang;     // the adapter to use: golang, cpp, ...
    std::string cmd;      // the entrypoint, manifest `cmd`
    std::string path;     // project-relative source directory
    std::string build;    // build command, when the adapter needs one
    std::string dist;     // output directory, for a webroot
    std::string file;     // Containerfile path, for the escape hatch
    std::string out;      // output path inside the image, for the escape hatch
    std::string arch;     // "arm64-v8a" | "armeabi-v7a" | "x86_64" | "all" | ""
    std::string kind;     // "dir" | "webroot"
    std::string glibc_floor;
    std::vector<std::string> libs;    // closure to bundle (cpp)
    std::vector<std::string> statics; // libs to link statically (cpp)
    std::map<std::string, std::string> opts;
    int port = 0;
    std::string expose;
    std::map<std::string, std::string> env;
    bool autostart = false;
    bool purge = false;
    bool pwa = false;
};

// Recipe is the whole file: shared settings and the artifacts.
struct Recipe {
    std::string version;  // [build].version
    std::string suite;    // [build].suite, the glibc floor's release
    std::string os;       // [build].os, the target OS ABI name
    std::vector<std::string> abis;  // [build].abi, empty = resolve from device
    std::vector<RecipeArtifact> artifacts;
};

}  // namespace vyzorix::domain
