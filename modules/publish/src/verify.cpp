#include "modules/publish/verify.hpp"

#include "modules/publish/ghcr.hpp"

#include "okfw/json.hpp"
#include "okfw/retry.hpp"
#include "okfw/sha256.hpp"
#include "okfw/subprocess.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unistd.h>

namespace vyzorix::publish {
namespace {

// temp_path makes a unique scratch path for the downloaded bytes.
std::string temp_path() {
    char tmpl[] = "/tmp/vyz-verify-XXXXXX";
    int fd = ::mkstemp(tmpl);
    if (fd >= 0) ::close(fd);
    return tmpl;
}

struct FetchResult {
    std::string status;   // the HTTP status curl reported
    std::string error;    // a transport failure's message
    bool ok = false;      // curl completed and reported a status
};

// fetch_once runs curl for the status. A transport failure is an error; an HTTP
// status (transient or not) is reported in `status` and is the caller's to judge.
// `auth` is an optional Authorization header value; `headers_out`, when set,
// receives the response headers (so a 401's WWW-Authenticate can be read).
FetchResult fetch_once(const std::string& curl, const std::string& url, const std::string& out_target,
                       const std::string& auth = "", std::string* headers_out = nullptr) {
    std::vector<std::string> argv{curl, "-sS", "-L", "-o", out_target, "-w", "%{http_code}",
                                  "-H", "Accept: application/vnd.oci.image.layer.v1.tar+gzip"};
    if (!auth.empty()) {
        argv.emplace_back("-H");
        argv.emplace_back("Authorization: " + auth);
    }
    std::string header_file;
    if (headers_out != nullptr) {
        header_file = temp_path();
        argv.emplace_back("-D");
        argv.emplace_back(header_file);
    }
    argv.push_back(url);

    auto r = okfw::run(argv);

    if (headers_out != nullptr) {
        std::ifstream in(header_file, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        *headers_out = ss.str();
        std::remove(header_file.c_str());
    }

    FetchResult f;
    if (!r) {
        f.error = r.error().message;
        return f;
    }
    if (r.value().exit_code != 0) {
        f.error = "could not reach " + url + ": " + okfw::error_first_line(r.value().err);
        return f;
    }
    f.status = okfw::error_first_line(r.value().out);
    f.ok = true;
    return f;
}

// token_for performs the registry auth handshake: it reads the WWW-Authenticate
// challenge, requests the token the challenge names, and returns
// "Bearer <token>". This is what any OCI client does, and what GHCR requires even
// for a public blob (contract R13): the device must do the same, or it cannot
// fetch a GHCR artifact at all.
std::string token_for(const std::string& curl, const std::string& www_authenticate) {
    auto extract = [&](const std::string& key) {
        auto pos = www_authenticate.find(key + "=\"");
        if (pos == std::string::npos) return std::string{};
        pos += key.size() + 2;
        auto end = www_authenticate.find('"', pos);
        return end == std::string::npos ? std::string{} : www_authenticate.substr(pos, end - pos);
    };

    std::string realm = extract("realm");
    std::string service = extract("service");
    std::string scope = extract("scope");
    if (realm.empty()) return "";

    std::string url = realm + "?service=" + service;
    if (!scope.empty()) url += "&scope=" + scope;

    auto r = okfw::run({curl, "-sS", "-L", url});
    if (!r || r.value().exit_code != 0) return "";

    auto parsed = okfw::Json::parse(r.value().out);
    if (!parsed) return "";
    const auto* t = parsed.value().find("token");
    if (t == nullptr) return "";
    return "Bearer " + t->as_string();
}

}  // namespace

okfw::Result<void> verify_anonymous(const std::string& url, const std::string& expected_sha256) {
    std::string curl = okfw::which("curl");
    if (curl.empty()) return okfw::Result<void>::fail(okfw::error("curl is required to publish"));

    // When a digest is expected, download the bytes so they can be hashed;
    // otherwise a status check needs no body.
    const bool check_bytes = !expected_sha256.empty();
    std::string out_target = check_bytes ? temp_path() : "/dev/null";

    auto cleanup = [&]() {
        if (check_bytes) std::remove(out_target.c_str());
    };

    // 1. An unauthenticated GET. A public artifact behind a registry that serves
    //    blobs directly answers 200 here; a registry that requires the OCI auth
    //    handshake (GHCR does, even for public blobs) answers 401 with a
    //    WWW-Authenticate challenge.
    std::string headers;
    auto f = fetch_once(curl, url, out_target, "", &headers);

    // 2. On a 401, do what every OCI client does: read the challenge, mint the
    //    token, and retry with it. This is the path a device must take too
    //    (contract R13), so the check mirrors the device rather than assuming a
    //    credentialless 200.
    if (f.ok && f.status == "401") {
        std::string challenge = header_value(headers, "www-authenticate");
        std::string auth = token_for(curl, challenge);
        if (!auth.empty()) {
            f = fetch_once(curl, url, out_target, auth);
        }
    }

    if (!f.ok) {
        cleanup();
        return okfw::Result<void>::fail(okfw::error(f.error));
    }
    if (f.status != "200" && f.status != "206") {
        cleanup();
        return okfw::Result<void>::fail(okfw::error(
            "the artifact at " + url + " did not answer with a fetchable body (HTTP " + f.status +
            "); the device fetches it with no credential of its own, so it must be public "
            "or behind an anonymous token"));
    }

    // The bytes must be the bytes the manifest pins. A 200 that serves different
    // content would fail the device's digest check, so it is refused here.
    if (check_bytes) {
        std::string actual;
        bool hashed = okfw::sha256_file(out_target, actual);
        std::remove(out_target.c_str());

        if (!hashed) {
            return okfw::Result<void>::fail(okfw::error("could not hash the bytes served by " + url));
        }
        if (actual != expected_sha256) {
            return okfw::Result<void>::fail(okfw::error(
                "the bytes at " + url + " hash to sha256:" + actual +
                ", but the manifest pins sha256:" + expected_sha256 +
                "; the device would fail its digest check"));
        }
    }

    return okfw::Result<void>::ok();
}

}  // namespace vyzorix::publish
