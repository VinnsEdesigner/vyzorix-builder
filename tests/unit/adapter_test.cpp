#include "support/test.hpp"

#include "modules/adapter/adapters.hpp"
#include "modules/plan/catalog.hpp"
#include "okfw/toml.hpp"

using namespace vyzorix;

// A one-artifact recipe body the adapter tests reuse.
static domain::RecipeArtifact artifact(const std::string& lang, const std::string& app = "vfd",
                                       const std::string& name = "engine") {
    domain::RecipeArtifact a;
    a.app = app;
    a.name = name;
    a.lang = lang;
    a.build = "make";
    a.path = "src";
    return a;
}

VYZ_TEST(adapter_catalog_knows_every_language) {
    auto catalog = plan::adapter::built_in_catalog();
    for (const char* lang : {"golang", "rust", "cpp", "node", "node-webroot", "deno",
                             "bun", "zig", "dotnet", "ruby", "php", "python", "jvm",
                             "containerfile"}) {
        auto a = catalog.lookup(lang);
        CHECK(a.has_value());
    }

    auto unknown = catalog.lookup("cobol");
    CHECK(!unknown.has_value());
}

VYZ_TEST(adapter_cpp_targets_the_cross_toolchain) {
    auto catalog = plan::adapter::built_in_catalog();
    auto adapter = catalog.lookup("cpp");
    CHECK(adapter.has_value());
    if (!adapter.has_value()) return;

    auto target = domain::resolve_target("arm64-v8a", "bookworm");
    CHECK(target.has_value());
    if (!target.has_value()) return;

    auto plan = adapter.value()->build_plan(artifact("cpp"), target.value(), "v1.2.0");
    CHECK(plan.has_value());
    if (!plan.has_value()) return;

    // The multiarch tuple and Debian arch must be right, or the build links the
    // wrong libc and fails only on the device.
    bool has_tuple = false, has_debarch = false, has_glibc = false;
    for (const auto& arg : plan.value().args) {
        if (arg == "MULTIARCH=aarch64-linux-gnu") has_tuple = true;
        if (arg == "DEB_ARCH=arm64") has_debarch = true;
        if (arg == "GLIBC_FLOOR=2.36") has_glibc = true;
    }
    CHECK(has_tuple);
    CHECK(has_debarch);
    CHECK(has_glibc);

    CHECK_EQ(plan.value().context, "src");
    CHECK_EQ(plan.value().outputs.size(), static_cast<size_t>(1));
    CHECK_EQ(plan.value().outputs[0].file_name, "vfd-engine-linux-android-arm64-v8a-v1.2.0.tar.gz");
    CHECK_EQ(plan.value().outputs[0].staging_dir, "/out/stage");
    CHECK_EQ(plan.value().outputs[0].arch, "arm64-v8a");
}

VYZ_TEST(adapter_golang_is_static_no_closure) {
    auto catalog = plan::adapter::built_in_catalog();
    auto adapter = catalog.lookup("golang");
    auto target = domain::resolve_target("arm64-v8a", "bookworm");

    auto plan = adapter.value()->build_plan(artifact("golang"), target.value(), "v1");
    CHECK(plan.has_value());

    bool cgo_off = false, right_arch = false;
    for (const auto& arg : plan.value().args) {
        if (arg == "CGO_ENABLED=0") cgo_off = true;
        if (arg == "GOARCH=arm64") right_arch = true;
    }
    CHECK(cgo_off);
    CHECK(right_arch);
}

VYZ_TEST(adapter_webroot_is_architecture_free) {
    auto catalog = plan::adapter::built_in_catalog();
    auto adapter = catalog.lookup("node-webroot");
    auto target = domain::resolve_target("arm64-v8a", "bookworm");

    auto plan = adapter.value()->build_plan(artifact("node-webroot", "vfd", "console"), target.value(), "v1.2.0");
    CHECK(plan.has_value());
    CHECK_EQ(plan.value().outputs[0].arch, "all");
    CHECK_EQ(plan.value().outputs[0].file_name, "vfd-console-linux-android-all-v1.2.0.tar.gz");
}

