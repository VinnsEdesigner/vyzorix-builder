#include "support/test.hpp"

#include "modules/containerbuild/container_builder.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace vyzorix;

namespace {

std::filesystem::path scratch_dir(const std::string& name) {
    std::error_code ec;
    auto dir = std::filesystem::temp_directory_path(ec) / name;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

VYZ_TEST(context_scratch_writes_the_dockerfile_and_augments_a_missing_ignore) {
    auto dir = scratch_dir("vyz-ctx-none");

    {
        containerbuild::ContextScratch scratch(dir.string(), "FROM scratch\n");
        // The generated Dockerfile exists while the scratch is alive.
        CHECK(std::filesystem::exists(scratch.dockerfile_path()));
        CHECK(read_file(scratch.dockerfile_path()).find("FROM scratch") != std::string::npos);

        // The .dockerignore now excludes the generated file and the build output.
        std::string ignore = read_file(dir / ".dockerignore");
        CHECK(ignore.find(".vyzorix-builder.Dockerfile") != std::string::npos);
        CHECK(ignore.find(".vyzorix-build") != std::string::npos);
    }

    // On scope exit: the generated Dockerfile is gone, and the .dockerignore the
    // builder created is removed (the project had none).
    CHECK(!std::filesystem::exists(dir / ".vyzorix-builder.Dockerfile"));
    CHECK(!std::filesystem::exists(dir / ".dockerignore"));

    std::filesystem::remove_all(dir);
}

VYZ_TEST(context_scratch_preserves_the_projects_own_dockerignore) {
    auto dir = scratch_dir("vyz-ctx-own");
    const std::string original = "node_modules\n*.log\n";
    { std::ofstream f(dir / ".dockerignore", std::ios::binary); f << original; }

    {
        containerbuild::ContextScratch scratch(dir.string(), "FROM scratch\n");
        // While building, the project's lines are still honoured and the
        // builder's are added — the project's own ignores are not lost.
        std::string ignore = read_file(dir / ".dockerignore");
        CHECK(ignore.find("node_modules") != std::string::npos);
        CHECK(ignore.find("*.log") != std::string::npos);
        CHECK(ignore.find(".vyzorix-builder.Dockerfile") != std::string::npos);
    }

    // On scope exit the project's file is byte-for-byte what it was.
    CHECK_EQ(read_file(dir / ".dockerignore"), original);
    CHECK(!std::filesystem::exists(dir / ".vyzorix-builder.Dockerfile"));

    std::filesystem::remove_all(dir);
}

VYZ_TEST(context_scratch_restores_even_without_a_trailing_newline) {
    auto dir = scratch_dir("vyz-ctx-nonl");
    const std::string original = "vendor";  // no trailing newline
    { std::ofstream f(dir / ".dockerignore", std::ios::binary); f << original; }

    {
        containerbuild::ContextScratch scratch(dir.string(), "FROM scratch\n");
        // The builder's lines must start on their own line, or "vendor" would
        // become "vendor.vyzorix-builder.Dockerfile".
        std::string ignore = read_file(dir / ".dockerignore");
        CHECK(ignore.find("vendor\n") != std::string::npos);
    }

    CHECK_EQ(read_file(dir / ".dockerignore"), original);

    std::filesystem::remove_all(dir);
}
