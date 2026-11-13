// publish — the anonymous-fetch check, shared by every backend.
#pragma once

#include <string>

#include "okfw/result.hpp"

namespace vyzorix::publish {

// verify_anonymous checks that url answers without a credential AND that the
// bytes it serves hash to `expected_sha256`. The device fetches with no
// credential and verifies the digest, so a URL that requires authentication, or
// one that serves different bytes than the manifest pins, is a refusal here
// rather than a `digest_mismatch` on the device (contract R13).
//
// `expected_sha256` is the lowercase hex digest the manifest will pin. An empty
// value skips the content check (only the status is verified).
[[nodiscard]] okfw::Result<void> verify_anonymous(const std::string& url,
                                                  const std::string& expected_sha256 = "");

}  // namespace vyzorix::publish
