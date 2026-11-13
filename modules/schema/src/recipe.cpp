#include "modules/schema/contract.hpp"

#include <set>
#include <sstream>

namespace vyzorix::schema {
namespace {

// as_string_array reads a TomlValue that is an array of strings.
std::vector<std::string> as_string_array(const okfw::TomlValue* v) {
    return v == nullptr ? std::vector<std::string>{} : v->as_strings();
}

const okfw::TomlValue* field(const okfw::TomlValue& table, const std::string& key) {
    auto it = table.table.find(key);
    return it == table.table.end() ? nullptr : &it->second;
}

std::string str_field(const okfw::TomlValue& table, const std::string& key) {
    const auto* v = field(table, key);
    return v == nullptr ? "" : v->as_string();
}

bool bool_field(const okfw::TomlValue& table, const std::string& key) {
    const auto* v = field(table, key);
    return v != nullptr && v->as_bool();
}

long long int_field(const okfw::TomlValue& table, const std::string& key) {
    const auto* v = field(table, key);
    return v == nullptr ? 0 : v->as_int();
}

std::map<std::string, std::string> map_field(const okfw::TomlValue& table, const std::string& key) {
    std::map<std::string, std::string> out;
    const auto* v = field(table, key);
    if (v == nullptr || v->type != okfw::TomlValue::Type::Table) return out;
    for (const auto& [k, val] : v->table) out[k] = val.as_string();
    return out;
}

// is_kebab reports whether s matches ^[a-z0-9][a-z0-9-]*$ within a length bound.
bool is_kebab(const std::string& s, size_t max_len) {
    if (s.empty() || s.size() > max_len) return false;
    if (!((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= '0' && s[0] <= '9'))) return false;
    for (char c : s) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) return false;
    }
    return true;
}

}  // namespace

okfw::Result<domain::Recipe> parse_recipe(const okfw::TomlTable& doc) {
    domain::Recipe recipe;

    auto build_it = doc.find("build");
    if (build_it != doc.end()) {
        const auto& b = build_it->second;
        recipe.version = str_field(b, "version");
        recipe.suite = str_field(b, "suite");
        recipe.os = str_field(b, "os");
        recipe.abis = as_string_array(field(b, "abi"));
    }

    auto art_it = doc.find("artifact");
    if (art_it == doc.end() || art_it->second.type != okfw::TomlValue::Type::Array || art_it->second.array.empty()) {
        return okfw::Result<domain::Recipe>::fail(okfw::error("recipe has no [[artifact]] blocks"));
    }

    std::set<std::pair<std::string, std::string>> seen;  // (app, name), for uniqueness

    for (const auto& elem : art_it->second.array) {
        if (elem.type != okfw::TomlValue::Type::Table) {
            return okfw::Result<domain::Recipe>::fail(okfw::error("an [[artifact]] is not a table"));
        }
        const auto& t = elem;

        domain::RecipeArtifact a;
        a.app = str_field(t, "app");
        a.name = str_field(t, "name");
        a.lang = str_field(t, "lang");

        if (!is_kebab(a.app, 32)) {
            return okfw::Result<domain::Recipe>::fail(
                okfw::error("artifact.app '" + a.app + "' must match ^[a-z0-9][a-z0-9-]*$ and be at most 32 characters"));
        }
        if (!is_kebab(a.name, 64)) {
            return okfw::Result<domain::Recipe>::fail(
                okfw::error("artifact.name '" + a.name + "' must match ^[a-z0-9][a-z0-9-]*$ and be at most 64 characters"));
        }
        if (!seen.insert({a.app, a.name}).second) {
            return okfw::Result<domain::Recipe>::fail(
                okfw::error("duplicate artifact '" + a.name + "' for app '" + a.app + "'"));
        }

        a.cmd = str_field(t, "cmd");
        a.path = str_field(t, "path");
        a.build = str_field(t, "build");
        a.dist = str_field(t, "dist");
        a.file = str_field(t, "file");
        a.out = str_field(t, "out");
        a.arch = str_field(t, "arch");
        a.kind = str_field(t, "kind");
        a.glibc_floor = str_field(t, "glibc_floor");
        a.expose = str_field(t, "expose");
        a.port = static_cast<int>(int_field(t, "port"));
        a.libs = as_string_array(field(t, "libs"));
        a.statics = as_string_array(field(t, "static"));
        a.env = map_field(t, "env");
        a.opts = map_field(t, "opts");
        a.autostart = bool_field(t, "autostart");
        a.purge = bool_field(t, "purge");
        a.pwa = bool_field(t, "pwa");

        recipe.artifacts.push_back(std::move(a));
    }

    return okfw::Result<domain::Recipe>::ok(std::move(recipe));
}

