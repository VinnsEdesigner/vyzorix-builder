#include "support/test.hpp"

#include "domain/paths.hpp"

using vyzorix::domain::contains_path;
using vyzorix::domain::require_project_path;

VYZ_TEST(paths_accept_project_relative) {
    // The shapes a recipe legitimately uses: the root, a subdirectory, a
    // dotted subdirectory, and a path that climbs and comes back.
    CHECK(contains_path(""));
    CHECK(contains_path("."));
    CHECK(contains_path("media-ingest-c"));
    CHECK(contains_path("apps/api"));
    CHECK(contains_path("./web/dist"));
    CHECK(contains_path("a/b/../c"));
    CHECK(contains_path("deploy/Containerfile.arm64"));
}

VYZ_TEST(paths_refuse_an_escape) {
    // Absolute paths, a home expansion, and any climb past the root.
    CHECK(!contains_path("/etc/passwd"));
    CHECK(!contains_path("/"));
    CHECK(!contains_path("~/.ssh"));
    CHECK(!contains_path(".."));
    CHECK(!contains_path("../secrets"));
    CHECK(!contains_path("a/../../b"));
    CHECK(!contains_path("../../etc/shadow"));
}

VYZ_TEST(paths_refusal_names_the_field) {
    auto ok = require_project_path("path", "media-ingest-c");
    CHECK(ok.has_value());

    auto bad = require_project_path("file", "../../etc/shadow");
    CHECK(!bad.has_value());
    // The message must name the field and the value, so a recipe author sees
    // which line is wrong rather than a bare refusal.
    CHECK(bad.error().message.find("file") != std::string::npos);
    CHECK(bad.error().message.find("../../etc/shadow") != std::string::npos);
}
