#include "modules/containerbuild/container_builder.hpp"

#include "domain/paths.hpp"
#include "modules/containerbuild/builder_images.hpp"
#include "modules/containerbuild/image_pins.hpp"
#include "modules/package/package.hpp"
#include "okfw/ids.hpp"
#include "okfw/log.hpp"
#include "okfw/subprocess.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

namespace vyzorix::containerbuild {
namespace {

std::string sanitize(const std::string& s) {
    std::string out;
    for (char c : s) {
        out.push_back((c == '/' || c == ':' || c == ' ') ? '-' : c);
    }
    return out;
}

// DockerCleanup removes the image tag and the container a build created, on every
// exit path — a normal return, an early refusal, or a cancellation unwinding out
// of a signal. A cancelled build must not leave a container or an
// image behind. run_quiet is used because a cleanup failure is not actionable.
class DockerCleanup {
public:
    ~DockerCleanup() {
        if (!container_.empty()) okfw::run_quiet({"docker", "rm", "-f", container_});
        if (!tag_.empty()) okfw::run_quiet({"docker", "rmi", "-f", tag_});
    }

    DockerCleanup(const DockerCleanup&) = delete;
    DockerCleanup& operator=(const DockerCleanup&) = delete;
    DockerCleanup(DockerCleanup&&) = delete;
    DockerCleanup& operator=(DockerCleanup&&) = delete;

    DockerCleanup() = default;

