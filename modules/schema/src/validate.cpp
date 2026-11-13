#include "modules/schema/validate.hpp"

#include "okfw/subprocess.hpp"

#include <regex>
#include <vector>

namespace vyzorix::schema {
namespace {

using okfw::Json;
using okfw::JsonType;

// The subset of JSON Schema the recipe schema uses. A keyword we do not handle
// is ignored, which is safe here because the schema is generated from build.cue
// and uses only these.
struct Context {
    const Json* root = nullptr;  // for $ref resolution against #/$defs/<name>
};

std::string json_type_name(JsonType t) {
    switch (t) {
        case JsonType::Null: return "null";
        case JsonType::Bool: return "boolean";
        case JsonType::Number: return "number";
        case JsonType::String: return "string";
        case JsonType::Array: return "array";
        case JsonType::Object: return "object";
    }
    return "unknown";
}

bool matches_type(const Json& v, const std::string& want) {
    if (want == "string") return v.type == JsonType::String;
    if (want == "integer" || want == "number") return v.type == JsonType::Number;
    if (want == "boolean") return v.type == JsonType::Bool;
    if (want == "array") return v.type == JsonType::Array;
    if (want == "object") return v.type == JsonType::Object;
    if (want == "null") return v.type == JsonType::Null;
    return true;  // an unknown type name is not enforced
}

// resolve follows a `#/$defs/<name>` reference to the schema's definition.
const Json* resolve(const Json& ref, const Context& ctx) {
    std::string r = ref.as_string();
    const std::string prefix = "#/$defs/";
    if (r.rfind(prefix, 0) != 0 || ctx.root == nullptr) return nullptr;
    return ctx.root->find("$defs") ? ctx.root->find("$defs")->find(r.substr(prefix.size())) : nullptr;
}

okfw::Result<void> check(const Json& value, const Json& schema, const std::string& path, const Context& ctx);

okfw::Result<void> fail(const std::string& path, const std::string& why) {
    return okfw::Result<void>::fail(okfw::error(path.empty() ? why : path + ": " + why));
}

okfw::Result<void> check_object(const Json& value, const Json& schema, const std::string& path, const Context& ctx) {
    const Json* props = schema.find("properties");

    // Required members.
    if (const Json* req = schema.find("required"); req != nullptr && req->type == JsonType::Array) {
        for (const auto& r : req->array) {
            if (value.find(r.as_string()) == nullptr) {
                return fail(path, "missing required field '" + r.as_string() + "'");
            }
        }
    }

    // additionalProperties: false refuses an unknown key by name — a typo must
    // not silently do nothing.
    const Json* extra = schema.find("additionalProperties");
    bool closed = extra != nullptr && extra->type == JsonType::Bool && !extra->boolean;

    for (const auto& [key, child] : value.object) {
        const Json* member = props != nullptr ? props->find(key) : nullptr;

        std::string child_path = path;
        if (!child_path.empty()) child_path += '.';
        child_path += key;

        if (member == nullptr) {
            if (closed) return fail(child_path, "unknown field");
            continue;
        }

        auto r = check(child, *member, child_path, ctx);
        if (!r) return r;
    }

    return okfw::Result<void>::ok();
}

okfw::Result<void> check_array(const Json& value, const Json& schema, const std::string& path, const Context& ctx) {
    if (const Json* items = schema.find("items"); items != nullptr) {
        for (size_t i = 0; i < value.array.size(); i++) {
            auto r = check(value.array[i], *items, path + "[" + std::to_string(i) + "]", ctx);
            if (!r) return r;
        }
    }
    return okfw::Result<void>::ok();
}

okfw::Result<void> check(const Json& value, const Json& schema, const std::string& path, const Context& ctx) {
    // $ref: resolve and recurse, ignoring sibling keywords (draft-07 semantics).
    if (const Json* ref = schema.find("$ref"); ref != nullptr) {
        const Json* target = resolve(*ref, ctx);
        if (target == nullptr) return fail(path, "unresolvable $ref " + ref->as_string());
        return check(value, *target, path, ctx);
    }

    if (const Json* type = schema.find("type"); type != nullptr) {
        if (!matches_type(value, type->as_string())) {
            return fail(path, "expected " + type->as_string() + ", got " + json_type_name(value.type));
        }
    }

    if (const Json* en = schema.find("enum"); en != nullptr && en->type == JsonType::Array) {
        bool found = false;
        for (const auto& e : en->array) {
            if (e.type == value.type && e.str == value.str) found = true;
        }
        if (!found) return fail(path, "not one of the allowed values");
    }

    if (value.type == JsonType::String) {
        if (const Json* pat = schema.find("pattern"); pat != nullptr) {
            // JSON Schema's `pattern` is a search, not a full match: the pattern
            // "^/" means "starts with /", not "is exactly /".
            if (!std::regex_search(value.str, std::regex(pat->as_string()))) {
                return fail(path, "does not match " + pat->as_string());
            }
        }
        if (const Json* min = schema.find("minLength"); min != nullptr) {
            if (value.str.size() < static_cast<size_t>(min->as_number())) return fail(path, "too short");
        }
        if (const Json* max = schema.find("maxLength"); max != nullptr) {
            if (value.str.size() > static_cast<size_t>(max->as_number())) return fail(path, "too long");
        }
    }

    if (value.type == JsonType::Number) {
        if (const Json* lo = schema.find("minimum"); lo != nullptr) {
            if (value.number < lo->as_number()) return fail(path, "below the minimum");
        }
        if (const Json* hi = schema.find("maximum"); hi != nullptr) {
            if (value.number > hi->as_number()) return fail(path, "above the maximum");
        }
    }

    if (value.type == JsonType::Object) return check_object(value, schema, path, ctx);
    if (value.type == JsonType::Array) return check_array(value, schema, path, ctx);

    return okfw::Result<void>::ok();
}

}  // namespace

okfw::Result<void> validate(const okfw::Json& document, const okfw::Json& schema) {
    Context ctx{&schema};
    return check(document, schema, "", ctx);
}

okfw::Result<okfw::Json> fetch(const std::string& url) {
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) {
        return okfw::Result<okfw::Json>::fail(okfw::error("schema URL must be http(s): " + url));
    }

    std::string curl = okfw::which("curl");
    if (curl.empty()) return okfw::Result<okfw::Json>::fail(okfw::error("curl is required to fetch the schema"));

    auto r = okfw::run({curl, "-sS", "-L", "-f", url});
    if (!r) return okfw::Result<okfw::Json>::fail(r.error());
    if (r.value().exit_code != 0) {
        return okfw::Result<okfw::Json>::fail(
            okfw::error("could not fetch the recipe schema from " + url + ": " +
                        okfw::error_first_line(r.value().err)));
    }

    auto parsed = okfw::Json::parse(r.value().out);
    if (!parsed) return okfw::Result<okfw::Json>::fail(okfw::error("the served schema is not JSON: " + parsed.error().message));
    return parsed;
}

}  // namespace vyzorix::schema
