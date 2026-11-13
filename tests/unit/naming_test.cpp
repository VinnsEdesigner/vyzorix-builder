#include "support/test.hpp"

#include "domain/naming.hpp"

using vyzorix::domain::arch_segment;
using vyzorix::domain::artifact_file_name;
using vyzorix::domain::require_unique_file_names;
using vyzorix::domain::tag_from_file_name;

VYZ_TEST(naming_is_explicit_and_unique) {
    // The exact filename pattern.
    CHECK_EQ(artifact_file_name("vfd", "engine", "linux-android", "arm64-v8a", "v1.2.0"),
             "vfd-engine-linux-android-arm64-v8a-v1.2.0.tar.gz");
    CHECK_EQ(artifact_file_name("vfd", "extractors", "linux-android", "arm64-v8a", "v1.2.0"),
             "vfd-extractors-linux-android-arm64-v8a-v1.2.0.tar.gz");
}

VYZ_TEST(naming_arch_free_is_all) {
    CHECK_EQ(artifact_file_name("vfd", "console", "linux-android", "all", "v1.2.0"),
             "vfd-console-linux-android-all-v1.2.0.tar.gz");
    CHECK_EQ(arch_segment(""), "all");
    CHECK_EQ(arch_segment("arm64-v8a"), "arm64-v8a");
}

VYZ_TEST(naming_normalises_a_leading_v) {
    // A version written either way produces the same, single-v filename.
    CHECK_EQ(artifact_file_name("vfd", "engine", "linux-android", "x86_64", "1.2.0"),
             artifact_file_name("vfd", "engine", "linux-android", "x86_64", "v1.2.0"));
}

VYZ_TEST(naming_tag_drops_the_archive_suffix) {
    CHECK_EQ(tag_from_file_name("vfd-engine-linux-android-arm64-v8a-v1.2.0.tar.gz"),
             "vfd-engine-linux-android-arm64-v8a-v1.2.0");
}

VYZ_TEST(naming_refuses_a_collision_naming_both) {
    // Distinct names do not collide.
    CHECK(require_unique_file_names({
        "vfd-engine-linux-android-arm64-v8a-v1.2.0.tar.gz",
        "vfd-console-linux-android-all-v1.2.0.tar.gz",
    }).has_value());

    // The same filename twice is a refusal that names the filename and both
    // artifact positions, so the author can see which two lines clash.
    auto dup = require_unique_file_names({
        "vfd-engine-linux-android-arm64-v8a-v1.2.0.tar.gz",
        "vfd-engine-linux-android-arm64-v8a-v1.2.0.tar.gz",
    });
    CHECK(!dup.has_value());
    CHECK(dup.error().message.find("vfd-engine-linux-android-arm64-v8a-v1.2.0.tar.gz") != std::string::npos);
    CHECK(dup.error().message.find("artifact 1") != std::string::npos);
    CHECK(dup.error().message.find("artifact 2") != std::string::npos);
}