    void set_tag(std::string tag) { tag_ = std::move(tag); }
    void set_container(std::string id) { container_ = std::move(id); }

private:
    std::string tag_;
    std::string container_;
};

// cpus_to_quota converts a `--cpus`-style value ("2", "1.5") to the BuildKit
// `cpu-quota` string (microseconds per 100ms period). A non-numeric value is
// passed through unchanged, so an operator who already knows the quota format is
// not surprised.
std::string cpus_to_quota(const std::string& cpus) {
    try {
        double n = std::stod(cpus);
        if (n <= 0) return cpus;
        return std::to_string(static_cast<long long>(n * 100000.0));
    } catch (const std::exception&) {
        return cpus;
    }
}

}  // namespace

ContextScratch::ContextScratch(std::string context_dir, const std::string& dockerfile)
    : context_dir_(std::move(context_dir)) {
    std::error_code ec;
    std::filesystem::create_directories(context_dir_, ec);

    dockerfile_path_ = (std::filesystem::path(context_dir_) / ".vyzorix-builder.Dockerfile").string();
    std::ofstream out(dockerfile_path_, std::ios::binary | std::ios::trunc);
    out << dockerfile;
    out.close();

    // Augment, do not replace, the project's .dockerignore: keep its lines and
    // append the builder's, so a project that already ignores its build output
    // still does, and its file is restored afterwards.
    ignore_path_ = (std::filesystem::path(context_dir_) / ".dockerignore").string();
    std::ifstream in(ignore_path_, std::ios::binary);
    if (in) {
        std::ostringstream ss;
        ss << in.rdbuf();
        original_ignore_ = ss.str();
        had_ignore_ = true;
    }
    in.close();

    std::ofstream ignore(ignore_path_, std::ios::binary | std::ios::trunc);
    ignore << original_ignore_;
    if (!original_ignore_.empty() && original_ignore_.back() != '\n') ignore << "\n";
    ignore << ".vyzorix-builder.Dockerfile\n.vyzorix-build\n";
    ignore.close();
}

ContextScratch::~ContextScratch() { restore(); }

void ContextScratch::restore() {
    std::error_code ec;
    std::filesystem::remove(dockerfile_path_, ec);
    if (had_ignore_) {
        std::ofstream ignore(ignore_path_, std::ios::binary | std::ios::trunc);
        ignore << original_ignore_;
    } else {
        std::filesystem::remove(ignore_path_, ec);
    }
}


bool ContainerBuilder::docker_available() const {
    auto r = okfw::run({"docker", "info"});
    return r && r.value().exit_code == 0;
}

okfw::Result<std::vector<Produced>> ContainerBuilder::run(const plan::BuildPlan& build_plan,
                                                const std::string& out_dir) const {
    if (build_plan.outputs.empty()) {
        return okfw::Result<std::vector<Produced>>::fail(
            okfw::error("the plan has no outputs to collect"));
    }
    if (okfw::which("docker").empty()) {
        return okfw::Result<std::vector<Produced>>::fail(okfw::error("docker is required to build artifacts"));
    }
    if (!docker_available()) {
        return okfw::Result<std::vector<Produced>>::fail(
            okfw::error("the docker daemon is not reachable; start it, or run the builder where docker works"));
    }

    // The recipe's paths are untrusted. Containment is checked here, before any
    // path is joined onto project_dir_ and handed to docker, so a recipe cannot
    // read the host outside the project.
    if (auto ok = domain::require_project_path("path", build_plan.context); !ok) {
        return okfw::Result<std::vector<Produced>>::fail(ok.error());
    }
    if (auto ok = domain::require_project_path("file", build_plan.containerfile); !ok) {
        return okfw::Result<std::vector<Produced>>::fail(ok.error());
    }

    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    if (ec) return okfw::Result<std::vector<Produced>>::fail(okfw::error("could not create " + out_dir + ": " + ec.message()));

    // 1. Build the image from the project. Both paths do this: a first-class
    //    adapter uses its bundled Dockerfile, and the escape hatch uses the
    //    recipe's own Containerfile. The project is the build context, because
    //    the Dockerfile COPYs it in and runs the build — a pre-baked image with
    //    no project inside could not build anything.
    const std::string context_dir = (std::filesystem::path(project_dir_) / build_plan.context).string();

    std::string tag = "vyzorix-builder-local/" + sanitize(build_plan.outputs.front().name) + ":" + okfw::run_token();
    DockerCleanup cleanup;
    cleanup.set_tag(tag);
    std::vector<std::string> build_argv = {"docker", "build", "-t", tag};

    // Build for the target platform, not the host's: on a
    // multi-arch host this is what makes the artifact the right architecture.
    if (!build_plan.platform.empty()) {
        build_argv.emplace_back("--platform");
        build_argv.emplace_back(build_plan.platform);
    }

    // --pull is opt-in: off by default so a build is reproducible
    // from the local cache; on, an operator picks up a re-published base image.
    if (limits_.pull) {
        build_argv.emplace_back("--pull");
    }

    // Build-time secrets. Each id names an environment variable;
    // BuildKit reads the value from the environment, so it is never in argv and
    // never in a layer. A missing variable is a refusal, not a silent empty
    // secret that would fail the build confusingly.
    for (const auto& id : limits_.secret_ids) {
        const char* value = std::getenv(id.c_str());
        if (value == nullptr || *value == '\0') {
            std::string msg = "build secret '";
            msg += id;
            msg += "' is not set in the environment; set ";
            msg += id;
            msg += " on the builder, or remove it from the request";
            return okfw::Result<std::vector<Produced>>::fail(okfw::error(msg));
        }
        build_argv.emplace_back("--secret");
        std::string spec = "id=";
        spec += id;
        spec += ",env=";
        spec += id;
        build_argv.emplace_back(spec);
    }

    // Resource caps: a runaway or hostile build cannot exhaust the
    // host. BuildKit's `docker build` takes these through `--resource`
    // (`memory=4g`, `cpu-quota=…`), not the run-time `--memory`/`--cpus` flags,
    // which `docker build` rejects. Only the caps the caller set are passed.
    if (!limits_.memory.empty()) {
        build_argv.emplace_back("--resource");
        build_argv.emplace_back("memory=" + limits_.memory);
    }
    if (!limits_.cpus.empty()) {
        // --cpus is a run-time flag; the build equivalent is a CPU quota in
        // microseconds per 100ms period (100000 * cpus).
        build_argv.emplace_back("--resource");
        build_argv.emplace_back("cpu-quota=" + cpus_to_quota(limits_.cpus));
    }
    // (A PID ceiling is not a BuildKit `--resource` key — only memory and
    // cpu-quota are — so it is not passed; the field is reserved for a future
    // runtime that supports it.)

    // A generated Dockerfile is written to the context dir, so the image is
    // self-contained: the binary needs no builders/ directory at run time. The
    // scratch restores the project's own files when the build ends, however it
    // ends.
    std::optional<ContextScratch> scratch;
    if (!build_plan.containerfile.empty()) {
        build_argv.emplace_back("-f");
        build_argv.emplace_back((std::filesystem::path(project_dir_) / build_plan.containerfile).string());
    } else {
        const char* dockerfile = bundled_dockerfile(build_plan.lang);
        if (dockerfile == nullptr) {
            return okfw::Result<std::vector<Produced>>::fail(
                okfw::error("no bundled builder for lang '" + build_plan.lang + "'"));
        }

        scratch.emplace(context_dir, apply_image_pins(dockerfile, image_pins()));
        build_argv.emplace_back("-f");
        build_argv.emplace_back(scratch->dockerfile_path());
    }

    for (const auto& a : build_plan.args) build_argv.emplace_back(a);
    build_argv.emplace_back(context_dir);

    okfw::info("building image for " + build_plan.outputs.front().name);
    // A wall-clock bound: a wedged build is refused, not left to
    // hang the builder forever.
    auto built = okfw::run_with_timeout(build_argv, limits_.timeout_seconds);

    if (!built) return okfw::Result<std::vector<Produced>>::fail(built.error());
    if (built.value().exit_code != 0) {
        return okfw::Result<std::vector<Produced>>::fail(
            okfw::error("the build failed: " + okfw::error_last_line(built.value().err)));
    }

    // 2. Create a container and copy each output out. Copying from a stopped
    //    container is an ordinary docker operation and does not depend on an
    //    exporter that may stall (Vfd found buildx's local exporter does).
    // The image is FROM scratch (no command), so `create` needs one; we only
    // want to copy files out, never run anything.
    auto created = okfw::run({"docker", "create", tag, "true"});
    if (!created) return okfw::Result<std::vector<Produced>>::fail(created.error());
    if (created.value().exit_code != 0) {
        return okfw::Result<std::vector<Produced>>::fail(
            okfw::error("docker create failed: " + okfw::error_first_line(created.value().err)));
    }
    std::string container = okfw::error_first_line(created.value().out);
    cleanup.set_container(container);

    std::vector<Produced> produced;
    for (const auto& out : build_plan.outputs) {
        std::string local = (std::filesystem::path(out_dir) / out.file_name).string();

        if (!out.archive.empty()) {
            // The escape hatch: a Containerfile produced its own archive, which
            // the engine copies out unchanged. The recipe owns its packaging.
            auto copied = okfw::run({"docker", "cp", container + ":" + out.archive, local});
            if (!copied || copied.value().exit_code != 0) {
                std::string why = copied ? okfw::error_first_line(copied.value().err) : copied.error().message;
                return okfw::Result<std::vector<Produced>>::fail(
                    okfw::error("the build produced no " + out.archive + ": " + why));
            }
            produced.push_back(Produced{out.name, out.arch, out.kind, local});
            continue;
        }

        // The normal case: the image left an unpacked tree. The engine is the
        // single packager — it asserts the glibc floor and writes the
        // deterministic tarball, so the packing logic is tested once here rather
        // than re-written in every Dockerfile.
        std::string stage = (std::filesystem::path(out_dir) / (out.name + ".stage")).string();
        std::error_code stage_ec;
        std::filesystem::remove_all(stage, stage_ec);

        auto copied = okfw::run({"docker", "cp", container + ":" + out.staging_dir + "/.", stage});
        if (!copied || copied.value().exit_code != 0) {
            std::string why = copied ? okfw::error_first_line(copied.value().err) : copied.error().message;
            return okfw::Result<std::vector<Produced>>::fail(
                okfw::error("the build produced no " + out.staging_dir + ": " + why));
        }

        if (!build_plan.libc_floor.empty()) {
            auto floor = package::assert_glibc_floor_tree(stage, build_plan.libc_floor);
            if (!floor) {
                std::filesystem::remove_all(stage, stage_ec);
                return okfw::Result<std::vector<Produced>>::fail(floor.error());
            }
        }

        // A build must not fill the host disk: refuse an artifact
        // over the ceiling before it is packed, naming the limit.
        if (auto size_ok = package::assert_under_size_limit(stage, limits_.max_artifact_bytes); !size_ok) {
            std::filesystem::remove_all(stage, stage_ec);
            return okfw::Result<std::vector<Produced>>::fail(size_ok.error());
        }

        // The entrypoint must exist in the artifact: a `cmd` the
        // device cannot run is the failure class this tool exists to prevent.
        if (auto ep = package::assert_entrypoint(stage, build_plan.app, out.name, build_plan.cmd); !ep) {
            std::filesystem::remove_all(stage, stage_ec);
            return okfw::Result<std::vector<Produced>>::fail(ep.error());
        }

        auto packed = package::deterministic_tarball(stage, local);
        std::filesystem::remove_all(stage, stage_ec);
        if (!packed) {
            return okfw::Result<std::vector<Produced>>::fail(packed.error());
        }

        produced.push_back(Produced{out.name, out.arch, out.kind, local});
    }

    return okfw::Result<std::vector<Produced>>::ok(std::move(produced));
}

}  // namespace vyzorix::containerbuild
