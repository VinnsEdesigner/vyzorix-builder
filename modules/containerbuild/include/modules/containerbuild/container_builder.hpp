// containerbuild — running a plan in a container, via the docker CLI.
#pragma once

#include <string>
#include <vector>

#include "modules/plan/adapter.hpp"
#include "modules/plan/plan.hpp"
#include "okfw/result.hpp"

namespace vyzorix::containerbuild {

// Produced is one output copied out of the image.
struct Produced {
    std::string name;
    std::string arch;
    std::string kind;
    std::string local_path;  // the file on this host
};

// BuildLimits bounds what a build may consume, so a runaway or hostile build
// cannot hang or exhaust the host. Zero means "no bound"
// for that field, which is the caller's explicit choice, not a default.
struct BuildLimits {
    int timeout_seconds = 0;      // wall-clock bound on the docker build
    std::string memory;           // e.g. "4g"  -> docker --memory
    std::string cpus;             // e.g. "2"   -> docker --cpus
    int pids_limit = 0;           // >0 -> docker --pids-limit
    bool pull = false;            // docker build --pull (off by default, R10)
    long long max_artifact_bytes = 0;  // >0 -> refuse an artifact larger than this (R5)
    // Build-time secret ids. Each names an environment variable on
    // the builder; it becomes a BuildKit `--secret id=<id>,env=<id>`, so the value
    // is read from the environment and never appears in argv or an image layer.
    std::vector<std::string> secret_ids;
};

// ContainerBuilder runs build plans in a container.
class ContainerBuilder {
public:
    explicit ContainerBuilder(std::string project_dir) : project_dir_(std::move(project_dir)) {}
    ContainerBuilder(std::string project_dir, BuildLimits limits)
        : project_dir_(std::move(project_dir)), limits_(std::move(limits)) {}

    // run builds the plan's image and copies each output to out_dir. It returns
    // the local paths, which the publish layer then stores.
    [[nodiscard]] okfw::Result<std::vector<Produced>> run(const plan::BuildPlan& build_plan,
                                                          const std::string& out_dir) const;

    // docker_available reports whether a usable docker daemon answers.
    [[nodiscard]] bool docker_available() const;

private:
    std::string project_dir_;
    BuildLimits limits_;
};

// prepare_context writes a bundled Dockerfile into `context_dir` and augments the
// context's .dockerignore (keeping any lines the project already had), so the
// generated Dockerfile and the build output stay out of the context. It returns a
// RAII handle: destroying it restores the project's own .dockerignore and removes
// the generated Dockerfile, on success, failure, or an early return.
class ContextScratch {
public:
    ContextScratch(std::string context_dir, const std::string& dockerfile);
    ~ContextScratch();

    ContextScratch(const ContextScratch&) = delete;
    ContextScratch& operator=(const ContextScratch&) = delete;
    ContextScratch(ContextScratch&&) = delete;
    ContextScratch& operator=(ContextScratch&&) = delete;

    [[nodiscard]] const std::string& dockerfile_path() const { return dockerfile_path_; }

private:
    void restore();

    std::string context_dir_;
    std::string dockerfile_path_;
    std::string ignore_path_;
    std::string original_ignore_;
    bool had_ignore_ = false;
};

}  // namespace vyzorix::containerbuild
