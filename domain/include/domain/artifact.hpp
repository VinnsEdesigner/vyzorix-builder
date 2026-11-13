// domain — a built, published artifact.
#pragma once

#include <cstdint>
#include <string>

namespace vyzorix::domain {

// Artifact is one built, published artifact: the pair a manifest references,
// plus the explicit filename and the registry tag.
struct Artifact {
    std::string name;    // manifest-compatible artifact name
    std::string arch;    // "arm64-v8a" | ... | "all"
    std::string kind;    // "dir" | "webroot"
    std::string url;     // anonymous HTTPS address of the bytes
    std::string sha256;  // the content digest (64 lowercase hex)
    std::string file;    // the explicit filename
    std::string tag;     // the human-facing OCI tag
    std::int64_t size = 0;
};

}  // namespace vyzorix::domain
