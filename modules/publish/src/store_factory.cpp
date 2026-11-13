#include "modules/publish/store.hpp"

#include "modules/publish/composite.hpp"
#include "modules/publish/ghcr.hpp"
#include "modules/publish/local.hpp"
#include "modules/publish/objectstore.hpp"

namespace vyzorix::publish {

okfw::Result<std::unique_ptr<Store>> make_store(const StoreConfig& config) {
    StoreConfig c = config;
    if (c.backend.empty()) c.backend = "ghcr";
    if (c.registry.empty()) c.registry = "ghcr.io";

    if (c.backend == "ghcr") {
        if (c.ns.empty() || c.repo.empty()) {
            return okfw::Result<std::unique_ptr<Store>>::fail(
                okfw::error("the ghcr backend needs --namespace and --repo"));
        }
        return okfw::Result<std::unique_ptr<Store>>::ok(make_ghcr_store(c));
    }

    if (c.backend == "object") return okfw::Result<std::unique_ptr<Store>>::ok(make_object_store(c));

    if (c.backend == "composite") {
        // The device store must be one that serves a plain, credentialless URL.
        // `local` cannot (it is a file:// URL), so it is refused here.
        if (c.device_backend == "local") {
            return okfw::Result<std::unique_ptr<Store>>::fail(okfw::error(
                "the composite backend's device store cannot be 'local': it returns a file:// URL the device cannot fetch"));
        }
        StoreConfig dev = c;
        dev.backend = c.device_backend;
        auto device = make_store(dev);
        if (!device) return okfw::Result<std::unique_ptr<Store>>::fail(device.error());

        StoreConfig rec = c;
        rec.backend = c.record_backend;
        auto record = make_store(rec);
        if (!record) return okfw::Result<std::unique_ptr<Store>>::fail(record.error());

        return okfw::Result<std::unique_ptr<Store>>::ok(
            make_composite_store(std::move(device.value()), std::move(record.value())));
    }

    if (c.backend == "local") {
        // The local backend writes a file:// URL, which the device cannot fetch
        // (it holds no credential and speaks HTTPS). It exists for dry runs and
        // tests only; a real deploy must name a device-reachable backend.
        if (!c.dry_run) {
            return okfw::Result<std::unique_ptr<Store>>::fail(okfw::error(
                "the local backend cannot back a real deploy: it returns a file:// URL the device cannot fetch. "
                "Use --publish ghcr (or object), or --dry-run to build without publishing"));
        }
        return okfw::Result<std::unique_ptr<Store>>::ok(make_local_store(c));
    }

    return okfw::Result<std::unique_ptr<Store>>::fail(
        okfw::error("unsupported publish backend '" + c.backend + "' (known: ghcr, object, composite, local)"));
}

}  // namespace vyzorix::publish
