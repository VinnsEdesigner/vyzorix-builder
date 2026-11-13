#include "support/test.hpp"

#include "modules/schema/contract.hpp"
#include "okfw/toml.hpp"

using namespace vyzorix;

// A recipe close to Vfd's real one, so the parse is exercised on the shape the
// tool is built for: a C engine with a closure and a static escape, a webroot,
// and the escape hatch.
static const char* kRecipe = R"(
[build]
version = "v1.2.0"
abi = ["arm64-v8a"]
suite = "bookworm"

[[artifact]]
app = "vfd"
name = "engine"
lang = "cpp"
path = "media-ingest-c"
build = "make"
cmd = "/home/apps/vfd/engine/run.sh"
kind = "dir"
port = 8787
expose = "loopback"
autostart = true
libs = ["libcurl", "libsqlite3"]
static = ["ffmpeg"]
glibc_floor = "2.36"
env = { MI_PORT = "8787", MI_HOST = "127.0.0.1" }

[[artifact]]
app = "vfd"
name = "console"
lang = "node-webroot"
path = "web"
build = "npm ci && npm run build"
dist = "dist"
kind = "webroot"
)";

VYZ_TEST(recipe_parses_artifacts_and_settings) {
    auto doc = okfw::toml_parse(kRecipe);
    CHECK(doc.has_value());
    if (!doc.has_value()) return;

    auto rec = schema::parse_recipe(doc.value());
    CHECK(rec.has_value());
    if (!rec.has_value()) return;
    const auto& r = rec.value();

    CHECK_EQ(r.version, "v1.2.0");
    CHECK_EQ(r.suite, "bookworm");
    CHECK_EQ(r.abis.size(), static_cast<size_t>(1));
    CHECK_EQ(r.abis[0], "arm64-v8a");
    CHECK_EQ(r.artifacts.size(), static_cast<size_t>(2));

    const auto& engine = r.artifacts[0];
    CHECK_EQ(engine.app, "vfd");
    CHECK_EQ(engine.name, "engine");
    CHECK_EQ(engine.lang, "cpp");
    CHECK_EQ(engine.cmd, "/home/apps/vfd/engine/run.sh");
    CHECK_EQ(engine.kind, "dir");
    CHECK_EQ(engine.port, 8787);
    CHECK_EQ(engine.expose, "loopback");
    CHECK(engine.autostart);
    CHECK_EQ(engine.libs.size(), static_cast<size_t>(2));
    CHECK_EQ(engine.libs[1], "libsqlite3");
    CHECK_EQ(engine.statics.size(), static_cast<size_t>(1));
    CHECK_EQ(engine.statics[0], "ffmpeg");
    CHECK_EQ(engine.glibc_floor, "2.36");
    CHECK_EQ(engine.env.at("MI_PORT"), "8787");

    const auto& console = r.artifacts[1];
    CHECK_EQ(console.name, "console");
    CHECK_EQ(console.lang, "node-webroot");
    CHECK_EQ(console.dist, "dist");
    CHECK_EQ(console.kind, "webroot");
}

VYZ_TEST(recipe_refuses_missing_name) {
    auto doc = okfw::toml_parse("[[artifact]]\napp = \"vfd\"\nlang = \"cpp\"\n");
    CHECK(doc.has_value());
    auto rec = schema::parse_recipe(doc.value());
    CHECK(!rec.has_value());
}

VYZ_TEST(recipe_refuses_a_duplicate_name) {
    const char* dup = R"(
[[artifact]]
app = "vfd"
name = "engine"
lang = "cpp"

[[artifact]]
app = "vfd"
name = "engine"
lang = "rust"
)";
    auto doc = okfw::toml_parse(dup);
    CHECK(doc.has_value());
    auto rec = schema::parse_recipe(doc.value());
    CHECK(!rec.has_value());
}