VYZ_TEST(adapter_containerfile_needs_a_file) {
    auto catalog = plan::adapter::built_in_catalog();
    auto adapter = catalog.lookup("containerfile");
    auto target = domain::resolve_target("arm64-v8a", "bookworm");

    // No `file` in the artifact -> refusal.
    auto bad = adapter.value()->build_plan(artifact("containerfile"), target.value(), "v1");
    CHECK(!bad.has_value());

    auto with_file = artifact("containerfile");
    with_file.file = "deploy/Containerfile.arm64";
    auto ok = adapter.value()->build_plan(with_file, target.value(), "v1");
    CHECK(ok.has_value());
    CHECK_EQ(ok.value().containerfile, "deploy/Containerfile.arm64");
}

VYZ_TEST(adapter_rust_uses_the_musl_target) {
    auto catalog = plan::adapter::built_in_catalog();
    auto adapter = catalog.lookup("rust");
    CHECK(adapter.has_value());
    if (!adapter.has_value()) return;

    auto target = domain::resolve_target("arm64-v8a", "bookworm");
    CHECK(target.has_value());
    if (!target.has_value()) return;

    auto plan = adapter.value()->build_plan(artifact("rust"), target.value(), "v1");
    CHECK(plan.has_value());
    if (!plan.has_value()) return;

    bool musl = false;
    for (const auto& arg : plan.value().args) {
        if (arg == "RUST_TARGET=aarch64-unknown-linux-musl") musl = true;
    }
    CHECK(musl);
    CHECK_EQ(plan.value().outputs[0].arch, "arm64-v8a");
}

VYZ_TEST(adapter_python_targets_the_manylinux_platform) {
    auto catalog = plan::adapter::built_in_catalog();
    auto adapter = catalog.lookup("python");
    CHECK(adapter.has_value());
    if (!adapter.has_value()) return;

    auto target = domain::resolve_target("arm64-v8a", "bookworm");
    CHECK(target.has_value());
    if (!target.has_value()) return;

    auto plan = adapter.value()->build_plan(artifact("python"), target.value(), "v1");
    CHECK(plan.has_value());
    if (!plan.has_value()) return;

    bool platform = false, pyver = false;
    for (const auto& arg : plan.value().args) {
        if (arg == "PY_PLATFORM=manylinux2014_aarch64") platform = true;
        if (arg == "PY_VER=311") pyver = true;
    }
    CHECK(platform);
    CHECK(pyver);
}

VYZ_TEST(adapter_jvm_defaults_to_native_image) {
    auto catalog = plan::adapter::built_in_catalog();
    auto adapter = catalog.lookup("jvm");
    CHECK(adapter.has_value());
    if (!adapter.has_value()) return;

    auto target = domain::resolve_target("arm64-v8a", "bookworm");
    CHECK(target.has_value());
    if (!target.has_value()) return;

    auto plan = adapter.value()->build_plan(artifact("jvm"), target.value(), "v1");
    CHECK(plan.has_value());
    if (!plan.has_value()) return;

    bool native = false;
    for (const auto& arg : plan.value().args) {
        if (arg == "JVM_MODE=native-image") native = true;
    }
    CHECK(native);
}

VYZ_TEST(adapter_jvm_opts_select_the_jar_fallback) {
    auto catalog = plan::adapter::built_in_catalog();
    auto adapter = catalog.lookup("jvm");
    auto target = domain::resolve_target("arm64-v8a", "bookworm");
    if (!adapter.has_value() || !target.has_value()) return;

    auto a = artifact("jvm");
    a.opts["jvm"] = "jar";

    auto plan = adapter.value()->build_plan(a, target.value(), "v1");
    CHECK(plan.has_value());
    if (!plan.has_value()) return;

    bool jar = false;
    for (const auto& arg : plan.value().args) {
        if (arg == "JVM_MODE=jar") jar = true;
    }
    CHECK(jar);
}

