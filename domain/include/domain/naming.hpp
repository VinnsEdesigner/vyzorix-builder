// domain — the artifact filename policy: <app>-<artifact>-<os>-<arch>-v<version>.tar.gz.
#pragma once

#include <string>
#include <vector>

#include "okfw/result.hpp"

namespace vyzorix::domain {

// ArchSegment is the filename's arch segment: the exact ABI, or "all" for an
// architecture-free artifact (a webroot, or a static site).
[[nodiscard]] std::string arch_segment(const std::string& abi);

// ArtifactFileName builds the explicit filename. version may carry a leading
// "v"; it is normalised so the name never reads "vv1.2.0".
[[nodiscard]] std::string artifact_file_name(const std::string& app,
                                             const std::string& artifact,
                                             const std::string& os,
                                             const std::string& abi,
                                             const std::string& version);

// TagFromFileName is the human-facing registry tag: the filename without its
// archive extension. The durable, content-addressed reference is still the blob
// digest, which is what the manifest pins.
[[nodiscard]] std::string tag_from_file_name(const std::string& file_name);

// RequireUniqueFileNames refuses a build whose artifacts collide on a filename,
// naming both. A collision would make two artifacts overwrite one another in the
// output directory and in the registry, so it is a refusal, not a last-writer
// win. The caller passes every filename the build will produce.
[[nodiscard]] okfw::Result<void> require_unique_file_names(const std::vector<std::string>& file_names);

}  // namespace vyzorix::domain
