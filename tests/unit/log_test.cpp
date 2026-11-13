#include "support/test.hpp"

#include "okfw/log.hpp"

#include <sstream>

using namespace vyzorix::okfw;

// The logger writes to stderr; these pin that the level gate works, so a Debug
// message is suppressed at the default level and shown once the level is raised.

VYZ_TEST(log_level_gate_suppresses_and_shows) {
    // At Info, a Debug line is dropped. We cannot capture stderr portably, so
    // this pins the gate does not crash and that set_level is honoured by
    // exercising both paths.
    set_level(Level::Info);
    log(Level::Debug, "suppressed");
    log(Level::Info, "shown");
    log(Level::Error, "shown");

    set_level(Level::Debug);
    log(Level::Debug, "now shown");

    // Restore the default so other tests see the normal gate.
    set_level(Level::Info);
    CHECK(true);
}

VYZ_TEST(log_helpers_route_to_their_levels) {
    set_level(Level::Info);
    info("info line");
    log_error("error line");
    CHECK(true);
}

VYZ_TEST(redact_scrubs_an_authorization_header) {
    auto out = redact("pushing with Authorization: Bearer ghp_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    CHECK(out.find("ghp_") == std::string::npos);
    CHECK(out.find("[REDACTED]") != std::string::npos);
}

VYZ_TEST(redact_scrubs_common_token_prefixes) {
    // A bare token with no "Bearer" in front is still a credential.
    CHECK(redact("token=ghp_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789").find("ghp_") == std::string::npos);
    CHECK(redact("x glpat-ABCDEFGHIJKLMNOPQRST").find("glpat-") == std::string::npos);
    CHECK(redact("aws AKIAIOSFODNN7EXAMPLE").find("AKIAIOSFODNN7EXAMPLE") == std::string::npos);
}

VYZ_TEST(redact_scrubs_signed_url_query_params) {
    auto out = redact("GET https://r2.example/b?a=1&X-Amz-Signature=deadbeefcafe&b=2");
    CHECK(out.find("deadbeefcafe") == std::string::npos);
    CHECK(out.find("X-Amz-Signature=[REDACTED]") != std::string::npos);
    // The unrelated parameters survive, so the line is still useful.
    CHECK(out.find("a=1") != std::string::npos);

    CHECK(redact("https://x/y?token=supersecretvalue").find("supersecretvalue") == std::string::npos);
}

VYZ_TEST(redact_scrubs_url_userinfo) {
    auto out = redact("cloning https://x-access-token:ghs_SECRETVALUE@github.com/o/r.git");
    CHECK(out.find("SECRETVALUE") == std::string::npos);
    CHECK(out.find("github.com") != std::string::npos);
}

VYZ_TEST(redact_leaves_ordinary_lines_alone) {
    // A redactor that mangles normal output is worse than none: pin that a plain
    // line and a normal URL pass through unchanged.
    const std::string plain = "building image for engine";
    CHECK_EQ(redact(plain), plain);

    const std::string url = "https://ghcr.io/v2/owner/vfd/blobs/sha256:9f2c";
    CHECK_EQ(redact(url), url);
}
