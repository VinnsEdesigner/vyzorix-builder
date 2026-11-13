// domain — a target: the device ABI mapped to everything a build needs.
#pragma once

#include <string>
#include <vector>

#include "okfw/result.hpp"

namespace vyzorix::domain {

struct Target {
    std::string abi;      // "arm64-v8a" — the Android ABI, exact
    std::string os;       // "linux-android" — the target OS ABI name
    std::string debarch;  // "arm64" | "armhf" | "amd64"
    std::string suite;    // "bookworm"
    std::string libc_floor;  // "2.36"
};

// Resolve maps an Android ABI name to a Target. An unknown ABI is a refusal
// naming it, because guessing an architecture is how a phone ends up running a
// foreign binary.
[[nodiscard]] okfw::Result<Target> resolve_target(const std::string& abi, const std::string& suite);

// ResolveAll maps each ABI, failing on the first unknown.
[[nodiscard]] okfw::Result<std::vector<Target>> resolve_targets(const std::vector<std::string>& abis,
                                                               const std::string& suite);

}  // namespace vyzorix::domain
