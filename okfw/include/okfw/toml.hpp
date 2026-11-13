// okfw — a small TOML reader for the build recipe's narrow subset.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "okfw/result.hpp"

namespace vyzorix::okfw {

struct TomlValue {
    enum class Type { String, Integer, Boolean, Array, Table };
    Type type = Type::String;
    std::string str;
    long long integer = 0;
    bool boolean = false;
    std::vector<TomlValue> array;
    std::map<std::string, TomlValue> table;

    [[nodiscard]] std::string as_string(const std::string& fallback = "") const {
        return type == Type::String ? str : fallback;
    }
    [[nodiscard]] long long as_int(long long fallback = 0) const {
        return type == Type::Integer ? integer : fallback;
    }
    [[nodiscard]] bool as_bool(bool fallback = false) const {
        return type == Type::Boolean ? boolean : fallback;
    }
    [[nodiscard]] std::vector<std::string> as_strings() const {
        std::vector<std::string> out;
        if (type != Type::Array) return out;
        for (const auto& v : array) out.push_back(v.as_string());
        return out;
    }
};

// TomlTable is a decoded document: top-level keys to values. An array of tables
// (`[[artifact]]`) is an Array whose elements are Table values.
using TomlTable = std::map<std::string, TomlValue>;

// toml_parse decodes a document. A malformed or unsupported document is a
// refusal naming the line.
[[nodiscard]] Result<TomlTable> toml_parse(const std::string& text);

}  // namespace vyzorix::okfw
