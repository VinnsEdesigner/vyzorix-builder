#include "okfw/json.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace vyzorix::okfw {

Json Json::of(bool b) { Json j; j.type = JsonType::Bool; j.boolean = b; return j; }
Json Json::of(double n) { Json j; j.type = JsonType::Number; j.number = n; return j; }
Json Json::of(const std::string& s) { Json j; j.type = JsonType::String; j.str = s; return j; }
Json Json::object_of(std::map<std::string, Json> fields) {
    Json j; j.type = JsonType::Object; j.object = std::move(fields); return j;
}
Json Json::array_of(std::vector<Json> items) {
    Json j; j.type = JsonType::Array; j.array = std::move(items); return j;
}

const Json* Json::find(const std::string& key) const {
    if (type != JsonType::Object) return nullptr;
    auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

std::string Json::as_string(const std::string& fallback) const {
    return type == JsonType::String ? str : fallback;
}
bool Json::as_bool(bool fallback) const {
    return type == JsonType::Bool ? boolean : fallback;
}
double Json::as_number(double fallback) const {
    return type == JsonType::Number ? number : fallback;
}
const std::vector<Json>* Json::as_array() const {
    return type == JsonType::Array ? &array : nullptr;
}

namespace {

void write_escaped(std::string& out, const std::string& s) {
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void write_value(std::string& out, const Json& j) {
    switch (j.type) {
        case JsonType::Null: out += "null"; break;
        case JsonType::Bool: out += j.boolean ? "true" : "false"; break;
        case JsonType::Number: {
            char buf[32];
            if (j.number == static_cast<double>(static_cast<long long>(j.number))) {
                std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(j.number));
            } else {
                std::snprintf(buf, sizeof(buf), "%g", j.number);
            }
            out += buf;
            break;
        }
        case JsonType::String: write_escaped(out, j.str); break;
        case JsonType::Array: {
            out.push_back('[');
            bool first = true;
            for (const auto& item : j.array) {
                if (!first) out.push_back(',');
                first = false;
                write_value(out, item);
            }
            out.push_back(']');
            break;
        }
        case JsonType::Object: {
            out.push_back('{');
            bool first = true;
            for (const auto& [key, value] : j.object) {
                if (!first) out.push_back(',');
                first = false;
                write_escaped(out, key);
                out.push_back(':');
                write_value(out, value);
            }
            out.push_back('}');
            break;
        }
    }
}

// A recursive-descent parser over the text. It is strict: trailing garbage, an
// unterminated string, or a bad escape is an error, so a malformed request fails
// loudly rather than decoding into a half-empty struct.
struct Parser {
    const std::string& s;
    size_t i = 0;

    void skip_ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    }

    bool fail(const std::string& msg, std::string& err) {
        std::ostringstream o;
        o << "json: " << msg << " at byte " << i;
        err = o.str();
        return false;
    }

    bool parse_value(Json& out, std::string& err) {
        skip_ws();
        if (i >= s.size()) return fail("unexpected end", err);
        char c = s[i];
        if (c == '{') return parse_object(out, err);
        if (c == '[') return parse_array(out, err);
        if (c == '"') return parse_string(out, err);
        if (c == 't' || c == 'f') return parse_bool(out, err);
        if (c == 'n') return parse_null(out, err);
        return parse_number(out, err);
    }

    bool parse_object(Json& out, std::string& err) {
        out.type = JsonType::Object;
        i++;  // {
        skip_ws();
        if (i < s.size() && s[i] == '}') { i++; return true; }
        while (true) {
            skip_ws();
            std::string key;
            if (i >= s.size() || s[i] != '"') return fail("expected object key", err);
            Json keyJson;
            if (!parse_string(keyJson, err)) return false;
            key = keyJson.str;
            skip_ws();
            if (i >= s.size() || s[i] != ':') return fail("expected ':'", err);
            i++;
            Json value;
            if (!parse_value(value, err)) return false;
            out.object[key] = std::move(value);
            skip_ws();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == '}') { i++; return true; }
            return fail("expected ',' or '}'", err);
        }
    }

    bool parse_array(Json& out, std::string& err) {
        out.type = JsonType::Array;
        i++;  // [
        skip_ws();
        if (i < s.size() && s[i] == ']') { i++; return true; }
        while (true) {
            Json value;
            if (!parse_value(value, err)) return false;
            out.array.push_back(std::move(value));
            skip_ws();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == ']') { i++; return true; }
            return fail("expected ',' or ']'", err);
        }
    }

    bool parse_string(Json& out, std::string& err) {
        out.type = JsonType::String;
        i++;  // opening quote
        std::string result;
        while (i < s.size() && s[i] != '"') {
            char c = s[i];
            if (c == '\\') {
                i++;
                if (i >= s.size()) return fail("bad escape", err);
                char e = s[i];
                switch (e) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'u': {
                        if (i + 4 >= s.size()) return fail("bad \\u escape", err);
                        unsigned code = 0;
                        for (int k = 0; k < 4; k++) {
                            char h = s[++i];
                            code <<= 4;
                            if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                            else return fail("bad hex", err);
                        }
                        // Encode as UTF-8. Surrogates are not paired here: the
                        // contract carries ASCII paths and digests.
                        if (code < 0x80) {
                            result.push_back(static_cast<char>(code));
                        } else if (code < 0x800) {
                            result.push_back(static_cast<char>(0xC0 | (code >> 6)));
                            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else {
                            result.push_back(static_cast<char>(0xE0 | (code >> 12)));
                            result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                        break;
                    }
                    default: return fail("bad escape", err);
                }
                i++;
            } else {
                result.push_back(c);
                i++;
            }
        }
        if (i >= s.size()) return fail("unterminated string", err);
        i++;  // closing quote
        out.str = std::move(result);
        return true;
    }

    bool parse_bool(Json& out, std::string& err) {
        if (s.compare(i, 4, "true") == 0) { out = Json::of(true); i += 4; return true; }
        if (s.compare(i, 5, "false") == 0) { out = Json::of(false); i += 5; return true; }
        return fail("bad literal", err);
    }

    bool parse_null(Json& out, std::string& err) {
        if (s.compare(i, 4, "null") == 0) { out = Json::of_null(); i += 4; return true; }
        return fail("bad literal", err);
    }

    bool parse_number(Json& out, std::string& err) {
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+')) i++;
        if (i == start) return fail("bad number", err);
        out = Json::of(std::strtod(s.substr(start, i - start).c_str(), nullptr));
        return true;
    }
};

}  // namespace

Result<Json> Json::parse(const std::string& text) {
    Parser p{text};
    Json out;
    std::string err;
    if (!p.parse_value(out, err)) return Result<Json>::fail(error(err));
    p.skip_ws();
    if (p.i != text.size()) return Result<Json>::fail(error("json: trailing data after value"));
    return Result<Json>::ok(std::move(out));
}

std::string Json::dump() const {
    std::string out;
    write_value(out, *this);
    return out;
}

std::vector<std::string> decode_string_array(const Json* value) {
    std::vector<std::string> out;
    if (value == nullptr) return out;
    const auto* arr = value->as_array();
    if (arr == nullptr) return out;
    for (const auto& item : *arr) {
        if (item.type == JsonType::String) out.push_back(item.str);
    }
    return out;
}

}  // namespace vyzorix::okfw
