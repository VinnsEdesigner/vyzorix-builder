#include "okfw/toml.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace vyzorix::okfw {
namespace {

// strip_comment removes a trailing `#` comment, honouring quotes so a '#' inside
// a string survives.
std::string strip_comment(const std::string& line) {
    bool in_string = false;
    bool escape = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (in_string) {
            if (escape) { escape = false; }
            else if (c == '\\') { escape = true; }
            else if (c == '"') { in_string = false; }
        } else if (c == '"') {
            in_string = true;
        } else if (c == '#') {
            return line.substr(0, i);
        }
    }
    return line;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

struct ValueParser {
    const std::string& s;
    size_t i = 0;

    void skip() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++; }

    Result<TomlValue> value() {
        skip();
        if (i >= s.size()) return Result<TomlValue>::fail(error("toml: missing value"));
        char c = s[i];
        if (c == '"') return string_value();
        if (c == '[') return array_value();
        if (c == '{') return inline_table();
        if (s.compare(i, 4, "true") == 0) { i += 4; TomlValue v; v.type = TomlValue::Type::Boolean; v.boolean = true; return Result<TomlValue>::ok(v); }
        if (s.compare(i, 5, "false") == 0) { i += 5; TomlValue v; v.type = TomlValue::Type::Boolean; v.boolean = false; return Result<TomlValue>::ok(v); }
        return number_value();
    }

    Result<TomlValue> string_value() {
        i++;  // opening quote
        std::string out;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                i++;
                char e = s[i];
                switch (e) {
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    default: out.push_back(e); break;
                }
                i++;
            } else {
                out.push_back(s[i++]);
            }
        }
        if (i >= s.size()) return Result<TomlValue>::fail(error("toml: unterminated string"));
        i++;  // closing quote
        TomlValue v; v.type = TomlValue::Type::String; v.str = std::move(out);
        return Result<TomlValue>::ok(v);
    }

    Result<TomlValue> array_value() {
        i++;  // [
        TomlValue v; v.type = TomlValue::Type::Array;
        skip();
        if (i < s.size() && s[i] == ']') { i++; return Result<TomlValue>::ok(v); }
        while (true) {
            auto item = value();
            if (!item) return item;
            v.array.push_back(item.value());
            skip();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == ']') { i++; return Result<TomlValue>::ok(v); }
            return Result<TomlValue>::fail(error("toml: expected ',' or ']'"));
        }
    }

    Result<TomlValue> inline_table() {
        i++;  // {
        TomlValue v; v.type = TomlValue::Type::Table;
        skip();
        if (i < s.size() && s[i] == '}') { i++; return Result<TomlValue>::ok(v); }
        while (true) {
            skip();
            // A key is a quoted string or a bare key (letters, digits, _ -). The
            // recipe's inline tables (env, opts) use bare keys.
            std::string key;
            if (i < s.size() && s[i] == '"') {
                auto k = string_value();
                if (!k) return k;
                key = k.value().str;
            } else {
                size_t start = i;
                while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '-')) i++;
                if (i == start) return Result<TomlValue>::fail(error("toml: bad inline-table key"));
                key = s.substr(start, i - start);
            }
            skip();
            if (i >= s.size() || s[i] != '=') return Result<TomlValue>::fail(error("toml: expected '=' in inline table"));
            i++;
            auto val = value();
            if (!val) return val;
            v.table[key] = val.value();
            skip();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == '}') { i++; return Result<TomlValue>::ok(v); }
            return Result<TomlValue>::fail(error("toml: expected ',' or '}' in inline table"));
        }
    }

    Result<TomlValue> number_value() {
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) i++;
        if (i == start) {
            std::string bad = s.substr(start, 12);
            return Result<TomlValue>::fail(error("toml: unsupported value '" + bad + "'"));
        }
        std::string num = s.substr(start, i - start);
        TomlValue v; v.type = TomlValue::Type::Integer;
        v.integer = static_cast<long long>(std::strtod(num.c_str(), nullptr));
        return Result<TomlValue>::ok(v);
    }
};

}  // namespace

Result<TomlTable> toml_parse(const std::string& text) {
    // The document is a table, so the container is a Table value and the result
    // is its members. `current` points at the table a `key = value` lands in.
    TomlValue root;
    root.type = TomlValue::Type::Table;
    TomlValue* current = &root;

    std::istringstream in(text);
    std::string raw;
    int line_no = 0;

    while (std::getline(in, raw)) {
        line_no++;
        std::string line = trim(strip_comment(raw));
        if (line.empty()) continue;

        std::ostringstream where;
        where << " (line " << line_no << ")";

        if (line.rfind("[[", 0) == 0) {
            // Array of tables: [[name]]
            size_t close = line.find("]]");
            if (close == std::string::npos) return Result<TomlTable>::fail(error("toml: bad array-of-tables header" + where.str()));
            std::string name = trim(line.substr(2, close - 2));
            TomlValue& arr = root.table[name];
            arr.type = TomlValue::Type::Array;
            arr.array.emplace_back();
            arr.array.back().type = TomlValue::Type::Table;
            current = &arr.array.back();
            continue;
        }

        if (line[0] == '[') {
            size_t close = line.find(']');
            if (close == std::string::npos) return Result<TomlTable>::fail(error("toml: bad table header" + where.str()));
            std::string name = trim(line.substr(1, close - 1));
            // A dotted header ([a.b]) is unsupported: the recipe schema is flat.
            if (name.find('.') != std::string::npos) {
                return Result<TomlTable>::fail(error("toml: dotted table header unsupported: [" + name + "]" + where.str()));
            }
            TomlValue& tbl = root.table[name];
            tbl.type = TomlValue::Type::Table;
            current = &tbl;
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) return Result<TomlTable>::fail(error("toml: expected key = value" + where.str()));

        std::string key = trim(line.substr(0, eq));
        std::string rest = line.substr(eq + 1);
        ValueParser vp{rest};
        auto val = vp.value();
        if (!val) return Result<TomlTable>::fail(error(val.error().message + where.str()));

        current->table[key] = val.value();
    }

    return Result<TomlTable>::ok(std::move(root.table));
}

}  // namespace vyzorix::okfw
