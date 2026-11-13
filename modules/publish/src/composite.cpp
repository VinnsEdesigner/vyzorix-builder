#include "modules/publish/composite.hpp"

#include "okfw/log.hpp"

namespace vyzorix::publish {
namespace {

class CompositeStore final : public Store {
public:
    CompositeStore(std::unique_ptr<Store> device, std::unique_ptr<Store> record)
        : device_(std::move(device)), record_(std::move(record)) {}

    okfw::Result<Published> put(const ArtifactIdentity& artifact) override {
        // The device store first: its URL is the one the manifest pins, so a
        // failure here is the refusal that matters.
        auto published = device_->put(artifact);
        if (!published) return published;

        Published out = published.value();

        // The record store is provenance. A failure is a warning: the artifact is
        // already fetchable, so the build should not fail for a missing label.
        if (record_ != nullptr) {
            auto recorded = record_->put(artifact);
            if (recorded) {
                out.tag = recorded.value().tag;
            } else {
                okfw::log_error("the artifact is published, but the record store did not accept it: " +
                                recorded.error().message);
            }
        }

        return okfw::Result<Published>::ok(std::move(out));
    }

private:
    std::unique_ptr<Store> device_;
    std::unique_ptr<Store> record_;
};

}  // namespace

std::unique_ptr<Store> make_composite_store(std::unique_ptr<Store> device,
                                            std::unique_ptr<Store> record) {
    return std::make_unique<CompositeStore>(std::move(device), std::move(record));
}

}  // namespace vyzorix::publish