okfw::Result<BuildRequest> decode_request(const std::string& json) {
    auto parsed = okfw::Json::parse(json);
    if (!parsed) return okfw::Result<BuildRequest>::fail(parsed.error());
    const okfw::Json& j = parsed.value();

    BuildRequest r;
    r.recipe = j.find("recipe") ? j.find("recipe")->as_string() : "";
    r.schema_url = j.find("schemaUrl") ? j.find("schemaUrl")->as_string() : "";
    r.project_dir = j.find("projectDir") ? j.find("projectDir")->as_string() : "";
    r.version = j.find("version") ? j.find("version")->as_string() : "";
    r.device_id = j.find("deviceId") ? j.find("deviceId")->as_string() : "";
    r.abis = okfw::decode_string_array(j.find("abis"));
    if (const auto* p = j.find("publish")) {
        r.backend = p->find("backend") ? p->find("backend")->as_string() : "";
        r.ns = p->find("namespace") ? p->find("namespace")->as_string() : "";
        r.repo = p->find("repo") ? p->find("repo")->as_string() : "";
        r.visibility = p->find("visibility") ? p->find("visibility")->as_string() : "";
        r.directory = p->find("directory") ? p->find("directory")->as_string() : "";
        r.device_backend = p->find("deviceBackend") ? p->find("deviceBackend")->as_string() : "";
        r.record_backend = p->find("recordBackend") ? p->find("recordBackend")->as_string() : "";
    }
    r.dry_run = j.find("dryRun") ? j.find("dryRun")->as_bool() : false;

    // Build limits: read from a `limits` object, so the shape can grow.
    if (const auto* lim = j.find("limits")) {
        r.timeout_seconds = lim->find("timeoutSeconds")
                                ? static_cast<int>(lim->find("timeoutSeconds")->as_number())
                                : 0;
        r.memory = lim->find("memory") ? lim->find("memory")->as_string() : "";
        r.cpus = lim->find("cpus") ? lim->find("cpus")->as_string() : "";
        r.pids_limit = lim->find("pidsLimit") ? static_cast<int>(lim->find("pidsLimit")->as_number()) : 0;
        r.pull = lim->find("pull") ? lim->find("pull")->as_bool() : false;
        r.max_artifact_bytes = lim->find("maxArtifactBytes")
                                   ? static_cast<long long>(lim->find("maxArtifactBytes")->as_number())
                                   : 0;
        r.secret_ids = okfw::decode_string_array(lim->find("secretIds"));
    }

    if (const auto* arts = j.find("artifacts"); arts != nullptr) {
        if (const auto* arr = arts->as_array(); arr != nullptr) {
            for (const auto& e : *arr) {
                PublishArtifact a;
                a.name = e.find("name") ? e.find("name")->as_string() : "";
                a.arch = e.find("arch") ? e.find("arch")->as_string() : "";
                a.kind = e.find("kind") ? e.find("kind")->as_string() : "";
                a.file = e.find("file") ? e.find("file")->as_string() : "";
                r.artifacts.push_back(std::move(a));
            }
        }
    }

    return okfw::Result<BuildRequest>::ok(std::move(r));
}

std::string encode_result(const std::vector<BuiltArtifact>& artifacts,
                          const std::string& builder_version) {
    std::vector<okfw::Json> items;
    items.reserve(artifacts.size());
    for (const auto& a : artifacts) {
        std::map<std::string, okfw::Json> m;
        m["name"] = okfw::Json::of(a.name);
        if (!a.arch.empty()) m["arch"] = okfw::Json::of(a.arch);
        if (!a.kind.empty()) m["kind"] = okfw::Json::of(a.kind);
        m["url"] = okfw::Json::of(a.url);
        m["sha256"] = okfw::Json::of(a.sha256);
        if (!a.file.empty()) m["file"] = okfw::Json::of(a.file);
        if (!a.tag.empty()) m["tag"] = okfw::Json::of(a.tag);
        if (a.size > 0) m["size"] = okfw::Json::of(static_cast<double>(a.size));
        items.push_back(okfw::Json::object_of(std::move(m)));
    }

    std::map<std::string, okfw::Json> root;
    root["artifacts"] = okfw::Json::array_of(std::move(items));
    if (!builder_version.empty()) root["builderVersion"] = okfw::Json::of(builder_version);
    return okfw::Json::object_of(std::move(root)).dump();
}

std::string encode_plan(const std::vector<PlannedArtifact>& planned) {
    std::vector<okfw::Json> items;
    items.reserve(planned.size());
    for (const auto& p : planned) {
        std::map<std::string, okfw::Json> m;
        m["app"] = okfw::Json::of(p.app);
        m["name"] = okfw::Json::of(p.name);
        m["lang"] = okfw::Json::of(p.lang);
        m["arch"] = okfw::Json::of(p.arch);
        m["image"] = okfw::Json::of(p.image);
        m["context"] = okfw::Json::of(p.context);
        if (!p.containerfile.empty()) m["containerfile"] = okfw::Json::of(p.containerfile);
        if (!p.libc_floor.empty()) m["libcFloor"] = okfw::Json::of(p.libc_floor);

        std::vector<okfw::Json> files;
        files.reserve(p.file_names.size());
        for (const auto& f : p.file_names) files.push_back(okfw::Json::of(f));
        m["files"] = okfw::Json::array_of(std::move(files));

        items.push_back(okfw::Json::object_of(std::move(m)));
    }

    std::map<std::string, okfw::Json> root;
    root["plan"] = okfw::Json::array_of(std::move(items));
    return okfw::Json::object_of(std::move(root)).dump();
}

}  // namespace vyzorix::schema
