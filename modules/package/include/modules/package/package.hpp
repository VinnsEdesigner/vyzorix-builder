// package — deterministic tar, digest, glibc-floor assertion, closure.
#pragma once

#include <string>
#include <vector>

#include "okfw/result.hpp"

namespace vyzorix::package {

// DeterministicTarball writes <staging-dir> to <out-file> as a gzip'd tar with
// sorted entries, a fixed mtime and uid/gid 0. Two runs over equal input bytes
// produce equal output, which is what makes the digest stable.
[[nodiscard]] okfw::Result<void> deterministic_tarball(const std::string& staging_dir, const std::string& out_file);

// Digest is the lowercase hex sha256 of a file.
[[nodiscard]] okfw::Result<std::string> digest(const std::string& file);

// GlibcFloor reads the highest GLIBC_x.y symbol a binary needs and refuses when
// it is above the target floor. A binary that links a newer glibc fails only on
// the device, with `version 'GLIBC_2.xx' not found` — so this assertion is the
// whole point of building against bookworm.
[[nodiscard]] okfw::Result<std::string> assert_glibc_floor(const std::string& binary, const std::string& floor);

// assert_glibc_floor_tree walks a staged artifact directory and asserts the
// glibc floor on every dynamically linked ELF file it finds. A tree with no
// such file (a webroot, a static binary) passes. It names the first offending
// file, so a bad link is fixed by name rather than by bisecting the build.
[[nodiscard]] okfw::Result<void> assert_glibc_floor_tree(const std::string& dir, const std::string& floor);

// assert_entrypoint checks that `cmd` names a file inside the staged artifact
// that is executable. The manifest's `cmd` is what the device runs, so a `cmd`
// that does not exist in the artifact is a build-time refusal, not a device-time
// failure (contract R20). A webroot (no `cmd`) is not checked.
//
// The device unpacks the artifact's tree at `<appsRoot>/<app>/<artifact.name>`
// (DeployApplier.destinationOf), and `<appsRoot>` is `/home/apps`. So a `cmd` of
// the form `/home/apps/<app>/<artifact>/<rel>` names `<rel>` inside the staged
// tree, and the staged tree's root is `<stage>`. A `cmd` that is not under
// `/home/apps/` cannot be placed, so it is refused with that reason.
[[nodiscard]] okfw::Result<void> assert_entrypoint(const std::string& staging_dir,
                                                   const std::string& app,
                                                   const std::string& artifact,
                                                   const std::string& cmd);

// assert_under_size_limit refuses when the total size of the staged tree exceeds
// `limit_bytes`. A build must not fill the host disk (contract R5): a runaway
// build is caught here, naming the limit, rather than after it has written a
// hundred gigabytes. `limit_bytes <= 0` means no limit.
[[nodiscard]] okfw::Result<void> assert_under_size_limit(const std::string& staging_dir,
                                                         long long limit_bytes);

// NeededLibs returns the DT_NEEDED shared libraries of a binary, in order.
[[nodiscard]] okfw::Result<std::vector<std::string>> needed_libs(const std::string& binary);

}  // namespace vyzorix::package
