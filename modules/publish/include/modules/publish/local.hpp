// publish — the local backend, for dev and dry runs only.
#pragma once

#include <memory>

#include "modules/publish/store.hpp"

namespace vyzorix::publish {

// make_local_store builds the local backend, writing under config.directory.
[[nodiscard]] std::unique_ptr<Store> make_local_store(StoreConfig config);

}  // namespace vyzorix::publish
