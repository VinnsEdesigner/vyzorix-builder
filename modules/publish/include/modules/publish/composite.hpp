// publish — the composite backend: a device store plus a provenance record.
//
// The device fetches an artifact with a plain HTTPS GET and no credential of its
// own. An object store (S3/R2 public prefix) serves such a URL directly. A
// container registry does not: GHCR answers an unauthenticated GET with a 401
// challenge even for a public blob, and its redirected CDN URL is signed and
// expires in minutes. So the honest split is:
//
//   - the **device store** (object store) holds the bytes the manifest points at;
//   - the **record store** (GHCR) holds the OCI image, for human browsing and
//     build provenance, at a tag, but is not the fetch address.
//
// This backend runs both in one build: it puts to the device store first (that
// URL is the manifest's), then to the record store for the tag, and returns the
// device URL with the record tag. A record-store failure is a warning, not a
// refusal — the device path already works.
#pragma once

#include <memory>

#include "modules/publish/store.hpp"

namespace vyzorix::publish {

// make_composite_store builds a store that publishes to `device` and records in
// `record`. The returned store's URL is the device store's; its tag is the record
// store's (empty when the record store has none).
[[nodiscard]] std::unique_ptr<Store> make_composite_store(std::unique_ptr<Store> device,
                                                          std::unique_ptr<Store> record);

}  // namespace vyzorix::publish
