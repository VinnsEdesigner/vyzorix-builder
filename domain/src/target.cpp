#include "domain/target.hpp"

#include <map>

namespace vyzorix::domain {
namespace {

// kSuiteFloor maps a Debian release to the glibc floor an artifact built on it
// needs. The recipe's `suite` selects this, so the floor the engine asserts is a
// function of the recipe, not a constant the recipe silently ignores (contract
// R11). An unknown suite is a refusal: guessing a floor is how a binary that
// needs a newer glibc than the device has gets published.
//
// The values are the glibc each release ships:
//   bullseye (Debian 11) -> 2.31
//   bookworm (Debian 12) -> 2.36
//   trixie   (Debian 13) -> 2.41
const std::map<std::string, std::string>& kSuiteFloor() {
    static const std::map<std::string, std::string> m = {
        {"bullseye", "2.31"},
        {"bookworm", "2.36"},
        {"trixie", "2.41"},
    };
    return m;
}

}  // namespace

okfw::Result<Target> resolve_target(const std::string& abi, const std::string& suite) {
    static const std::map<std::string, std::string> kDebarch = {
        {"arm64-v8a", "arm64"},
        {"armeabi-v7a", "armhf"},
        {"x86_64", "amd64"},
    };

    auto it = kDebarch.find(abi);
    if (it == kDebarch.end()) {
        return okfw::Result<Target>::fail(
            okfw::error("unsupported abi '" + abi + "' (use arm64-v8a, armeabi-v7a or x86_64)"));
    }

    const std::string resolved_suite = suite.empty() ? "bookworm" : suite;
    auto floor = kSuiteFloor().find(resolved_suite);
    if (floor == kSuiteFloor().end()) {
        return okfw::Result<Target>::fail(okfw::error(
            "unsupported suite '" + resolved_suite +
            "' (use bullseye, bookworm or trixie); the suite selects the glibc floor the artifact must run against"));
    }

    Target t;
    t.abi = abi;
    t.os = "linux-android";
    t.debarch = it->second;
    t.suite = resolved_suite;
    t.libc_floor = floor->second;  // the glibc the suite ships; the engine asserts it
    return okfw::Result<Target>::ok(std::move(t));
}

okfw::Result<std::vector<Target>> resolve_targets(const std::vector<std::string>& abis,
                                                  const std::string& suite) {
    std::vector<Target> out;
    out.reserve(abis.size());
    for (const auto& abi : abis) {
        auto t = resolve_target(abi, suite);
        if (!t) return okfw::Result<std::vector<Target>>::fail(t.error());
        out.push_back(t.value());
    }
    return okfw::Result<std::vector<Target>>::ok(std::move(out));
}

}  // namespace vyzorix::domain