VYZ_TEST(adapter_cpp_passes_the_static_escape_and_libs) {
    auto catalog = plan::adapter::built_in_catalog();
    auto adapter = catalog.lookup("cpp");
    auto target = domain::resolve_target("x86_64", "bookworm");
    if (!adapter.has_value() || !target.has_value()) return;

    auto a = artifact("cpp");
    a.libs = {"libcurl", "libsqlite3"};
    a.statics = {"ffmpeg"};
    a.glibc_floor = "2.31";

    auto plan = adapter.value()->build_plan(a, target.value(), "v1");
    CHECK(plan.has_value());
    if (!plan.has_value()) return;

    bool libs = false, statics = false, floor = false, debarch = false;
    for (const auto& arg : plan.value().args) {
        if (arg == "LIBS=libcurl,libsqlite3") libs = true;
        if (arg == "STATIC_LIBS=ffmpeg") statics = true;
        if (arg == "GLIBC_FLOOR=2.31") floor = true;
        if (arg == "DEB_ARCH=amd64") debarch = true;
    }
    CHECK(libs);
    CHECK(statics);
    CHECK(floor);
    CHECK(debarch);
}

VYZ_TEST(adapter_webroot_build_cmd_reaches_the_plan) {
    auto catalog = plan::adapter::built_in_catalog();
    auto adapter = catalog.lookup("node-webroot");
    auto target = domain::resolve_target("arm64-v8a", "bookworm");
    if (!adapter.has_value() || !target.has_value()) return;

    auto a = artifact("node-webroot", "vfd", "console");
    a.build = "npm ci && npm run build";
    a.dist = "dist";

    auto plan = adapter.value()->build_plan(a, target.value(), "v1");
    CHECK(plan.has_value());
    if (!plan.has_value()) return;

    bool cmd = false, dist = false;
    for (const auto& arg : plan.value().args) {
        if (arg == "BUILD_CMD=npm ci && npm run build") cmd = true;
        if (arg == "DIST=dist") dist = true;
    }
    CHECK(cmd);
    CHECK(dist);
}

VYZ_TEST(adapter_carries_the_entrypoint_onto_the_plan) {
    // The engine asserts the entrypoint exists in the artifact (contract R20), so
    // the adapter must put the recipe's `cmd` on the plan. A dropped cmd would
    // silently disable that assertion.
    auto catalog = plan::adapter::built_in_catalog();
    auto target = domain::resolve_target("arm64-v8a", "bookworm");
    if (!target.has_value()) return;

    for (const char* lang : {"golang", "rust", "cpp", "python", "jvm", "node", "php", "ruby"}) {
        auto adapter = catalog.lookup(lang);
        CHECK(adapter.has_value());
        if (!adapter.has_value()) continue;

        auto a = artifact(lang);
        a.cmd = "/home/apps/vfd/engine/run.sh";

        auto plan = adapter.value()->build_plan(a, target.value(), "v1");
        CHECK(plan.has_value());
        if (plan.has_value()) CHECK_EQ(plan.value().cmd, std::string("/home/apps/vfd/engine/run.sh"));
    }
}

VYZ_TEST(adapter_platform_only_on_the_escape_hatch) {
    // A first-class adapter cross-compiles natively on the host, so it must NOT
    // set --platform: forcing the target platform would demand emulation and
    // break the build (a real failure seen on an x86_64 host). The escape hatch
    // owns its build and is the one place a platform is set (contract R9).
    auto catalog = plan::adapter::built_in_catalog();
    auto target = domain::resolve_target("arm64-v8a", "bookworm");
    if (!target.has_value()) return;

    for (const char* lang : {"golang", "rust", "cpp", "python", "jvm", "node", "node-webroot"}) {
        auto adapter = catalog.lookup(lang);
        CHECK(adapter.has_value());
        if (!adapter.has_value()) continue;

        auto plan = adapter.value()->build_plan(artifact(lang), target.value(), "v1");
        CHECK(plan.has_value());
        if (plan.has_value()) CHECK(plan.value().platform.empty());
    }

    auto cf = catalog.lookup("containerfile");
    auto a = artifact("containerfile");
    a.file = "deploy/Containerfile.arm64";
    auto plan = cf.value()->build_plan(a, target.value(), "v1");
    CHECK(plan.has_value());
    if (plan.has_value()) CHECK_EQ(plan.value().platform, std::string("linux/arm64"));
}
