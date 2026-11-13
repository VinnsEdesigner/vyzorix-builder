// Integration test harness: real Docker, skipped when it is absent.
//
// These tests exercise the engine end-to-end: a minimal image is built that
// honours an adapter's output contract, tagged as the image that adapter expects,
// and then the engine runs a real plan against it. The language toolchain inside
// the image is not the point — the builder's contract (staging dir vs archive,
// glibc assertion, deterministic tar) is.
#pragma once

#include <cstdlib>
#include <string>
#include <vector>

#include "okfw/result.hpp"
#include "okfw/subprocess.hpp"

namespace itest {

using namespace vyzorix;

// docker_available reports whether a usable docker daemon answers.
inline bool docker_available() {
    auto r = okfw::run({"docker", "info"});
    return r && r.value().exit_code == 0;
}

// run_docker runs a docker subcommand and returns whether it succeeded.
inline bool run_docker(const std::vector<std::string>& args) {
    std::vector<std::string> argv{"docker"};
    argv.insert(argv.end(), args.begin(), args.end());
    auto r = okfw::run(argv);
    return r && r.value().exit_code == 0;
}

// build_stub_image writes a Dockerfile that stages `staging_content` at
// /out/stage and builds it, tagged as `tag`. It returns the working directory
// (for cleanup) or "" on failure.
inline bool build_stub_image(const std::string& dir, const std::string& tag, const std::string& dockerfile_body) {
    std::string df = dir + "/Dockerfile";
    std::FILE* f = std::fopen(df.c_str(), "w");
    if (f == nullptr) return false;
    std::fputs(dockerfile_body.c_str(), f);
    std::fclose(f);

    return run_docker({"build", "-q", "-t", tag, dir});
}

}  // namespace itest
