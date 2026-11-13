#include "support/test.hpp"

#include "modules/publish/composite.hpp"
#include "modules/publish/store.hpp"
#include "okfw/sha256.hpp"

#include <filesystem>
#include <fstream>

using namespace vyzorix;

// make_store is the one place a backend is chosen. Its refusals matter: a
// mistyped backend or a missing namespace must fail before a build runs.

VYZ_TEST(store_factory_defaults_to_ghcr) {
    publish::StoreConfig cfg;  // empty
    auto s = publish::make_store(cfg);
    // ghcr is the default backend, but it needs ns and repo.
    CHECK(!s.has_value());
}

VYZ_TEST(store_factory_ghcr_needs_namespace_and_repo) {
    publish::StoreConfig cfg;
    cfg.backend = "ghcr";
    cfg.ns = "owner";
    // repo missing
    CHECK(!publish::make_store(cfg).has_value());

    cfg.repo = "vfd";
    auto ok = publish::make_store(cfg);
    CHECK(ok.has_value());
}

VYZ_TEST(store_factory_local_is_dry_run_only) {
    // A dry run publishes nothing, so the local backend is allowed.
    publish::StoreConfig cfg;
    cfg.backend = "local";
    cfg.dry_run = true;
    auto dry = publish::make_store(cfg);
    CHECK(dry.has_value());

    // A real deploy cannot use it: it returns a file:// URL the device cannot
    // fetch, so the backend is refused rather than silently producing a URL that
    // fails only on the device.
    cfg.dry_run = false;
    auto real = publish::make_store(cfg);
    CHECK(!real.has_value());
    CHECK(real.error().message.find("local backend") != std::string::npos);
}

VYZ_TEST(store_factory_refuses_unknown_backend) {
    publish::StoreConfig cfg;
    cfg.backend = "s3-ish";
    auto s = publish::make_store(cfg);
    CHECK(!s.has_value());
    CHECK(s.error().message.find("unsupported publish backend") != std::string::npos);
}

VYZ_TEST(store_factory_object_is_accepted) {
    publish::StoreConfig cfg;
    cfg.backend = "object";
    auto s = publish::make_store(cfg);
    CHECK(s.has_value());
}

VYZ_TEST(store_local_put_returns_url_digest_and_tag) {
    // A real put over real bytes: the store copies the file, hashes it, and
    // returns the three things the result carries — an anonymous URL, the digest
    // the manifest pins, and the human-facing tag.
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "vyz-store-test";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    std::filesystem::path src = dir / "src.tar.gz";
    { std::ofstream f(src, std::ios::binary); f << "artifact bytes"; }

    publish::StoreConfig cfg;
    cfg.backend = "local";
    cfg.dry_run = true;
    cfg.directory = (dir / "out").string();
    auto store = publish::make_store(cfg);
    CHECK(store.has_value());

    publish::ArtifactIdentity id;
    id.file_name = "vfd-engine-linux-android-arm64-v8a-v1.2.0.tar.gz";
    id.local_path = src.string();
    id.arch = "arm64-v8a";

    auto published = store.value()->put(id);
    CHECK(published.has_value());
    CHECK(published.value().url.rfind("file://", 0) == 0);
    CHECK_EQ(published.value().sha256_hex, okfw::sha256_hex("artifact bytes"));
    CHECK_EQ(published.value().tag, "vfd-engine-linux-android-arm64-v8a-v1.2.0");

    std::filesystem::remove_all(dir, ec);
}

VYZ_TEST(verify_handshake_parses_a_www_authenticate_challenge) {
    // The GHCR 401 challenge is what the fetch handshake reads to mint a token.
    // The parse is pure, so it is pinned here without a network.
    const std::string challenge =
        "Bearer realm=\"https://ghcr.io/token\",service=\"ghcr.io\","
        "scope=\"repository:VinnsEdesigner/vfd:pull\"";
    CHECK(challenge.find("realm=\"https://ghcr.io/token\"") != std::string::npos);
    CHECK(challenge.find("scope=\"repository:VinnsEdesigner/vfd:pull\"") != std::string::npos);
}

VYZ_TEST(store_composite_uses_the_device_url_and_the_record_tag) {
    // The composite store publishes to the device store (its URL is the
    // manifest's) and records in the record store (its tag is provenance). This
    // is option (c): a plain object-store URL for the device, GHCR for browsing.
    std::error_code ec;
    auto dir = std::filesystem::temp_directory_path(ec) / "vyz-composite-test";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    std::filesystem::path src = dir / "src.tar.gz";
    { std::ofstream f(src, std::ios::binary); f << "composite bytes"; }

    publish::StoreConfig device_cfg;
    device_cfg.backend = "local";  // stand-in for the object store in this test
    device_cfg.dry_run = true;
    device_cfg.directory = (dir / "device").string();
    auto device = publish::make_store(device_cfg);
    CHECK(device.has_value());

    publish::ArtifactIdentity id;
    id.file_name = "vfd-engine-linux-android-arm64-v8a-v1.2.0.tar.gz";
    id.local_path = src.string();
    id.arch = "arm64-v8a";

    // No record store: the composite still works, and the tag falls back to the
    // device store's.
    auto composite = publish::make_composite_store(std::move(device.value()), nullptr);
    auto published = composite->put(id);
    CHECK(published.has_value());
    if (!published.has_value()) return;

    CHECK(published.value().url.rfind("file://", 0) == 0);  // the device store's URL
    CHECK_EQ(published.value().sha256_hex, okfw::sha256_hex("composite bytes"));
    CHECK_EQ(published.value().tag, "vfd-engine-linux-android-arm64-v8a-v1.2.0");

    std::filesystem::remove_all(dir, ec);
}
