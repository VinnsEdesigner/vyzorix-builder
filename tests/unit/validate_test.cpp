#include "support/test.hpp"

#include "modules/schema/validate.hpp"

using namespace vyzorix;

// The validator is the builder's authority for what a recipe may contain, so its
// refusals are pinned directly rather than only through a full build.

namespace {
okfw::Json recipe_schema() {
    // A schema with the same shape the generator produces, kept small.
    auto parse = okfw::Json::parse(R"({
      "$ref": "#/$defs/BuildRecipe",
      "$defs": {
        "BuildRecipe": {
          "type": "object", "additionalProperties": false, "required": ["build", "artifact"],
          "properties": {
            "build": {"$ref": "#/$defs/BuildSettings"},
            "artifact": {"type": "array", "items": {"$ref": "#/$defs/BuildArtifact"}}
          }
        },
        "BuildSettings": {
          "type": "object", "additionalProperties": false,
          "properties": {"version": {"type": "string", "maxLength": 4}, "abi": {"type": "array", "items": {"type": "string"}}}
        },
        "BuildArtifact": {
          "type": "object", "additionalProperties": false, "required": ["app", "name", "lang"],
          "properties": {
            "app": {"type": "string", "pattern": "^[a-z0-9][a-z0-9-]*$"},
            "name": {"type": "string"},
            "lang": {"type": "string", "enum": ["golang", "cpp"]},
            "port": {"type": "integer", "minimum": 0, "maximum": 65535}
          }
        }
      }
    })");
    return parse.value();
}
}  // namespace

VYZ_TEST(validate_accepts_a_good_recipe) {
    auto schema = recipe_schema();
    auto doc = okfw::Json::parse(R"({"build":{"version":"v1"},"artifact":[{"app":"vfd","name":"engine","lang":"cpp","port":8787}]})");
    CHECK(doc.has_value());
    CHECK(schema::validate(doc.value(), schema).has_value());
}

VYZ_TEST(validate_refuses_unknown_field_by_path) {
    auto schema = recipe_schema();
    auto doc = okfw::Json::parse(R"({"build":{},"artifact":[{"app":"vfd","name":"e","lang":"cpp","typo":1}]})");
    CHECK(doc.has_value());
    auto r = schema::validate(doc.value(), schema);
    CHECK(!r.has_value());
    CHECK(r.error().message.find("artifact[0].typo") != std::string::npos);
}

VYZ_TEST(validate_refuses_missing_required) {
    auto schema = recipe_schema();
    auto doc = okfw::Json::parse(R"({"build":{},"artifact":[{"app":"vfd","lang":"cpp"}]})");
    CHECK(doc.has_value());
    auto r = schema::validate(doc.value(), schema);
    CHECK(!r.has_value());
    CHECK(r.error().message.find("name") != std::string::npos);
}

VYZ_TEST(validate_refuses_bad_enum) {
    auto schema = recipe_schema();
    auto doc = okfw::Json::parse(R"({"build":{},"artifact":[{"app":"vfd","name":"e","lang":"cobol"}]})");
    CHECK(doc.has_value());
    CHECK(!schema::validate(doc.value(), schema).has_value());
}

VYZ_TEST(validate_pattern_is_a_search_not_a_full_match) {
    // JSON Schema's `pattern` searches; a "^-anchored" pattern like "^/" means
    // "starts with", so a long cmd value that starts with "/" must pass. A
    // full-match implementation would wrongly refuse it.
    auto schema = okfw::Json::parse(R"({
      "$ref": "#/$defs/A",
      "$defs": {"A": {"type":"object","additionalProperties":false,
        "properties": {"cmd": {"type":"string","pattern":"^/"}}}}
    })").value();

    auto ok = okfw::Json::parse(R"({"cmd":"/home/apps/x/run.sh"})");
    CHECK(ok.has_value());
    CHECK(schema::validate(ok.value(), schema).has_value());

    auto bad = okfw::Json::parse(R"({"cmd":"home/apps/x"})");
    CHECK(bad.has_value());
    CHECK(!schema::validate(bad.value(), schema).has_value());
}

VYZ_TEST(validate_refuses_pattern_mismatch) {
    auto schema = recipe_schema();
    auto doc = okfw::Json::parse(R"({"build":{},"artifact":[{"app":"Bad Name","name":"e","lang":"cpp"}]})");
    CHECK(doc.has_value());
    CHECK(!schema::validate(doc.value(), schema).has_value());
}

VYZ_TEST(validate_refuses_out_of_range_number) {
    auto schema = recipe_schema();
    auto doc = okfw::Json::parse(R"({"build":{},"artifact":[{"app":"vfd","name":"e","lang":"cpp","port":99999}]})");
    CHECK(doc.has_value());
    CHECK(!schema::validate(doc.value(), schema).has_value());
}

VYZ_TEST(validate_refuses_wrong_type) {
    auto schema = recipe_schema();
    auto doc = okfw::Json::parse(R"({"build":{},"artifact":"not an array"})");
    CHECK(doc.has_value());
    CHECK(!schema::validate(doc.value(), schema).has_value());
}