VYZ_TEST(recipe_refuses_a_bad_kebab_name) {
    auto doc = okfw::toml_parse("[[artifact]]\napp = \"vfd\"\nname = \"Bad Name\"\nlang = \"cpp\"\n");
    CHECK(doc.has_value());
    auto rec = schema::parse_recipe(doc.value());
    CHECK(!rec.has_value());
}

VYZ_TEST(contract_decodes_a_request_and_encodes_a_result) {
    const char* req = R"({
      "recipe": "[build]\nversion = \"v1\"\n",
      "projectDir": "/abs/project",
      "abis": ["arm64-v8a"],
      "publish": {"backend": "ghcr", "namespace": "owner", "repo": "vfd", "visibility": "public"},
      "dryRun": false
    })";
    auto decoded = schema::decode_request(req);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded.value().project_dir, "/abs/project");
    CHECK_EQ(decoded.value().abis.size(), static_cast<size_t>(1));
    CHECK_EQ(decoded.value().ns, "owner");
    CHECK_EQ(decoded.value().repo, "vfd");

    std::vector<schema::BuiltArtifact> arts;
    schema::BuiltArtifact a;
    a.name = "engine";
    a.arch = "arm64-v8a";
    a.url = "https://ghcr.io/v2/owner/vfd/blobs/sha256:abc";
    a.sha256 = "abc";
    a.file = "vfd-engine-linux-android-arm64-v8a-v1.2.0.tar.gz";
    a.size = 42;
    arts.push_back(a);

    std::string json = schema::encode_result(arts, "dev");
    // The result must round-trip through the same parser the CLI uses.
    auto back = okfw::Json::parse(json);
    CHECK(back.has_value());
    const auto* arr = back.value().find("artifacts");
    CHECK(arr != nullptr);
    CHECK_EQ(arr->as_array()->size(), static_cast<size_t>(1));
    CHECK_EQ((*arr->as_array())[0].find("sha256")->as_string(), "abc");
    // The builder records its own version in the result (contract R12), so the
    // artifact's provenance says which builder produced it.
    const auto* bv = back.value().find("builderVersion");
    CHECK(bv != nullptr);
    if (bv != nullptr) CHECK_EQ(bv->as_string(), std::string("dev"));
}

VYZ_TEST(contract_encodes_a_plan) {
    std::vector<schema::PlannedArtifact> planned;
    schema::PlannedArtifact p;
    p.app = "vfd";
    p.name = "engine";
    p.lang = "cpp";
    p.arch = "arm64-v8a";
    p.image = "ghcr.io/x/builder-cpp:bookworm";
    p.context = "media-ingest-c";
    p.libc_floor = "2.36";
    p.file_names = {"vfd-engine-linux-android-arm64-v8a-v1.0.0.tar.gz"};
    planned.push_back(p);

    auto json = schema::encode_plan(planned);
    auto back = okfw::Json::parse(json);
    CHECK(back.has_value());

    const auto* arr = back.value().find("plan");
    CHECK(arr != nullptr);
    CHECK_EQ(arr->as_array()->size(), static_cast<size_t>(1));

    const auto& e = (*arr->as_array())[0];
    CHECK_EQ(e.find("name")->as_string(), "engine");
    CHECK_EQ(e.find("image")->as_string(), "ghcr.io/x/builder-cpp:bookworm");
    CHECK_EQ(e.find("libcFloor")->as_string(), "2.36");
    CHECK_EQ(e.find("files")->as_array()->size(), static_cast<size_t>(1));
}

VYZ_TEST(contract_decodes_a_publish_request) {
    // A publish request carries artifacts and no recipe; the decoder must not
    // require a recipe for it.
    const char* req = R"({
      "artifacts": [
        {"name": "engine", "arch": "arm64-v8a", "kind": "dir", "file": "/tmp/x.tar.gz"}
      ],
      "publish": {"backend": "local", "directory": "out", "visibility": "public"}
    })";
    auto decoded = schema::decode_request(req);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded.value().artifacts.size(), static_cast<size_t>(1));
    CHECK_EQ(decoded.value().artifacts[0].name, "engine");
    CHECK_EQ(decoded.value().artifacts[0].file, "/tmp/x.tar.gz");
    CHECK_EQ(decoded.value().directory, "out");
    CHECK(decoded.value().recipe.empty());  // no recipe required for publish
}

