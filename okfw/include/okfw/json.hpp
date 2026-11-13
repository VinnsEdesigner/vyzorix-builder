// okfw — a small JSON value, parser and writer, for the CLI <-> builder contract.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "okfw/result.hpp"

namespace vyzorix::okfw {

enum class JsonType { Null, Bool, Number, String, Array, Object };

struct Json {
    JsonType type = JsonType::Null;
    bool boolean = false;
    double number = 0;
    std::string str;
    std::vector<Json> array;
    std::map<std::string, Json> object;

    static Json of_null() { return Json{}; }
    static Json of(bool b);
    static Json of(double n);
    static Json of(const std::string& s);
    static Json object_of(std::map<std::string, Json> fields);
    static Json array_of(std::vector<Json> items);

    // parse decodes a JSON document. A malformed document is a refusal naming
    // the byte offset, never a silent empty value.
    [[nodiscard]] static Result<Json> parse(const std::string& text);

    // dump serialises compactly. Key order is sorted (std::map), so the output
    // is deterministic for equal values.
    [[nodiscard]] std::string dump() const;

    // find returns the object member or nullptr.
    [[nodiscard]] const Json* find(const std::string& key) const;

    [[nodiscard]] std::string as_string(const std::string& fallback = "") const;
    [[nodiscard]] bool as_bool(bool fallback = false) const;
    [[nodiscard]] double as_number(double fallback = 0) const;
    [[nodiscard]] const std::vector<Json>* as_array() const;
};

// decode_string_array reads a JSON array of strings, tolerating absence.
[[nodiscard]] std::vector<std::string> decode_string_array(const Json* value);

}  // namespace vyzorix::okfw
