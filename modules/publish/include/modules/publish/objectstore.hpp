// publish — the object-store backend (a public bucket prefix).
#pragma once

#include <memory>

#include "modules/publish/store.hpp"

namespace vyzorix::publish {

// make_object_store builds the object-store backend. `base_url` is read from
// VYZORIX_BUILD_BASE_URL; the upload command from VYZORIX_BUILD_UPLOAD_CMD.
[[nodiscard]] std::unique_ptr<Store> make_object_store(const StoreConfig& config);

}  // namespace vyzorix::publish