VYZ_TEST(recipe_reads_the_build_command) {
    // The recipe's `build` must reach the artifact, or a project whose default
    // build needs an argument (a composer flag, a make target) cannot build.
    const char* r =
        "[build]\nversion = \"v1\"\nabi = [\"arm64-v8a\"]\n\n[[artifact]]\n"
        "app = \"guzzle\"\nname = \"app\"\nlang = \"php\"\npath = \".\"\n"
        "cmd = \"/home/apps/guzzle/run.sh\"\n"
        "build = \"COMPOSER_ROOT_VERSION=8.5.0 composer install\"\n";
    auto doc = okfw::toml_parse(r);
    CHECK(doc.has_value());
    if (!doc.has_value()) return;

    auto rec = schema::parse_recipe(doc.value());
    CHECK(rec.has_value());
    if (!rec.has_value()) return;

    CHECK_EQ(rec.value().artifacts[0].build, "COMPOSER_ROOT_VERSION=8.5.0 composer install");
}

VYZ_TEST(recipe_reads_the_build_limits) {
    // The limits object carries the build's resource caps and its timeout
    // (contract R2/R4). An absent object means no bound, which is the caller's
    // explicit choice.
    const char* req = R"({
      "recipe": "[build]\nversion = \"v1\"\nabi = [\"arm64-v8a\"]\n\n[[artifact]]\napp = \"a\"\nname = \"b\"\nlang = \"golang\"\n",
      "projectDir": "/tmp/x",
      "limits": {"timeoutSeconds": 1800, "memory": "4g", "cpus": "2", "pidsLimit": 512}
    })";

    auto decoded = schema::decode_request(req);
    CHECK(decoded.has_value());
    if (!decoded.has_value()) return;

    CHECK_EQ(decoded.value().timeout_seconds, 1800);
    CHECK_EQ(decoded.value().memory, std::string("4g"));
    CHECK_EQ(decoded.value().cpus, std::string("2"));
    CHECK_EQ(decoded.value().pids_limit, 512);
}

VYZ_TEST(recipe_defaults_limits_to_no_bound) {
    const char* req = R"({
      "recipe": "[build]\nversion = \"v1\"\nabi = [\"arm64-v8a\"]\n\n[[artifact]]\napp = \"a\"\nname = \"b\"\nlang = \"golang\"\n",
      "projectDir": "/tmp/x"
    })";

    auto decoded = schema::decode_request(req);
    CHECK(decoded.has_value());
    if (!decoded.has_value()) return;

    CHECK_EQ(decoded.value().timeout_seconds, 0);
    CHECK(decoded.value().memory.empty());
    CHECK_EQ(decoded.value().pids_limit, 0);
}

VYZ_TEST(recipe_reads_build_secret_ids) {
    // Build secrets are named by id; the value lives in the builder's environment
    // and never in the request (contract R18).
    const char* req = R"({
      "recipe": "[build]\nversion = \"v1\"\nabi = [\"arm64-v8a\"]\n\n[[artifact]]\napp = \"a\"\nname = \"b\"\nlang = \"golang\"\n",
      "projectDir": "/tmp/x",
      "limits": {"secretIds": ["GITHUB_TOKEN", "NPM_TOKEN"]}
    })";

    auto decoded = schema::decode_request(req);
    CHECK(decoded.has_value());
    if (!decoded.has_value()) return;

    CHECK_EQ(decoded.value().secret_ids.size(), static_cast<size_t>(2));
    CHECK_EQ(decoded.value().secret_ids[0], std::string("GITHUB_TOKEN"));
    CHECK_EQ(decoded.value().secret_ids[1], std::string("NPM_TOKEN"));
}
