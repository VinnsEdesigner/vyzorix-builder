#include "modules/publish/local.hpp"

#include "domain/naming.hpp"
#include "okfw/log.hpp"
#include "okfw/sha256.hpp"

#include <filesystem>
#include <fstream>

namespace vyzorix::publish {
namespace {

class LocalStore final : public Store {
public:
    explicit LocalStore(StoreConfig config) : config_(std::move(config)) {}

    okfw::Result<Published> put(const ArtifactIdentity& artifact) override {
        std::string dir = config_.directory.empty() ? "dist" : config_.directory;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) return okfw::Result<Published>::fail(okfw::error("could not create " + dir + ": " + ec.message()));

        std::filesystem::path dest = std::filesystem::path(dir) / artifact.file_name;
        std::filesystem::copy_file(artifact.local_path, dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) return okfw::Result<Published>::fail(okfw::error("could not copy to " + dest.string() + ": " + ec.message()));

        std::string hex;
        if (!okfw::sha256_file(dest.string(), hex)) {
            return okfw::Result<Published>::fail(okfw::error("could not hash " + dest.string()));
        }

        Published out;
        out.url = "file://" + std::filesystem::absolute(dest).string();
        out.sha256_hex = hex;
        out.tag = domain::tag_from_file_name(artifact.file_name);

        okfw::info("published (local) " + artifact.file_name + " -> " + out.url);
        return okfw::Result<Published>::ok(std::move(out));
    }

private:
    StoreConfig config_;
};

}  // namespace

std::unique_ptr<Store> make_local_store(StoreConfig config) {
    return std::make_unique<LocalStore>(std::move(config));
}

}  // namespace vyzorix::publish
