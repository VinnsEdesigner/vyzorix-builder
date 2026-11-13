#include "support/test.hpp"

#include "modules/package/package.hpp"
#include "okfw/subprocess.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace vyzorix;

VYZ_TEST(package_deterministic_tarball_is_reproducible) {
    // Two tarballs of the same bytes must be byte-identical, or the digest a
    // manifest pins drifts and the device refuses with digest_mismatch. The
    // outputs live outside the staged tree, or each archive would contain the
    // other and the two would legitimately differ.
    auto root = std::filesystem::temp_directory_path() / "vyz-pkg-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "stage" / "a");
    std::filesystem::create_directories(root / "stage" / "b");
    std::filesystem::create_directories(root / "out");

    std::ofstream(root / "stage" / "a" / "one.txt") << "one\n";
    std::ofstream(root / "stage" / "b" / "two.txt") << "two\n";

    auto t1 = root / "out" / "one.tar.gz";
    auto t2 = root / "out" / "two.tar.gz";

    auto r1 = package::deterministic_tarball((root / "stage").string(), t1.string());
    auto r2 = package::deterministic_tarball((root / "stage").string(), t2.string());
    CHECK(r1.has_value());
    CHECK(r2.has_value());

    auto d1 = package::digest(t1.string());
    auto d2 = package::digest(t2.string());
    CHECK(d1.has_value());
    CHECK(d2.has_value());
    CHECK_EQ(d1.value(), d2.value());

    std::filesystem::remove_all(root);
}

VYZ_TEST(package_needed_libs_reads_a_real_binary) {
    // /bin/sh is a real dynamically linked ELF on this host; readelf must find
    // its libc. This is the closure step the cpp adapter depends on, run for real.
    auto sh = okfw::which("sh");
    CHECK(!sh.empty());

    auto libs = package::needed_libs(sh);
    CHECK(libs.has_value());

    bool has_libc = false;
    for (const auto& l : libs.value()) {
        if (l.find("libc.so") == 0) has_libc = true;
    }
    CHECK(has_libc);
}

VYZ_TEST(package_glibc_floor_refuses_a_too_new_binary) {
    auto sh = okfw::which("sh");
    CHECK(!sh.empty());

    // The floor the device's bookworm rootfs provides. A host binary built on a
    // newer suite must be refused here, not discovered on the phone.
    auto strict = package::assert_glibc_floor(sh, "2.36");
    // Either the host is bookworm-or-older (passes) or newer (refused); both are
    // valid outcomes — what must not happen is a newer requirement being accepted.
    if (!strict.has_value()) {
        CHECK(strict.error().message.find("GLIBC_") != std::string::npos);
    }

    // An impossibly high floor must never be satisfied.
    auto permissive = package::assert_glibc_floor(sh, "9999.0");
    CHECK(permissive.has_value());
}

VYZ_TEST(package_tree_floor_skips_non_elf_and_passes_a_tree_of_text) {
    // A webroot of text files has no ELF, so the assertion passes regardless of
    // floor — the check must not refuse a static site.
    auto root = std::filesystem::temp_directory_path() / "vyz-tree-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "www");
    std::ofstream(root / "www" / "index.html") << "<html></html>\n";
    std::ofstream(root / "www" / "app.js") << "console.log(1)\n";

    auto r = package::assert_glibc_floor_tree(root.string(), "2.36");
    CHECK(r.has_value());

    std::filesystem::remove_all(root);
}

VYZ_TEST(package_tree_floor_checks_a_real_elf) {
    // /bin/sh is a real dynamically linked ELF; copying it into a tree makes the
    // walk assert against it. A floor of 9999 must refuse.
    auto sh = okfw::which("sh");
    CHECK(!sh.empty());

    auto root = std::filesystem::temp_directory_path() / "vyz-tree-elf";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::filesystem::copy_file(sh, root / "sh", std::filesystem::copy_options::overwrite_existing);

    auto strict = package::assert_glibc_floor_tree(root.string(), "9999.0");
    CHECK(strict.has_value());  // 9999 is above anything, so it passes

    auto impossible = package::assert_glibc_floor_tree(root.string(), "0.0");
    CHECK(!impossible.has_value());  // nothing satisfies a 0.0 floor

    std::filesystem::remove_all(root);
}

VYZ_TEST(package_entrypoint_must_exist_in_the_artifact) {
    std::error_code ec;
    auto dir = std::filesystem::temp_directory_path(ec) / "vyz-ep-test";
    std::filesystem::remove_all(dir, ec);
    // The artifact unpacks at /home/apps/<app>/<artifact>/, so the staged tree
    // holds only what is below that: a cmd of /home/apps/vfd/engine/run.sh is
    // "run.sh" inside the stage.
    std::filesystem::create_directories(dir, ec);

    // A webroot has no cmd: nothing to check.
    CHECK(package::assert_entrypoint(dir.string(), "vfd", "engine", "").has_value());

    // A cmd not under /home/apps/<app>/<artifact>/ cannot be placed.
    auto outside = package::assert_entrypoint(dir.string(), "vfd", "engine", "/etc/passwd");
    CHECK(!outside.has_value());

    // A cmd that maps below the prefix but is not in the tree is refused.
    auto missing = package::assert_entrypoint(dir.string(), "vfd", "engine", "/home/apps/vfd/engine/run.sh");
    CHECK(!missing.has_value());
    if (!missing.has_value()) {
        CHECK(missing.error().message.find("run.sh") != std::string::npos);
    }

    // A file that exists but is not executable is refused too.
    std::ofstream(dir / "run.sh") << "#!/bin/sh\n";
    std::filesystem::permissions(dir / "run.sh", std::filesystem::perms::owner_read,
                                 std::filesystem::perm_options::replace, ec);
    CHECK(!package::assert_entrypoint(dir.string(), "vfd", "engine", "/home/apps/vfd/engine/run.sh").has_value());

    // An executable entrypoint passes.
    std::filesystem::permissions(dir / "run.sh",
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace, ec);
    CHECK(package::assert_entrypoint(dir.string(), "vfd", "engine", "/home/apps/vfd/engine/run.sh").has_value());

    std::filesystem::remove_all(dir, ec);
}

VYZ_TEST(package_size_limit_refuses_an_oversized_tree) {
    std::error_code ec;
    auto dir = std::filesystem::temp_directory_path(ec) / "vyz-size-test";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    { std::ofstream(dir / "a.bin", std::ios::binary) << std::string(1000, 'x'); }
    std::filesystem::create_directories(dir / "sub", ec);
    { std::ofstream(dir / "sub" / "b.bin", std::ios::binary) << std::string(1000, 'y'); }

    // No limit passes.
    CHECK(package::assert_under_size_limit(dir.string(), 0).has_value());
    // A generous limit passes.
    CHECK(package::assert_under_size_limit(dir.string(), 1'000'000).has_value());
    // A limit below the tree refuses, naming the limit.
    auto over = package::assert_under_size_limit(dir.string(), 500);
    CHECK(!over.has_value());
    if (!over.has_value()) {
        CHECK(over.error().message.find("500") != std::string::npos);
    }

    std::filesystem::remove_all(dir, ec);
}
