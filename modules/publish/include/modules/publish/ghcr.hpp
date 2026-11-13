// publish — the GHCR (OCI) backend.
#pragma once

#include <memory>
#include <string>

#include "modules/publish/store.hpp"
#include "okfw/result.hpp"

namespace vyzorix::publish {

// ghcr_blob_url is the anonymous fetch address of a blob.
[[nodiscard]] std::string ghcr_blob_url(const std::string& registry,
                                        const std::string& ns,
                                        const std::string& repo,
                                        const std::string& sha256_hex);

// make_ghcr_store builds the GHCR backend.
[[nodiscard]] std::unique_ptr<Store> make_ghcr_store(StoreConfig config);

// header_value reads one case-insensitive header from a block of HTTP response
// headers (as `curl -D -` emits), returning the trimmed value or "".
[[nodiscard]] std::string header_value(const std::string& headers, const std::string& name);

}  // namespace vyzorix::publish
