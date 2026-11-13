// schema — the recipe document and the CLI <-> builder JSON contract.
#pragma once

#include <string>
#include <vector>

#include "domain/recipe.hpp"
#include "okfw/json.hpp"
#include "okfw/result.hpp"
#include "okfw/toml.hpp"

namespace vyzorix::schema {

// parse_recipe maps a decoded vyzorix.build.toml into the domain model. It
// enforces the constraints the recipe schema enforces (a non-empty app/name, a
// known shape), so the builder refuses what the schema would, with the same
// field path discipline.
[[nodiscard]] okfw::Result<domain::Recipe> parse_recipe(const okfw::TomlTable& doc);

// PublishArtifact names one already-built file to publish, with the identity the
// manifest needs. It is the input to `vyzorix-builder publish`.
struct PublishArtifact {
    std::string name;
    std::string arch;
    std::string kind;
    std::string file;  // path to the built artifact on this host
};

// BuildRequest is what the CLI sends the builder.
struct BuildRequest {
    std::string recipe;       // the raw recipe bytes
    std::string schema_url;   // where the recipe schema is served
    std::string project_dir;
    std::string version;
    std::string device_id;
    std::vector<std::string> abis;
    // publish
    std::string backend;
    std::string ns;
    std::string repo;
    std::string visibility;
    std::string directory;                   // for the local backend
    std::string device_backend;              // for the composite backend: the device store
    std::string record_backend;              // for the composite backend: the provenance store
    std::vector<PublishArtifact> artifacts;  // for `publish`; empty for `build`
    bool dry_run = false;
    // Build limits (contract R2/R4/R5). Zero/empty means "no bound".
    int timeout_seconds = 0;
    std::string memory;   // e.g. "4g"
    std::string cpus;     // e.g. "2"
    int pids_limit = 0;
    bool pull = false;    // docker build --pull (contract R10)
    long long max_artifact_bytes = 0;  // >0 -> a size ceiling on the artifact (R5)
    // Build-time secret ids (contract R18). Each names an environment variable on
    // the builder; the value is read there and passed as a BuildKit --secret, so
    // it never appears in argv, in an image layer, or in a log.
    std::vector<std::string> secret_ids;
};

// decode_request parses the CLI's request JSON.
[[nodiscard]] okfw::Result<BuildRequest> decode_request(const std::string& json);

// BuiltArtifact is one published artifact in the result document.
struct BuiltArtifact {
    std::string name;
    std::string arch;
    std::string kind;
    std::string url;
    std::string sha256;
    std::string file;
    std::string tag;  // the human-facing registry tag, when the backend is a registry
    long long size = 0;
};

// encode_result writes the result JSON, the only thing the builder writes to
// stdout (its logs go to stderr). `builder_version` is recorded so the artifact's
// provenance says which builder produced it (contract R12).
[[nodiscard]] std::string encode_result(const std::vector<BuiltArtifact>& artifacts,
                                        const std::string& builder_version);

// PlannedArtifact is one artifact the plan step resolved: the adapter, the image,
// the outputs and the target. `vyzorix-builder plan` prints these, so an operator
// can see what a build would do without running Docker.
struct PlannedArtifact {
    std::string app;
    std::string name;
    std::string lang;
    std::string arch;         // the ABI this plan targets
    std::string image;
    std::string context;
    std::string containerfile;
    std::string libc_floor;
    std::vector<std::string> file_names;  // the explicit artifact filenames
};

// encode_plan writes the plan JSON for `vyzorix-builder plan`.
[[nodiscard]] std::string encode_plan(const std::vector<PlannedArtifact>& planned);

}  // namespace vyzorix::schema
