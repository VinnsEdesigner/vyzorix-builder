#include "modules/package/package.hpp"

#include "okfw/sha256.hpp"
#include "okfw/subprocess.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>

namespace vyzorix::package {

okfw::Result<void> deterministic_tarball(const std::string& staging_dir, const std::string& out_file) {
    // --sort=name and a fixed --mtime make the archive reproducible; numeric
    // ownership with id 0 keeps the bytes independent of the builder's user.
    auto r = okfw::run({"tar",
                        "--sort=name",
                        "--mtime=@0",
                        "--owner=0",
                        "--group=0",
                        "--numeric-owner",
                        "-czf", out_file,
                        "-C", staging_dir,
                        "."});
    if (!r) return okfw::Result<void>::fail(r.error());
    if (r.value().exit_code != 0) {
        return okfw::Result<void>::fail(okfw::error("tar failed: " + okfw::error_first_line(r.value().err)));
    }
    return okfw::Result<void>::ok();
}

okfw::Result<std::string> digest(const std::string& file) {
    std::string hex;
    if (!okfw::sha256_file(file, hex)) {
        return okfw::Result<std::string>::fail(okfw::error("could not read " + file + " for hashing"));
    }
    return okfw::Result<std::string>::ok(std::move(hex));
}

okfw::Result<std::string> assert_glibc_floor(const std::string& binary, const std::string& floor) {
    // --dyn-syms lists the versioned symbols the binary requires, e.g.
    // GLIBC_2.34. The highest one is the floor the artifact needs.
    auto r = okfw::run({"readelf", "--dyn-syms", "--wide", binary});
    if (!r) return okfw::Result<std::string>::fail(r.error());
    if (r.value().exit_code != 0) {
        return okfw::Result<std::string>::fail(
            okfw::error("readelf failed on " + binary + ": " + okfw::error_first_line(r.value().err)));
    }

    static const std::regex glibc("GLIBC_([0-9]+)\\.([0-9]+)");
    int hi_major = 0, hi_minor = 0;
    for (auto it = std::sregex_iterator(r.value().out.begin(), r.value().out.end(), glibc);
         it != std::sregex_iterator(); ++it) {
        int major = static_cast<int>(std::strtol((*it)[1].str().c_str(), nullptr, 10));
        int minor = static_cast<int>(std::strtol((*it)[2].str().c_str(), nullptr, 10));
        if (major > hi_major || (major == hi_major && minor > hi_minor)) {
            hi_major = major;
            hi_minor = minor;
        }
    }

    if (hi_major == 0) return okfw::Result<std::string>::ok("static");  // no dynamic glibc refs

    int floor_major = 2, floor_minor = 0;
    std::sscanf(floor.c_str(), "%d.%d", &floor_major, &floor_minor);

    std::string found = std::to_string(hi_major) + "." + std::to_string(hi_minor);
    if (hi_major > floor_major || (hi_major == floor_major && hi_minor > floor_minor)) {
        return okfw::Result<std::string>::fail(okfw::error(
            "the binary needs GLIBC_" + found + " but the target floor is GLIBC_" + floor +
            " — build against an older suite (bookworm) or link statically"));
    }

    return okfw::Result<std::string>::ok(found);
}

okfw::Result<std::vector<std::string>> needed_libs(const std::string& binary) {
    auto r = okfw::run({"readelf", "-d", "--wide", binary});
    if (!r) return okfw::Result<std::vector<std::string>>::fail(r.error());
    if (r.value().exit_code != 0) {
        return okfw::Result<std::vector<std::string>>::fail(
            okfw::error("readelf -d failed on " + binary + ": " + okfw::error_first_line(r.value().err)));
    }

    static const std::regex needed(R"(\(NEEDED\)\s+Shared library: \[([^\]]+)\])");
    std::vector<std::string> libs;
    std::set<std::string> seen;
    for (auto it = std::sregex_iterator(r.value().out.begin(), r.value().out.end(), needed);
         it != std::sregex_iterator(); ++it) {
        std::string lib = (*it)[1].str();
        if (seen.insert(lib).second) libs.push_back(lib);
    }
    return okfw::Result<std::vector<std::string>>::ok(std::move(libs));
}

okfw::Result<void> assert_glibc_floor_tree(const std::string& dir, const std::string& floor) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return okfw::Result<void>::fail(okfw::error("not a directory: " + dir));
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;

        const std::string path = entry.path().string();
        // Only ELF files carry a DT_NEEDED / GLIBC version requirement; a text
        // or webroot file is skipped. readelf on a non-ELF prints nothing and
        // exits non-zero, which assert_glibc_floor would treat as a refusal, so
        // the magic bytes decide first.
        std::ifstream f(path, std::ios::binary);
        unsigned char magic[4] = {0, 0, 0, 0};
        f.read(reinterpret_cast<char*>(magic), 4);
        if (magic[0] != 0x7f || magic[1] != 'E' || magic[2] != 'L' || magic[3] != 'F') continue;

        auto r = assert_glibc_floor(path, floor);
        if (!r) {
            return okfw::Result<void>::fail(
                okfw::error(entry.path().filename().string() + ": " + r.error().message));
        }
    }

    return okfw::Result<void>::ok();
}

okfw::Result<void> assert_entrypoint(const std::string& staging_dir, const std::string& app,
                                     const std::string& artifact, const std::string& cmd) {
    if (cmd.empty()) return okfw::Result<void>::ok();  // a webroot has no entrypoint

    // The device unpacks the artifact at /home/apps/<app>/<artifact>/, so the
    // cmd must start there; the remainder names a file inside the staged tree.
    const std::string prefix = "/home/apps/" + app + "/" + artifact + "/";
    if (cmd.rfind(prefix, 0) != 0) {
        return okfw::Result<void>::fail(okfw::error(
            "the recipe's cmd \"" + cmd + "\" is not under \"" + prefix +
            "\" (the device unpacks this artifact there); the device would fail to start the app"));
    }

    std::string rel = cmd.substr(prefix.size());
    std::filesystem::path path = std::filesystem::path(staging_dir) / rel;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return okfw::Result<void>::fail(okfw::error(
            "the recipe's cmd \"" + cmd + "\" is not in the built artifact (looked for \"" +
            path.string() + "\"); the device would fail to start the app"));
    }

    // A file that exists but cannot be executed is the same failure one step
    // later; refuse it here too.
    auto perms = std::filesystem::status(path, ec).permissions();
    if ((perms & std::filesystem::perms::owner_exec) == std::filesystem::perms::none &&
        (perms & std::filesystem::perms::group_exec) == std::filesystem::perms::none &&
        (perms & std::filesystem::perms::others_exec) == std::filesystem::perms::none) {
        return okfw::Result<void>::fail(okfw::error(
            "the recipe's cmd \"" + cmd + "\" is in the artifact but not executable"));
    }

    return okfw::Result<void>::ok();
}

okfw::Result<void> assert_under_size_limit(const std::string& staging_dir, long long limit_bytes) {
    if (limit_bytes <= 0) return okfw::Result<void>::ok();  // no limit

    std::error_code ec;
    long long total = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(staging_dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        total += static_cast<long long>(entry.file_size(ec));
        // Stop early: once the total is over the limit, the exact size does not
        // matter, and a hostile tree could be enormous.
        if (total > limit_bytes) {
            return okfw::Result<void>::fail(okfw::error(
                "the built artifact exceeds the size limit (" + std::to_string(limit_bytes) +
                " bytes); raise the limit or shrink the artifact"));
        }
    }
    return okfw::Result<void>::ok();
}

}  // namespace vyzorix::package
