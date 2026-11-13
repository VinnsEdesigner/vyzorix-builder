#include "support/test.hpp"

#include "domain/target.hpp"

using vyzorix::domain::resolve_target;
using vyzorix::domain::resolve_targets;

VYZ_TEST(target_maps_android_abi_to_debian_arch) {
    auto arm64 = resolve_target("arm64-v8a", "bookworm");
    CHECK(arm64.has_value());
    CHECK_EQ(arm64.value().debarch, "arm64");
    CHECK_EQ(arm64.value().os, "linux-android");
    CHECK_EQ(arm64.value().libc_floor, "2.36");  // bookworm

    auto armhf = resolve_target("armeabi-v7a", "bookworm");
    CHECK(armhf.has_value());
    CHECK_EQ(armhf.value().debarch, "armhf");

    auto x86 = resolve_target("x86_64", "");
    CHECK(x86.has_value());
    CHECK_EQ(x86.value().debarch, "amd64");
    CHECK_EQ(x86.value().suite, "bookworm");  // the default suite
}

VYZ_TEST(target_refuses_unknown_abi) {
    // Guessing an architecture is how a phone ends up running a foreign binary,
    // so an unknown ABI is a refusal naming it.
    auto bad = resolve_target("mips64", "bookworm");
    CHECK(!bad.has_value());
}

VYZ_TEST(target_resolves_all) {
    auto all = resolve_targets({"arm64-v8a", "x86_64"}, "bookworm");
    CHECK(all.has_value());
    CHECK_EQ(all.value().size(), static_cast<size_t>(2));
    CHECK_EQ(all.value()[0].debarch, "arm64");
    CHECK_EQ(all.value()[1].debarch, "amd64");
}

VYZ_TEST(target_suite_selects_the_glibc_floor) {
    // The recipe's `suite` picks the floor the engine asserts; the floor is not a
    // constant the suite silently ignores (contract R11).
    auto bullseye = resolve_target("arm64-v8a", "bullseye");
    CHECK(bullseye.has_value());
    if (bullseye.has_value()) CHECK_EQ(bullseye.value().libc_floor, "2.31");

    auto bookworm = resolve_target("arm64-v8a", "bookworm");
    CHECK(bookworm.has_value());
    if (bookworm.has_value()) CHECK_EQ(bookworm.value().libc_floor, "2.36");

    auto trixie = resolve_target("arm64-v8a", "trixie");
    CHECK(trixie.has_value());
    if (trixie.has_value()) CHECK_EQ(trixie.value().libc_floor, "2.41");

    // The empty suite is the default (bookworm).
    auto def = resolve_target("arm64-v8a", "");
    CHECK(def.has_value());
    if (def.has_value()) {
        CHECK_EQ(def.value().suite, "bookworm");
        CHECK_EQ(def.value().libc_floor, "2.36");
    }
}

VYZ_TEST(target_refuses_an_unknown_suite) {
    // An unknown suite cannot map to a floor; guessing one is how a binary that
    // needs a newer glibc than the device has gets published.
    auto bad = resolve_target("arm64-v8a", "sid");
    CHECK(!bad.has_value());
    if (!bad.has_value()) {
        CHECK(bad.error().message.find("sid") != std::string::npos);
    }
}
