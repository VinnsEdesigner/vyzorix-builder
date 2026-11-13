// publish — the Store port: where an artifact's bytes live, anonymously.
#pragma once

#include <memory>
#include <string>

#include "okfw/result.hpp"

namespace vyzorix::publish {

// Published is what a store returns: the anonymous URL of the bytes and the
// content digest the manifest pins. Tag is the human-facing reference the
// backend also created (an OCI tag for a registry), so a person browsing the
// registry can find the artifact; it is not what the device fetches.
struct Published {
    std::string url;
    std::string sha256_hex;
    std::string tag;
};

// ArtifactIdentity is everything a backend needs to store one artifact: the
// explicit filename, the local bytes, the target, and the metadata a registry
// image records as labels. The filename is the identity; the labels are
// provenance.
struct ArtifactIdentity {
    std::string file_name;    // the explicit artifact filename (§4.11)
    std::string local_path;   // the bytes on this host
    std::string arch;         // "arm64-v8a" | "armeabi-v7a" | "x86_64" | "all"
    std::string debarch;      // "arm64" | "armhf" | "amd64", for the OCI platform
    std::string name;         // artifact name (a label)
    std::string kind;         // "dir" | "webroot" (a label)
    std::string glibc_floor;  // the asserted floor (a label)
};

// Store is the port. A backend that cannot return an anonymous URL must fail, not
// return a private one — the device cannot authenticate.
class Store {
public:
    virtual ~Store() = default;
    Store() = default;
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    Store(Store&&) = delete;
    Store& operator=(Store&&) = delete;

    // put stores the artifact and returns where it can be fetched. The URL must
    // be anonymously reachable, or the call is a refusal.
    [[nodiscard]] virtual okfw::Result<Published> put(const ArtifactIdentity& artifact) = 0;
};

// StoreConfig is the CLI's `publish` block.
struct StoreConfig {
    std::string backend;     // "ghcr" | "object" | "local"
    std::string registry;    // "ghcr.io" (default)
    std::string ns;          // owner / namespace
    std::string repo;        // repository
    std::string visibility;  // "public" (required for a device-fetchable URL)
    std::string directory;   // for "local"
    bool dry_run = false;    // a dry run publishes nothing, so "local" is allowed
    // For backend "composite": where the device fetches from, and where the build
    // is recorded. `device` must return a plain, credentialless URL (an object
    // store); `record` is provenance (ghcr) and may fail without failing the build.
    std::string device_backend = "object";
    std::string record_backend = "ghcr";
};

// make_store builds the backend the config names. An unknown backend is a refusal.
// The "local" backend is refused unless this is a dry run: it returns a file://
// URL that the device cannot fetch, so it must never back a real deploy.
[[nodiscard]] okfw::Result<std::unique_ptr<Store>> make_store(const StoreConfig& config);

}  // namespace vyzorix::publish
