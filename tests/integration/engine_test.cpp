#include "support/test.hpp"

#include "harness.hpp"
#include "modules/adapter/adapters.hpp"
#include "modules/containerbuild/container_builder.hpp"
#include "modules/plan/catalog.hpp"
#include "okfw/sha256.hpp"

#include <filesystem>
#include <fstream>

using namespace vyzorix;

namespace {

std::filesystem::path scratch(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

// A tiny Python project: one source file and one dependency. It is the cheapest
// real first-class build (a small base image, no toolchain download), and it
// exercises the whole path: bundled Dockerfile, project context, wheel
// resolution, staging, and the engine's pack.
void write_python_project(const std::filesystem::path& dir) {
    std::ofstream(dir / "requirements.txt") << "six\n";
    std::ofstream(dir / "main.py") << "import six\nprint(six.__version__)\n";
}

// every_language is the set the catalog must answer for. It mirrors the recipe
// schema's `lang` enum, so an adapter that exists in the schema but not the
// catalog (or vice versa) is caught here.
const std::vector<std::string>& every_language() {
    static const std::vector<std::string> langs = {
        "golang", "rust", "cpp", "node", "node-webroot", "deno", "bun", "zig",
        "dotnet", "ruby", "php", "python", "jvm", "containerfile",
    };
    return langs;
}

// plan_for builds one adapter's plan for a fixture artifact, or the error.
okfw::Result<plan::BuildPlan> plan_for(const std::string& lang, const domain::RecipeArtifact& a) {
    auto catalog = plan::adapter::built_in_catalog();
    auto adapter = catalog.lookup(lang);
    if (!adapter) return okfw::Result<plan::BuildPlan>::fail(adapter.error());

    auto target = domain::resolve_target("arm64-v8a", "bookworm");
    if (!target) return okfw::Result<plan::BuildPlan>::fail(target.error());

    return adapter.value()->build_plan(a, target.value(), "v1.2.0");
}

okfw::Result<plan::BuildPlan> python_plan() {
    auto catalog = plan::adapter::built_in_catalog();
    auto adapter = catalog.lookup("python");
    if (!adapter) return okfw::Result<plan::BuildPlan>::fail(adapter.error());

    auto target = domain::resolve_target("arm64-v8a", "bookworm");
    if (!target) return okfw::Result<plan::BuildPlan>::fail(target.error());

    domain::RecipeArtifact a;
    a.app = "demo";
    a.name = "app";
    a.lang = "python";
    a.path = ".";
    return adapter.value()->build_plan(a, target.value(), "v1");
}

bool tarball_has(const std::string& tarball, const std::string& needle) {
    auto listing = okfw::run({"tar", "tzf", tarball});
    return listing && listing.value().out.find(needle) != std::string::npos;
}

}  // namespace

VYZ_TEST(integration_engine_packs_a_real_staged_tree) {
    if (!itest::docker_available()) {
        std::fprintf(stderr, "  skip (docker unavailable)\n");
        return;
    }

    auto dir = scratch("vyz-it-py");
    write_python_project(dir);

    auto p = python_plan();
    CHECK(p.has_value());
    if (!p.has_value()) return;

    containerbuild::ContainerBuilder builder(dir.string());
    auto produced = builder.run(p.value(), (std::filesystem::temp_directory_path() / "vyz-it-out").string());
    CHECK(produced.has_value());
    if (!produced.has_value()) return;

    CHECK_EQ(produced.value().size(), static_cast<size_t>(1));
    std::string tarball = produced.value()[0].local_path;
    CHECK(std::filesystem::is_regular_file(tarball));

    // The artifact carries the app and its resolved dependency.
    CHECK(tarball_has(tarball, "app/main.py"));
    CHECK(tarball_has(tarball, "site-packages/six.py"));

    std::filesystem::remove_all(dir);
}

VYZ_TEST(integration_engine_is_deterministic) {
    if (!itest::docker_available()) {
        std::fprintf(stderr, "  skip (docker unavailable)\n");
        return;
    }

    auto dir = scratch("vyz-it-det");
    write_python_project(dir);

    auto p = python_plan();
    CHECK(p.has_value());
    if (!p.has_value()) return;

    // Outputs go outside the project, as the CLI does, so neither build's
    // context includes the other's artifact.
    auto out = scratch("vyz-it-det-out");
    containerbuild::ContainerBuilder builder(dir.string());
    auto first = builder.run(p.value(), (out / "o1").string());
    auto second = builder.run(p.value(), (out / "o2").string());
    CHECK(first.has_value());
    CHECK(second.has_value());
    if (!first.has_value() || !second.has_value()) return;

    std::string h1, h2;
    CHECK(okfw::sha256_file(first.value()[0].local_path, h1));
    CHECK(okfw::sha256_file(second.value()[0].local_path, h2));
    CHECK_EQ(h1, h2);

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(out);
}

VYZ_TEST(integration_engine_refuses_a_tree_above_the_floor) {
    if (!itest::docker_available()) {
        std::fprintf(stderr, "  skip (docker unavailable)\n");
        return;
    }

    // A host binary (a newer glibc than the target floor) is staged with the
    // project, so the engine's assertion must refuse the artifact and name it.
    auto dir = scratch("vyz-it-floor");
    write_python_project(dir);
    std::string host_sh = okfw::which("sh");
    CHECK(!host_sh.empty());
    std::filesystem::copy_file(host_sh, dir / "sh", std::filesystem::copy_options::overwrite_existing);

    auto p = python_plan();
    CHECK(p.has_value());
    if (!p.has_value()) return;
    p.value().libc_floor = "2.0";  // below any real glibc

    containerbuild::ContainerBuilder builder(dir.string());
    auto produced = builder.run(p.value(), (std::filesystem::temp_directory_path() / "vyz-it-out").string());
    CHECK(!produced.has_value());
    if (!produced.has_value()) {
        CHECK(produced.error().message.find("GLIBC_") != std::string::npos);
    }

    std::filesystem::remove_all(dir);
}

VYZ_TEST(integration_engine_copies_an_archive_unchanged) {
    if (!itest::docker_available()) {
        std::fprintf(stderr, "  skip (docker unavailable)\n");
        return;
    }

    // The escape hatch: the recipe's own Containerfile packs the archive.
    auto dir = scratch("vyz-it-archive");
    std::ofstream(dir / "Containerfile")
        << "FROM debian:bookworm-slim\n"
        << "RUN mkdir -p /out/app && printf 'hi\\n' > /out/app/x.txt && "
        << "tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner "
        << "-czf /out/escape-linux-android-arm64-v8a-v1.tar.gz -C /out/app .\n";

    plan::BuildPlan p;
    p.lang = "containerfile";
    p.context = ".";
    p.containerfile = "Containerfile";
    plan::Output o;
    o.name = "escape";
    o.arch = "arm64-v8a";
    o.kind = "dir";
    o.file_name = "escape-linux-android-arm64-v8a-v1.tar.gz";
    o.archive = "/out/" + o.file_name;
    p.outputs = {o};

    containerbuild::ContainerBuilder builder(dir.string());
    auto produced = builder.run(p, (std::filesystem::temp_directory_path() / "vyz-it-out").string());
    CHECK(produced.has_value());
    if (!produced.has_value()) return;

    CHECK(std::filesystem::is_regular_file(produced.value()[0].local_path));
    CHECK(tarball_has(produced.value()[0].local_path, "x.txt"));

    std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------
// One contract test per adapter. These are cheap and Docker-free: they assert
// each adapter resolves a plan whose shape the engine can run — a pinned image,
// a build context inside the project, and at least one output with an explicit
// filename. The real Docker build for each toolchain is the operator's opt-in
// acceptance test (contract §9); what must hold for every adapter, always, is
// that the plan is well-formed.
// ---------------------------------------------------------------------------

VYZ_TEST(integration_every_adapter_plans_a_runnable_build) {
    auto catalog = plan::adapter::built_in_catalog();

    for (const auto& lang : every_language()) {
        auto adapter = catalog.lookup(lang);
        CHECK(adapter.has_value());
        if (!adapter.has_value()) continue;

        domain::RecipeArtifact a;
        a.app = "demo";
        a.name = "app";
        a.lang = lang;
        a.path = ".";
        a.cmd = "/home/apps/demo/run.sh";
        if (lang == "containerfile") a.file = "deploy/Containerfile.arm64";

        auto p = plan_for(lang, a);
        CHECK(p.has_value());
        if (!p.has_value()) continue;

        const auto& plan = p.value();
        CHECK_EQ(plan.lang, lang);
        // Either a pinned bundled image, or the recipe's own Containerfile (the
        // escape hatch). Never neither: a plan with no image and no Containerfile
        // is not runnable. Never both, either — the engine would not know which
        // to build.
        CHECK(plan.image.empty() != plan.containerfile.empty());
        // The context is project-relative; an absolute path would escape.
        CHECK(!plan.context.empty());
        CHECK(plan.context.front() != '/');
        // At least one output, each with an explicit filename (§4.11).
        CHECK(!plan.outputs.empty());
        for (const auto& o : plan.outputs) {
            CHECK(!o.file_name.empty());
            CHECK(o.file_name.size() > 8);
            CHECK_EQ(o.file_name.substr(o.file_name.size() - 7), ".tar.gz");
            CHECK(!o.staging_dir.empty() || !o.archive.empty());
        }
    }
}

VYZ_TEST(integration_containerfile_is_the_only_adapter_needing_a_file) {
    // Every first-class adapter bundles its Dockerfile and needs no `file`; the
    // escape hatch is the one that does. This pins that boundary.
    for (const auto& lang : every_language()) {
        domain::RecipeArtifact a;
        a.app = "demo";
        a.name = "app";
        a.lang = lang;
        a.path = ".";

        auto p = plan_for(lang, a);
        if (lang == "containerfile") {
            CHECK(!p.has_value());  // no `file` -> refusal
        } else {
            CHECK(p.has_value());
            if (p.has_value()) CHECK(p.value().containerfile.empty());
        }
    }
}

VYZ_TEST(integration_adapter_plans_are_deterministic) {
    // The same artifact and target must plan identically, or a digest would drift
    // between two builds of the same source (§4.7).
    domain::RecipeArtifact a;
    a.app = "demo";
    a.name = "app";
    a.path = ".";
    a.build = "make";

    for (const auto& lang : every_language()) {
        if (lang == "containerfile") continue;
        a.lang = lang;

        auto first = plan_for(lang, a);
        auto second = plan_for(lang, a);
        CHECK(first.has_value());
        CHECK(second.has_value());
        if (!first.has_value() || !second.has_value()) continue;

        CHECK_EQ(first.value().image, second.value().image);
        CHECK_EQ(first.value().context, second.value().context);
        CHECK_EQ(first.value().args.size(), second.value().args.size());
        for (size_t i = 0; i < first.value().args.size(); i++) {
            CHECK_EQ(first.value().args[i], second.value().args[i]);
        }
        CHECK_EQ(first.value().outputs[0].file_name, second.value().outputs[0].file_name);
    }
}
