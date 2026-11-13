#include "modules/publish/ghcr.hpp"

#include "domain/naming.hpp"
#include "modules/publish/verify.hpp"

#include "okfw/json.hpp"
#include "okfw/log.hpp"
#include "okfw/retry.hpp"
#include "okfw/sha256.hpp"
#include "okfw/subprocess.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

namespace vyzorix::publish {
namespace {

// CurlAuth is a 0600 curl config file carrying a credential. Passing a token on
// the command line would expose it to any user who can run `ps`; a config file
// read by curl keeps it out of the process arguments.
class CurlAuth {
public:
    explicit CurlAuth(const std::string& directive) {
        std::string tmpl = (std::filesystem::temp_directory_path() / "vyz-auth-XXXXXX").string();
        char* dir = ::mkdtemp(tmpl.data());
        if (dir == nullptr) return;  // path() stays empty; curl runs without the file

        dir_ = dir;
        path_ = (dir_ / "auth.conf").string();

        // Create the file 0600 first, then write, so the credential is never on
        // disk in a world-readable file even for an instant.
        int fd = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) { path_.clear(); return; }

        ssize_t n = ::write(fd, directive.data(), directive.size());
        if (n < 0 || static_cast<size_t>(n) != directive.size()) path_.clear();
        if (::write(fd, "\n", 1) < 0) path_.clear();
        ::close(fd);
    }

