// schema — validating a decoded recipe against the served JSON Schema.
//
// The builder parses the recipe locally and validates it against the schema the
// server publishes, so a recipe the builder accepts is a recipe the server
// accepts. This is a small validator for the subset of JSON Schema the recipe
// schema uses (type, enum, pattern, length and numeric bounds, required,
// properties, additionalProperties:false, items, $ref) — deliberately not a
// general engine, which would be a dependency this project avoids.
#pragma once

#include <string>

#include "okfw/json.hpp"
#include "okfw/result.hpp"

namespace vyzorix::schema {

// validate checks a decoded document against a JSON Schema. A refusal names the
// field path (e.g. "artifact[0].lang"), so the operator fixes the file, not the
// builder.
[[nodiscard]] okfw::Result<void> validate(const okfw::Json& document, const okfw::Json& schema);

// fetch retrieves a JSON document over HTTPS, via curl. The builder is
// authoritative for what it accepts: it validates the recipe against the schema
// the server publishes, so a recipe it accepts is a recipe the server accepts.
[[nodiscard]] okfw::Result<okfw::Json> fetch(const std::string& url);

}  // namespace vyzorix::schema
