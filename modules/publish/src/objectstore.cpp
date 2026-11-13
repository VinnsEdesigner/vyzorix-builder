#include "modules/publish/objectstore.hpp"

#include "domain/naming.hpp"
#include "modules/publish/verify.hpp"
#include "okfw/log.hpp"
#include "okfw/sha256.hpp"
#include "okfw/subprocess.hpp"

#include <cstdlib>
#include <sstream>

namespace vyzorix::publish {
namespace {

class ObjectStore final : public Store {
public:
    explicit ObjectStore(const StoreConfig& config) { (void)config; }

    okfw::Result<Published> put(const ArtifactIdentity& artifact) override {
        const char* base_env = std::getenv("VYZORIX_BUILD_BASE_URL");
        std::string base = base_env ? base_env : "";
        if (base.empty()) {
            return okfw::Result<Published>::fail(okfw::error("the object backend needs VYZORIX_BUILD_BASE_URL (the public prefix)"));
        }
        while (!base.empty() && base.back() == '/') base.pop_back();

        std::string hex;
        if (!okfw::sha256_file(artifact.local_path, hex)) {
            return okfw::Result<Published>::fail(okfw::error("could not hash " + artifact.local_path));
        }

        // The uploader is external and configured, because the backend may be
        // S3, R2, or anything with a CLI. The builder's job is the contract, not
        // the credentials.
        const char* cmd_env = std::getenv("VYZORIX_BUILD_UPLOAD_CMD");
        std::string cmd = cmd_env ? cmd_env : "";
        if (cmd.empty()) {
            return okfw::Result<Published>::fail(okfw::error(
                "the object backend needs VYZORIX_BUILD_UPLOAD_CMD (e.g. \"aws s3 cp\"); the destination URL is appended"));
        }

        // Compose the destination and run `<cmd> <file> <dest>`.
        std::string url = base + "/" + artifact.file_name;

        // Split the configured command on spaces; it is a command line, not an
        // argv, because that is what an environment variable can carry.
        std::vector<std::string> argv;
        std::string token;
        std::istringstream in(cmd);
        while (in >> token) argv.push_back(token);
        argv.push_back(artifact.local_path);
        argv.push_back(url);

        auto r = okfw::run(argv);
        if (!r) return okfw::Result<Published>::fail(r.error());
        if (r.value().exit_code != 0) {
            return okfw::Result<Published>::fail(
                okfw::error("upload failed: " + okfw::error_first_line(r.value().err)));
        }

        Published out;
        out.url = url;
        out.sha256_hex = hex;
        out.tag = domain::tag_from_file_name(artifact.file_name);

        if (auto verified = verify_anonymous(out.url, hex); !verified) {
            return okfw::Result<Published>::fail(verified.error());
        }

        okfw::info("published " + artifact.file_name + " -> " + out.url);
        return okfw::Result<Published>::ok(std::move(out));
    }

};

}  // namespace

std::unique_ptr<Store> make_object_store(const StoreConfig& config) {
    return std::make_unique<ObjectStore>(config);
}

}  // namespace vyzorix::publish