    ~CurlAuth() {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    CurlAuth(const CurlAuth&) = delete;
    CurlAuth& operator=(const CurlAuth&) = delete;
    CurlAuth(CurlAuth&&) = delete;
    CurlAuth& operator=(CurlAuth&&) = delete;

    [[nodiscard]] const std::string& path() const { return path_; }

private:
    std::filesystem::path dir_;
    std::string path_;
};

// run_curl runs curl with args and returns its output; a transport failure
// (curl's own non-zero exit) is a refusal. `config` is an optional credential
// file the caller has already written.
//
// A transport failure (curl exit != 0) is retried with backoff (contract R14):
// a connection reset is transient. An HTTP status is not retried here — the
// caller decides, because only it knows whether the status is on the request.
okfw::Result<okfw::ProcessOutput> run_curl(const std::vector<std::string>& args, const std::string& config = "") {
    std::string curl = okfw::which("curl");
    if (curl.empty()) return okfw::Result<okfw::ProcessOutput>::fail(okfw::error("curl is required to publish"));

    std::vector<std::string> argv{curl, "-sS", "-L"};
    if (!config.empty()) {
        argv.emplace_back("--config");
        argv.emplace_back(config);
    }
    for (const auto& a : args) argv.emplace_back(a);

    // Retry a transport failure (a non-zero curl exit), not an HTTP status.
    return okfw::retry<okfw::ProcessOutput>(
        3, std::chrono::milliseconds(500),
        [](const okfw::Error&) { return true; },  // the caller only sees a start failure or exit != 0
        [&]() -> okfw::Result<okfw::ProcessOutput> {
            auto r = okfw::run(argv);
            if (!r) return r;
            if (r.value().exit_code != 0) {
                return okfw::Result<okfw::ProcessOutput>::fail(
                    okfw::error("curl failed: " + okfw::error_first_line(r.value().err)));
            }
            return r;
        });
}

// registry_token exchanges the push credential for a bearer token scoped to this
// repo. GHCR requires this: an upload is authenticated even though the later
// anonymous read is not.
okfw::Result<std::string> registry_token(const std::string& registry, const std::string& ns,
                                         const std::string& repo, const std::string& push_token) {
    CurlAuth auth("user = \"x-access-token:" + push_token + "\"");
    std::string url = "https://" + registry + "/token?service=" + registry +
                      "&scope=repository:" + ns + "/" + repo + ":push,pull";

    auto r = run_curl({url}, auth.path());
    if (!r) return okfw::Result<std::string>::fail(r.error());
    if (r.value().exit_code != 0) {
        return okfw::Result<std::string>::fail(okfw::error("could not reach the registry token endpoint: " +
                                                           okfw::error_first_line(r.value().err)));
    }

    auto parsed = okfw::Json::parse(r.value().out);
    if (!parsed) return okfw::Result<std::string>::fail(parsed.error());
    const auto* t = parsed.value().find("token");
    if (t == nullptr) return okfw::Result<std::string>::fail(okfw::error("registry token response has no token"));
    return okfw::Result<std::string>::ok(t->as_string());
}

}  // namespace

std::string header_value(const std::string& headers, const std::string& name) {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::istringstream in(headers);
    std::string line;
    while (std::getline(in, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(0, colon);
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (key != lower_name) continue;

        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
        while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.pop_back();
        return value;
    }
    return "";
}

std::string ghcr_blob_url(const std::string& registry, const std::string& ns,
                          const std::string& repo, const std::string& sha256_hex) {
    return "https://" + registry + "/v2/" + ns + "/" + repo + "/blobs/sha256:" + sha256_hex;
}

namespace {

class GhcrStore final : public Store {
public:
    explicit GhcrStore(StoreConfig config) : config_(std::move(config)) {}

    okfw::Result<Published> put(const ArtifactIdentity& artifact) override {
        if (config_.visibility != "public") {
            return okfw::Result<Published>::fail(okfw::error(
                "publish.visibility must be \"public\": the device fetches anonymously and cannot use a private package"));
        }

        std::string push_token = env_token();
        if (push_token.empty()) {
            return okfw::Result<Published>::fail(okfw::error("no GHCR_TOKEN or GITHUB_TOKEN in the environment (needed to push)"));
        }

        // The digest of the exact bytes we are about to publish: the value the
        // manifest pins, and the name the registry stores the blob under.
        std::string hex;
        if (!okfw::sha256_file(artifact.local_path, hex)) {
            return okfw::Result<Published>::fail(okfw::error("could not hash " + artifact.local_path));
        }

        auto bearer = registry_token(config_.registry, config_.ns, config_.repo, push_token);
        if (!bearer) return okfw::Result<Published>::fail(bearer.error());

        std::string scope = config_.ns + "/" + config_.repo;

        // The authenticated phase runs under a bearer token. A token can expire
        // between the mint and the push (they are separate HTTP calls), so a 401
        // re-mints the token once and retries, rather than failing a build on a
        // token that was valid when the build started (contract R15).
        auto push_authenticated = [&](const std::string& token) -> okfw::Result<void> {
            CurlAuth auth("header = \"Authorization: Bearer " + token + "\"");

            // 1. Push the tarball as an OCI blob. Its digest is the sha256 the
            //    manifest pins, so the signature covers exactly the bytes GHCR
            //    serves.
            if (auto pushed = push_blob(scope, artifact.local_path, hex, auth.path()); !pushed) {
                return pushed;
            }

            // 2. Push a small OCI image manifest that references the blob as its
            //    single layer and carries the provenance labels. This is what
            //    makes the artifact browsable in GHCR: an image with a tag, not an
            //    orphan blob. A failure here is a warning, not a refusal — the
            //    device path (the blob) already works.
            auto tagged = push_image(scope, artifact, hex, token);
            if (!tagged) {
                okfw::log_error("the blob is published, but the OCI image tag was not created: " + tagged.error().message);
            }
            return okfw::Result<void>::ok();
        };

        auto pushed = push_authenticated(bearer.value());
        if (!pushed && pushed.error().message.find("HTTP 401") != std::string::npos) {
            okfw::info("the registry token expired; re-minting and retrying the push");
            auto refreshed = registry_token(config_.registry, config_.ns, config_.repo, push_token);
            if (!refreshed) return okfw::Result<Published>::fail(refreshed.error());
            pushed = push_authenticated(refreshed.value());
        }
        if (!pushed) return okfw::Result<Published>::fail(pushed.error());

        Published out;
        out.url = ghcr_blob_url(config_.registry, config_.ns, config_.repo, hex);
        out.sha256_hex = hex;
        out.tag = scope + ":" + domain::tag_from_file_name(artifact.file_name);

        // The device holds no credential, so a blob that does not answer
        // anonymously is a refusal, not a warning.
        if (auto verified = verify_anonymous(out.url, hex); !verified) {
            return okfw::Result<Published>::fail(verified.error());
        }

        okfw::info("published " + artifact.file_name + " -> " + out.url);
        return okfw::Result<Published>::ok(std::move(out));
    }

private:
    // push_blob starts an upload, PUTs the bytes under their digest, and commits.
    // The registry verifies the digest against the bytes, so a mismatch is caught
    // here rather than on the device.
    okfw::Result<void> push_blob(const std::string& scope, const std::string& file,
                                 const std::string& hex, const std::string& auth_path) const {
        // Idempotency (contract R16): a content-addressed store already holding
        // this digest is a no-op, not a re-upload. A HEAD on the blob answers 200
        // when it exists, so a re-run of a build is cheap and safe.
        std::string blob_url = "https://" + config_.registry + "/v2/" + scope + "/blobs/sha256:" + hex;
        auto head = run_curl({"-I", "-o", "/dev/null", "-w", "%{http_code}", blob_url}, auth_path);
        if (head && head.value().exit_code == 0) {
            std::string code = okfw::error_first_line(head.value().out);
            if (code == "200") {
                okfw::info("blob sha256:" + hex + " already present; skipping the upload");
                return okfw::Result<void>::ok();
            }
        }

        auto start = run_curl({"-X", "POST", "-D", "-", "-o", "/dev/null",
                               "https://" + config_.registry + "/v2/" + scope + "/blobs/uploads/"},
                              auth_path);
        if (!start) return okfw::Result<void>::fail(start.error());
        std::string location = header_value(start.value().out, "location");
        if (location.empty()) {
            return okfw::Result<void>::fail(okfw::error("the registry did not return an upload location: " +
                                                        okfw::error_last_line(start.value().err)));
        }
        if (location.rfind("http", 0) != 0) location = "https://" + config_.registry + location;

        std::string put_url = location + (location.find('?') == std::string::npos ? "?" : "&") + "digest=sha256:" + hex;
        auto uploaded = run_curl({"-X", "PUT", "--data-binary", "@" + file,
                                  "-H", "Content-Type: application/octet-stream",
                                  "-o", "/dev/null", "-w", "%{http_code}", put_url},
                                 auth_path);
        if (!uploaded) return okfw::Result<void>::fail(uploaded.error());
        std::string code = okfw::error_first_line(uploaded.value().out);
        if (code != "201" && code != "202") {
            return okfw::Result<void>::fail(okfw::error("blob upload failed (HTTP " + code + ") for " + scope));
        }
        return okfw::Result<void>::ok();
    }

    // push_image PUTs a manifest (application/vnd.oci.image.manifest.v1+json)
    // that references the blob as its single layer and records the artifact's
    // provenance as annotations, then tags it. The layer's media type is the
    // gzip layer type because the artifact is a gzipped tarball, which is what a
    // registry layer is.
    okfw::Result<void> push_image(const std::string& scope, const ArtifactIdentity& artifact,
                                  const std::string& hex, const std::string& bearer) const {
        const std::string media_type = "application/vnd.oci.image.manifest.v1+json";
        const std::string layer_type = "application/vnd.oci.image.layer.v1.tar+gzip";

        std::string config_json =
            "{\"architecture\":\"" + oci_arch(artifact.debarch) + "\","
            "\"os\":\"linux\","
            "\"rootfs\":{\"type\":\"layers\",\"diff_ids\":[\"sha256:" + hex + "\"]},"
            "\"config\":{\"Labels\":{" + labels(artifact) + "}}}";
        std::string config_digest = "sha256:" + okfw::sha256_hex(config_json);

        // Push the image config as a blob first; the manifest references it.
        if (auto c = push_bytes(scope, config_json, config_digest, "application/vnd.oci.image.config.v1+json", bearer); !c) {
            return okfw::Result<void>::fail(c.error());
        }

        std::string manifest =
            "{\"schemaVersion\":2,"
            "\"mediaType\":\"" + media_type + "\","
            "\"config\":{\"mediaType\":\"application/vnd.oci.image.config.v1+json\","
            "\"digest\":\"" + config_digest + "\",\"size\":" + std::to_string(config_json.size()) + "},"
            "\"layers\":[{\"mediaType\":\"" + layer_type + "\","
            "\"digest\":\"sha256:" + hex + "\",\"size\":" + std::to_string(file_size(artifact.local_path)) + "}],"
            "\"annotations\":{" + labels(artifact) + "}}";

        std::string tag = domain::tag_from_file_name(artifact.file_name);
        std::string put_url = "https://" + config_.registry + "/v2/" + scope + "/manifests/" + tag;
        CurlAuth auth("header = \"Authorization: Bearer " + bearer + "\"");
        auto r = run_curl({"-X", "PUT", "-H", "Content-Type: " + media_type,
                           "--data-binary", manifest, "-o", "/dev/null", "-w", "%{http_code}", put_url},
                          auth.path());
        if (!r) return okfw::Result<void>::fail(r.error());
        std::string code = okfw::error_first_line(r.value().out);
        if (code != "201" && code != "202") {
            return okfw::Result<void>::fail(okfw::error("manifest PUT failed (HTTP " + code + ") for " + scope + ":" + tag));
        }
        return okfw::Result<void>::ok();
    }

    // push_bytes uploads a small in-memory blob under a known digest.
    okfw::Result<void> push_bytes(const std::string& scope, const std::string& body,
                                  const std::string& digest, const std::string& content_type,
                                  const std::string& bearer) const {
        CurlAuth auth("header = \"Authorization: Bearer " + bearer + "\"");
        auto start = run_curl({"-X", "POST", "-D", "-", "-o", "/dev/null",
                               "https://" + config_.registry + "/v2/" + scope + "/blobs/uploads/"},
                              auth.path());
        if (!start) return okfw::Result<void>::fail(start.error());
        std::string location = header_value(start.value().out, "location");
        if (location.empty()) return okfw::Result<void>::fail(okfw::error("no upload location for the image config"));
        if (location.rfind("http", 0) != 0) location = "https://" + config_.registry + location;

        std::string put_url = location + (location.find('?') == std::string::npos ? "?" : "&") + "digest=" + digest;
        // The upload endpoint takes the bytes as octet-stream; the blob's own
        // media type belongs on the *manifest* that references it. GHCR 404s the
        // upload PUT when it carries an OCI media type, which is a real failure
        // seen against the live registry.
        (void)content_type;
        auto uploaded = run_curl({"-X", "PUT", "--data-binary", body, "-H", "Content-Type: application/octet-stream",
                                  "-o", "/dev/null", "-w", "%{http_code}", put_url},
                                 auth.path());
        if (!uploaded) return okfw::Result<void>::fail(uploaded.error());
        std::string code = okfw::error_first_line(uploaded.value().out);
        if (code != "201" && code != "202") {
            return okfw::Result<void>::fail(okfw::error("blob upload failed (HTTP " + code + ") for the image config"));
        }
        return okfw::Result<void>::ok();
    }

    // labels are the provenance annotations on the image and its config.
    static std::string labels(const ArtifactIdentity& a) {
        auto kv = [](const std::string& k, const std::string& v) {
            return "\"" + k + "\":\"" + json_escape(v) + "\"";
        };
        std::vector<std::string> parts = {
            kv("org.opencontainers.image.title", a.name),
            kv("org.opencontainers.image.description", "vyzorix artifact " + a.name + " (" + a.arch + ")"),
            kv("io.vyzorix.artifact.name", a.name),
            kv("io.vyzorix.artifact.arch", a.arch),
            kv("io.vyzorix.artifact.kind", a.kind),
        };
        if (!a.glibc_floor.empty()) parts.push_back(kv("io.vyzorix.artifact.glibc-floor", a.glibc_floor));

        std::string out;
        for (size_t i = 0; i < parts.size(); i++) {
            if (i) out += ",";
            out += parts[i];
        }
        return out;
    }

    static std::string oci_arch(const std::string& debarch) {
        if (debarch == "arm64") return "arm64";
        if (debarch == "armhf") return "arm";
        return "amd64";
    }

    static std::string json_escape(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') out.push_back('\\');
            out.push_back(c);
        }
        return out;
    }

    static long long file_size(const std::string& path) {
        std::error_code ec;
        auto n = std::filesystem::file_size(path, ec);
        return ec ? 0 : static_cast<long long>(n);
    }

    static std::string env_token() {
        if (const char* t = std::getenv("GHCR_TOKEN"); t != nullptr && *t != '\0') return t;
        if (const char* t = std::getenv("GITHUB_TOKEN"); t != nullptr && *t != '\0') return t;
        return "";
    }

    StoreConfig config_;
};

}  // namespace

std::unique_ptr<Store> make_ghcr_store(StoreConfig config) {
    return std::make_unique<GhcrStore>(std::move(config));
}

}  // namespace vyzorix::publish
