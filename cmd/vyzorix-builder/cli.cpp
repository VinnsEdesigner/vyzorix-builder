#include "cli.hpp"

#include "domain/naming.hpp"
#include "domain/target.hpp"
#include "modules/adapter/adapters.hpp"
#include "modules/containerbuild/container_builder.hpp"
#include "modules/plan/catalog.hpp"
#include "modules/publish/store.hpp"
#include "modules/schema/contract.hpp"
#include "modules/schema/validate.hpp"
#include "okfw/ids.hpp"
#include "okfw/json.hpp"
#include "okfw/log.hpp"
#include "okfw/sha256.hpp"
#include "okfw/subprocess.hpp"
#include "okfw/toml.hpp"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

namespace vyzorix::cli {
namespace {

using namespace vyzorix;

// Exit codes, mirroring the deploy CLI: 0 ok, 1 a refusal, 2 usage or a local
// failure.
constexpr int kOk = 0;
constexpr int kRefused = 1;
constexpr int kUsage = 2;

// kVersion is the released build's version. The release workflow defines
// VYZORIX_BUILDER_VERSION; a working-tree build reports "dev".
#ifdef VYZORIX_BUILDER_VERSION
constexpr const char* kVersion = VYZORIX_BUILDER_VERSION;
#else
constexpr const char* kVersion = "dev";
#endif

// read_stdin reads the whole of standard input, which carries the request JSON.
std::string read_stdin() {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    return ss.str();
}

// to_json_object converts a decoded TOML table into a JSON object, so the same
// document can be validated against the served schema. TOML and the schema's
// JSON shape agree on strings, numbers, booleans, arrays and tables.
std::map<std::string, okfw::Json> to_json_table(const okfw::TomlTable& t);
okfw::Json to_json_value(const okfw::TomlValue& v);

okfw::Json to_json_value(const okfw::TomlValue& v) {
    switch (v.type) {
        case okfw::TomlValue::Type::String: return okfw::Json::of(v.str);
        case okfw::TomlValue::Type::Integer: return okfw::Json::of(static_cast<double>(v.integer));
        case okfw::TomlValue::Type::Boolean: return okfw::Json::of(v.boolean);
        case okfw::TomlValue::Type::Array: {
            std::vector<okfw::Json> items;
            items.reserve(v.array.size());
            for (const auto& e : v.array) items.push_back(to_json_value(e));
            return okfw::Json::array_of(std::move(items));
        }
        case okfw::TomlValue::Type::Table: return okfw::Json::object_of(to_json_table(v.table));
    }
    return okfw::Json::of_null();
}

std::map<std::string, okfw::Json> to_json_table(const okfw::TomlTable& t) {
    std::map<std::string, okfw::Json> out;
    for (const auto& [k, val] : t) out[k] = to_json_value(val);
    return out;
}

// Request is the decoded BuildRequest plus the recipe it carries, resolved once
// and shared by every subcommand. Loading is where a refusal names the field, so
// it happens before any subcommand does work.
struct Request {
    schema::BuildRequest req;
    domain::Recipe recipe;
    std::vector<std::string> abis;
    std::string version;
};

// load_request reads stdin, parses the recipe, validates it against the served
// schema, and resolves the target ABIs and version. On failure it logs the
// reason and returns the exit code to use.
okfw::Result<Request> load_request() {
    auto request = schema::decode_request(read_stdin());
    if (!request) return okfw::Result<Request>::fail(request.error());

    if (request.value().recipe.empty()) return okfw::Result<Request>::fail(okfw::error("request has no recipe"));
    if (request.value().project_dir.empty()) return okfw::Result<Request>::fail(okfw::error("request has no projectDir"));

    auto doc = okfw::toml_parse(request.value().recipe);
    if (!doc) return okfw::Result<Request>::fail(doc.error());

    // Validate against the schema the server publishes, so a recipe the builder
    // accepts is a recipe the server accepts, and a typo is refused with a field
    // path rather than silently ignored.
    if (!request.value().schema_url.empty()) {
        auto served = schema::fetch(request.value().schema_url);
        if (!served) return okfw::Result<Request>::fail(served.error());

        okfw::Json doc_json = okfw::Json::object_of(to_json_table(doc.value()));
        auto valid = schema::validate(doc_json, served.value());
        if (!valid) {
            return okfw::Result<Request>::fail(
                okfw::error("vyzorix.build.toml does not match the server's schema: " + valid.error().message));
        }
    }

    auto recipe = schema::parse_recipe(doc.value());
    if (!recipe) return okfw::Result<Request>::fail(recipe.error());

    Request out;
    out.req = request.value();
    out.recipe = recipe.value();

    // The target ABIs: the request resolved them (from the device, or the
    // recipe's own `abi`).
    out.abis = out.req.abis.empty() ? out.recipe.abis : out.req.abis;
    if (out.abis.empty()) {
        return okfw::Result<Request>::fail(okfw::error("no abis: the request carried none and the recipe sets none"));
    }

    out.version = out.req.version.empty() ? out.recipe.version : out.req.version;
    return okfw::Result<Request>::ok(std::move(out));
}

// targets_for returns the ABIs one artifact builds for: its explicit `arch`, or
// every requested target.
std::vector<std::string> targets_for(const domain::RecipeArtifact& a, const std::vector<std::string>& abis) {
    return a.arch.empty() ? abis : std::vector<std::string>{a.arch};
}

// exit_code picks the code for a refusal: a schema mismatch is a refusal (1); a
// usage or local failure is 2.
int exit_code(const okfw::Error& e) {
    return e.message.find("does not match the server's schema") != std::string::npos ? kRefused : kUsage;
}

// plan_all resolves every artifact and target into a BuildPlan. It touches no
// Docker and no network, so `plan` is a pure preview and `build` shares it.
okfw::Result<std::vector<plan::BuildPlan>> plan_all(const Request& r) {
    auto catalog = plan::adapter::built_in_catalog();
    std::vector<plan::BuildPlan> plans;

    for (const auto& artifact : r.recipe.artifacts) {
        auto adapter = catalog.lookup(artifact.lang);
        if (!adapter) return okfw::Result<std::vector<plan::BuildPlan>>::fail(adapter.error());

        for (const auto& abi : targets_for(artifact, r.abis)) {
            auto target = domain::resolve_target(abi, r.recipe.suite);
            if (!target) return okfw::Result<std::vector<plan::BuildPlan>>::fail(target.error());

            auto p = adapter.value()->build_plan(artifact, target.value(), r.version);
            if (!p) return okfw::Result<std::vector<plan::BuildPlan>>::fail(p.error());
            plans.push_back(p.value());
        }
    }

    // Two artifacts that would write the same filename collide in the output
    // directory and in the registry, so the build is refused here, naming both,
    // rather than letting one silently overwrite the other.
    std::vector<std::string> file_names;
    for (const auto& p : plans) {
        for (const auto& o : p.outputs) file_names.push_back(o.file_name);
    }
    if (auto unique = domain::require_unique_file_names(file_names); !unique) {
        return okfw::Result<std::vector<plan::BuildPlan>>::fail(unique.error());
    }

    return okfw::Result<std::vector<plan::BuildPlan>>::ok(std::move(plans));
}

}  // namespace

// plan_of resolves the request into the planned artifacts. It touches no Docker
// and no network, so both `plan` (the JSON protocol) and a runtime command (the
// human surface) share it.
okfw::Result<std::vector<schema::PlannedArtifact>> plan_of(const Request& r) {
    auto plans = plan_all(r);
    if (!plans) return okfw::Result<std::vector<schema::PlannedArtifact>>::fail(plans.error());

    std::vector<schema::PlannedArtifact> planned;
    size_t i = 0;
    for (const auto& artifact : r.recipe.artifacts) {
        for (const auto& abi : targets_for(artifact, r.abis)) {
            const plan::BuildPlan& p = plans.value()[i++];

            schema::PlannedArtifact pa;
            pa.app = artifact.app;
            pa.name = artifact.name;
            pa.lang = artifact.lang;
            pa.arch = abi;
            pa.image = p.image;
            pa.context = p.context;
            pa.containerfile = p.containerfile;
            pa.libc_floor = p.libc_floor;
            for (const auto& o : p.outputs) pa.file_names.push_back(o.file_name);
            planned.push_back(std::move(pa));
        }
    }

    return okfw::Result<std::vector<schema::PlannedArtifact>>::ok(std::move(planned));
}

// build_artifacts builds and (unless dry-run) publishes every artifact of a
// request. The result is the list of built artifacts, which the caller prints
// either as JSON (the CLI protocol) or as a human summary (a runtime command).
okfw::Result<std::vector<schema::BuiltArtifact>> build_artifacts(const Request& r) {
    std::error_code dir_ec;
    if (!std::filesystem::is_directory(r.req.project_dir, dir_ec)) {
        return okfw::Result<std::vector<schema::BuiltArtifact>>::fail(
            okfw::error("projectDir is not a directory: " + r.req.project_dir));
    }

    auto plans = plan_all(r);
    if (!plans) return okfw::Result<std::vector<schema::BuiltArtifact>>::fail(plans.error());

    publish::StoreConfig store_cfg;
    store_cfg.backend = r.req.backend;
    store_cfg.ns = r.req.ns;
    store_cfg.repo = r.req.repo;
    store_cfg.visibility = r.req.visibility;
    store_cfg.directory = r.req.directory;
    store_cfg.dry_run = r.req.dry_run;
    if (!r.req.device_backend.empty()) store_cfg.device_backend = r.req.device_backend;
    if (!r.req.record_backend.empty()) store_cfg.record_backend = r.req.record_backend;

    std::unique_ptr<publish::Store> store;
    if (!r.req.dry_run) {
        auto made = publish::make_store(store_cfg);
        if (!made) return okfw::Result<std::vector<schema::BuiltArtifact>>::fail(made.error());
        store = std::move(made.value());
    }

    containerbuild::BuildLimits limits;
    // A generous default bound (contract R2): a real build may take minutes, but
    // no build should hang the builder forever. 0 in the request means "use the
    // default"; a negative value means "no bound" (an explicit operator choice).
    constexpr int kDefaultBuildTimeoutSeconds = 3600;
    limits.timeout_seconds = r.req.timeout_seconds == 0
                                 ? kDefaultBuildTimeoutSeconds
                                 : r.req.timeout_seconds;
    limits.memory = r.req.memory;
    limits.cpus = r.req.cpus;
    limits.pids_limit = r.req.pids_limit;
    limits.pull = r.req.pull;
    limits.max_artifact_bytes = r.req.max_artifact_bytes;
    limits.secret_ids = r.req.secret_ids;
    containerbuild::ContainerBuilder builder(r.req.project_dir, limits);

    // The build output lives outside the project: an output dir inside the
    // context would be copied into the next build and break determinism. It is
    // unique per run, so two builds on one host do not share a scratch dir
    // (contract R6). The guard removes it on every exit path, success or failure
    // (contract R5): a build must not leave disk behind.
    std::error_code tmp_ec;
    std::filesystem::path out_dir =
        std::filesystem::temp_directory_path(tmp_ec) / ("vyzorix-build-out-" + okfw::run_token());
    std::filesystem::remove_all(out_dir, tmp_ec);
    struct OutDirGuard {
        const std::filesystem::path& dir;
        ~OutDirGuard() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
        OutDirGuard(const OutDirGuard&) = delete;
        OutDirGuard& operator=(const OutDirGuard&) = delete;
        OutDirGuard(OutDirGuard&&) = delete;
        OutDirGuard& operator=(OutDirGuard&&) = delete;
    } out_guard{out_dir};

    std::vector<schema::BuiltArtifact> results;
    results.reserve(plans.value().size());

    size_t i = 0;
    for (const auto& artifact : r.recipe.artifacts) {
        for (const auto& abi : targets_for(artifact, r.abis)) {
            auto target = domain::resolve_target(abi, r.recipe.suite);
            if (!target) return okfw::Result<std::vector<schema::BuiltArtifact>>::fail(target.error());

            auto produced = builder.run(plans.value()[i], out_dir);
            if (!produced) return okfw::Result<std::vector<schema::BuiltArtifact>>::fail(produced.error());

            const plan::BuildPlan& current = plans.value()[i];
            i++;

            for (const auto& p : produced.value()) {
                schema::BuiltArtifact out;
                out.name = p.name;
                out.arch = p.arch;
                out.kind = p.kind;
                out.file = std::filesystem::path(p.local_path).filename().string();

                if (r.req.dry_run) {
                    // A preview still reports the real digest: the bytes exist
                    // locally, only the fetch URL is unknown until published.
                    std::string hex;
                    out.sha256 = okfw::sha256_file(p.local_path, hex) ? hex : "";
                    out.url = "dry-run";
                } else {
                    publish::ArtifactIdentity id;
                    id.file_name = out.file;
                    id.local_path = p.local_path;
                    id.arch = p.arch;
                    id.debarch = target.value().debarch;
                    id.name = p.name;
                    id.kind = p.kind;
                    id.glibc_floor = current.libc_floor;

                    auto published = store->put(id);
                    if (!published) return okfw::Result<std::vector<schema::BuiltArtifact>>::fail(published.error());
                    out.url = published.value().url;
                    out.sha256 = published.value().sha256_hex;
                    out.tag = published.value().tag;
                }

                std::error_code ec;
                out.size = static_cast<long long>(std::filesystem::file_size(p.local_path, ec));
                results.push_back(std::move(out));
            }
        }
    }

    // The per-run scratch dir is removed by out_guard on the way out.
    return okfw::Result<std::vector<schema::BuiltArtifact>>::ok(std::move(results));
}

// run_plan is the JSON-protocol `plan`: read a request, print the plan JSON.
int run_plan() {
    auto loaded = load_request();
    if (!loaded) {
        okfw::log_error("plan: " + loaded.error().message);
        return exit_code(loaded.error());
    }

    auto planned = plan_of(loaded.value());
    if (!planned) {
        okfw::log_error("plan: " + planned.error().message);
        return exit_code(planned.error());
    }

    std::cout << schema::encode_plan(planned.value()) << "\n";
    return kOk;
}

// run_publish publishes already-built artifact files named by the request.
int run_publish() {
    auto request = schema::decode_request(read_stdin());
    if (!request) {
        okfw::log_error("publish: " + request.error().message);
        return kUsage;
    }
    const schema::BuildRequest& req = request.value();

    if (req.artifacts.empty()) {
        okfw::log_error("publish: the request carries no artifacts to publish");
        return kUsage;
    }

    publish::StoreConfig store_cfg;
    store_cfg.backend = req.backend;
    store_cfg.ns = req.ns;
    store_cfg.repo = req.repo;
    store_cfg.visibility = req.visibility;
    store_cfg.directory = req.directory;

    auto store = publish::make_store(store_cfg);
    if (!store) {
        okfw::log_error("publish: " + store.error().message);
        return kUsage;
    }

    std::vector<schema::BuiltArtifact> results;
    results.reserve(req.artifacts.size());

    for (const auto& a : req.artifacts) {
        if (!std::filesystem::is_regular_file(a.file)) {
            okfw::log_error("publish: no such file: " + a.file);
            return kUsage;
        }

        // The key is the artifact filename, not the source path.
        std::string file_name = std::filesystem::path(a.file).filename().string();

        publish::ArtifactIdentity id;
        id.file_name = file_name;
        id.local_path = a.file;
        id.arch = a.arch;
        id.debarch = a.arch;  // publish has no target; the label uses the ABI as-is
        id.name = a.name;
        id.kind = a.kind;

        auto published = store.value()->put(id);
        if (!published) {
            okfw::log_error("publish: " + published.error().message);
            return kRefused;
        }

        schema::BuiltArtifact out;
        out.name = a.name;
        out.arch = a.arch;
        out.kind = a.kind;
        out.file = file_name;
        out.url = published.value().url;
        out.sha256 = published.value().sha256_hex;
        out.tag = published.value().tag;

        std::error_code ec;
        out.size = static_cast<long long>(std::filesystem::file_size(a.file, ec));
        results.push_back(std::move(out));
    }

    std::cout << schema::encode_result(results, kVersion) << "\n";
    return kOk;
}

// run_build is the JSON-protocol `build`: read a request, build, print JSON.
int run_build() {
    auto loaded = load_request();
    if (!loaded) {
        okfw::log_error("build: " + loaded.error().message);
        return exit_code(loaded.error());
    }

    auto results = build_artifacts(loaded.value());
    if (!results) {
        okfw::log_error("build: " + results.error().message);
        return kRefused;
    }

    std::cout << schema::encode_result(results.value(), kVersion) << "\n";
    return kOk;
}

// ---------------------------------------------------------------------------
// The runtime surface: one command per runtime, so `vyzorix-builder go`,
// `vyzorix-builder python`, ... are each a self-contained build. The flags are
// the recipe, so a user never writes a manifest to build locally.
// ---------------------------------------------------------------------------

namespace {

using namespace vyzorix;

// Options are the flags a runtime command takes.
struct Options {
    std::string project = ".";
    std::string app;       // default: the project directory's name
    std::string name = "app";
    std::string cmd;       // default: /home/apps/<app>/run.sh
    std::string arch = "arm64-v8a";
    std::string version = "v0.0.0";
    std::string out = "dist";
    std::string build;     // override the default build command
    std::string file;      // the Containerfile, for lang=containerfile
    std::string kind = "dir";
    std::string dist;
    std::string backend = "local";
    bool dry_run = false;
    bool plan_only = false;
}

;

std::string basename_of(const std::string& path) {
    std::string p = path;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    auto slash = p.find_last_of("/\\");
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

// parse_options reads `--key value` and `--key=value` flags. It is small and
// dependency-free, and it names the offending flag rather than guessing.
bool parse_options(const std::vector<std::string>& args, Options& o, std::string& err) {
    for (size_t i = 0; i < args.size(); i++) {
        const std::string& arg = args[i];
        if (arg.rfind("--", 0) != 0) {
            err = "unexpected argument '" + arg + "'";
            return false;
        }

        std::string key = arg, value;
        auto eq = arg.find('=');
        if (eq != std::string::npos) {
            key = arg.substr(0, eq);
            value = arg.substr(eq + 1);
        }

        auto take = [&](std::string& dst) -> bool {
            if (!value.empty()) { dst = value; return true; }
            if (i + 1 < args.size()) { dst = args[++i]; return true; }
            err = key + " needs a value";
            return false;
        };

        if (key == "--project") { if (!take(o.project)) return false; }
        else if (key == "--app") { if (!take(o.app)) return false; }
        else if (key == "--name") { if (!take(o.name)) return false; }
        else if (key == "--cmd") { if (!take(o.cmd)) return false; }
        else if (key == "--arch") { if (!take(o.arch)) return false; }
        else if (key == "--version") { if (!take(o.version)) return false; }
        else if (key == "--out") { if (!take(o.out)) return false; }
        else if (key == "--build") { if (!take(o.build)) return false; }
        else if (key == "--file") { if (!take(o.file)) return false; }
        else if (key == "--kind") { if (!take(o.kind)) return false; }
        else if (key == "--dist") { if (!take(o.dist)) return false; }
        else if (key == "--publish") { if (!take(o.backend)) return false; }
        else if (key == "--dry-run") { o.dry_run = true; }
        else if (key == "--plan") { o.plan_only = true; }
        else { err = "unknown flag '" + key + "'"; return false; }
    }

    return true;
}

// request_from_flags turns the flags into the same Request the JSON protocol
// uses, so one build path serves both surfaces.
Request request_from_flags(const std::string& lang, const Options& o) {
    Request r;
    r.req.project_dir = o.project;
    r.req.backend = o.backend;
    r.req.directory = o.out;
    r.req.dry_run = o.dry_run;
    r.version = o.version;
    r.abis = {o.arch};

    domain::RecipeArtifact a;
    a.app = o.app;
    a.name = o.name;
    a.lang = lang;
    a.path = ".";
    a.cmd = o.cmd;
    a.build = o.build;
    a.file = o.file;
    a.kind = o.kind;
    a.dist = o.dist;
    r.recipe.version = o.version;
    r.recipe.artifacts.push_back(std::move(a));

    return r;
}

// human_bytes formats a byte count for a terminal.
std::string human_bytes(long long n) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    double v = static_cast<double>(n);
    int u = 0;
    while (v >= 1024.0 && u < 3) { v /= 1024.0; u++; }
    char buf[32];
    std::snprintf(buf, sizeof(buf), (u == 0) ? "%.0f %s" : "%.1f %s", v, units[u]);
    return buf;
}

// print_plan_human prints what a runtime command would build.
void print_plan_human(const std::vector<schema::PlannedArtifact>& planned) {
    for (const auto& p : planned) {
        std::cout << "plan  " << p.lang << "  " << p.name << "  " << p.arch << "\n";
        std::cout << "  image   " << p.image << "\n";
        std::cout << "  context " << p.context << "\n";
        if (!p.libc_floor.empty()) std::cout << "  glibc   " << p.libc_floor << "\n";
        for (const auto& f : p.file_names) std::cout << "  -> " << f << "\n";
    }
}

// print_build_human prints the artifacts a runtime command built, plus the
// `[[app.artifact]]` blocks to paste into a manifest.
void print_build_human(const std::vector<schema::BuiltArtifact>& results, bool dry_run) {
    for (const auto& a : results) {
        std::cout << "built  " << a.name << "  " << a.arch;
        if (!a.file.empty()) std::cout << "  " << a.file;
        std::cout << "\n";
        if (a.size > 0) std::cout << "  size   " << human_bytes(a.size) << "\n";
        if (!a.sha256.empty()) std::cout << "  sha256 " << a.sha256 << "\n";
        if (!a.url.empty()) std::cout << "  url    " << a.url << "\n";
    }

    if (dry_run) {
        std::cout << "\n(dry run: nothing was published)\n";
        return;
    }

    std::cout << "\nPaste into vyzorix.toml:\n";
    for (const auto& a : results) {
        std::cout << "\n  [[app.artifact]]\n";
        std::cout << "  name   = \"" << a.name << "\"\n";
        if (!a.arch.empty() && a.arch != "all") std::cout << "  arch   = \"" << a.arch << "\"\n";
        std::cout << "  url    = \"" << a.url << "\"\n";
        std::cout << "  sha256 = \"" << a.sha256 << "\"\n";
        if (!a.kind.empty() && a.kind != "dir") std::cout << "  kind   = \"" << a.kind << "\"\n";
    }
}

// runtime_usage prints the flags a runtime command takes.
void runtime_usage(const std::string& lang) {
    std::cout << "Usage: vyzorix-builder " << lang << " [flags]\n\n"
              << "  --project DIR   the project to build (default: .)\n"
              << "  --app NAME      the app name in the manifest (default: the project dir)\n"
              << "  --name NAME     the artifact name (default: app)\n"
              << "  --cmd PATH      the entrypoint (default: /home/apps/<app>/run.sh)\n"
              << "  --arch ABI      arm64-v8a | armeabi-v7a | x86_64 (default: arm64-v8a)\n"
              << "  --version V     the artifact version (default: v0.0.0)\n"
              << "  --out DIR       where local artifacts land (default: dist)\n"
              << "  --build CMD     override the runtime's default build command\n"
              << "  --file PATH     the Containerfile (lang=containerfile only)\n"
              << "  --kind KIND     dir | webroot (default: dir)\n"
              << "  --dist DIR      the built output dir (lang=node-webroot)\n"
              << "  --publish B     local | ghcr | object (default: local)\n"
              << "  --dry-run       build, but publish nothing\n"
              << "  --plan          print the plan, run no Docker\n";
}

// run_runtime is one runtime's command: build (or plan) a project directly.
int run_runtime(const std::string& lang, const std::vector<std::string>& args) {
    if (!args.empty() && (args[0] == "-h" || args[0] == "--help")) {
        runtime_usage(lang);
        return kOk;
    }

    Options o;
    std::string err;
    if (!parse_options(args, o, err)) {
        okfw::log_error(lang + ": " + err);
        return kUsage;
    }

    if (o.app.empty()) o.app = basename_of(o.project);
    if (o.cmd.empty()) o.cmd = "/home/apps/" + o.app + "/run.sh";

    if (lang == "containerfile" && o.file.empty()) {
        okfw::log_error(lang + ": --file is required (the Containerfile to build)");
        return kUsage;
    }

    // The runtime surface is a local build: it reports what would be published.
    // The local backend cannot back a device fetch, so it is a preview, not a
    // publish — a real publish names ghcr (or object) explicitly.
    if (o.backend == "local") o.dry_run = true;

    Request r = request_from_flags(lang, o);

    if (o.plan_only) {
        auto planned = plan_of(r);
        if (!planned) {
            okfw::log_error(lang + ": " + planned.error().message);
            return exit_code(planned.error());
        }
        print_plan_human(planned.value());
        return kOk;
    }

    auto results = build_artifacts(r);
    if (!results) {
        okfw::log_error(lang + ": " + results.error().message);
        return kRefused;
    }

    print_build_human(results.value(), o.dry_run);
    return kOk;
}

}  // namespace

// usage prints the top-level help: the runtime commands and the JSON protocol.
void usage() {
    auto langs = plan::adapter::built_in_catalog().languages();

    std::cout << "vyzorix-builder - build a project into a deployable artifact\n\n"
              << "Build a project with one command per runtime:\n\n";
    for (const auto& l : langs) {
        std::cout << "  vyzorix-builder " << l << " --project . --app myapp\n";
    }
    std::cout << "\nRun 'vyzorix-builder <runtime> --help' for the flags.\n\n"
              << "Machine protocol (used by `vyzorix build`):\n"
              << "  vyzorix-builder build   < request.json   build and publish\n"
              << "  vyzorix-builder plan    < request.json   print the plan, no Docker\n"
              << "  vyzorix-builder publish < request.json   publish already-built files\n";
}

int main_entry(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);

    if (!args.empty() && (args[0] == "-v" || args[0] == "--verbose")) {
        okfw::set_level(okfw::Level::Debug);
        args.erase(args.begin());
    }

    if (args.empty()) {
        usage();
        return kUsage;
    }

    const std::string command = args[0];
    const std::vector<std::string> rest(args.begin() + 1, args.end());

    if (command == "build") return run_build();
    if (command == "plan") return run_plan();
    if (command == "publish") return run_publish();
    if (command == "-h" || command == "--help" || command == "help") {
        usage();
        return kOk;
    }
    if (command == "-V" || command == "--version" || command == "version") {
        std::cout << "vyzorix-builder " << kVersion << "\n";
        return kOk;
    }

    // A runtime command? Each language the catalog knows is one.
    for (const auto& lang : plan::adapter::built_in_catalog().languages()) {
        if (lang == command) return run_runtime(lang, rest);
    }

    okfw::log_error("unknown command or runtime '" + command + "'");
    usage();
    return kUsage;
}

}  // namespace vyzorix::cli
