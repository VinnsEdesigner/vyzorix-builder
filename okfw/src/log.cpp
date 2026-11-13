#include "okfw/log.hpp"

#include <cstdio>
#include <mutex>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace vyzorix::okfw {
namespace {

std::mutex& mutex() {
    static std::mutex m;
    return m;
}

Level& current() {
    static Level lvl = Level::Info;
    return lvl;
}

const char* name(Level level) {
    switch (level) {
        case Level::Debug: return "debug";
        case Level::Info:  return "info";
        case Level::Error: return "error";
    }
    return "info";
}

}  // namespace

void set_level(Level level) { current() = level; }

std::string redact(const std::string& text) {
    static const std::vector<std::pair<std::regex, std::string>> rules = {
        // An Authorization header, whole value.
        {std::regex(R"((Authorization:\s*)([^\r\n]+))", std::regex::icase), "$1[REDACTED]"},
        // A bearer/basic credential anywhere.
        {std::regex(R"(\b(Bearer|Basic|Token)\s+[A-Za-z0-9._~+/=-]+)", std::regex::icase), "$1 [REDACTED]"},
        // Common token prefixes (GitHub, GitLab, Slack, AWS keys).
        {std::regex(R"(\b(gh[pousr]_[A-Za-z0-9]{16,}|glpat-[A-Za-z0-9_-]{16,}|xox[baprs]-[A-Za-z0-9-]{10,}|AKIA[0-9A-Z]{16}))"),
         "[REDACTED]"},
        // A signed-URL / token query parameter: ?token=…, ?sig=…, ?X-Amz-Signature=…
        {std::regex(R"(([?&](?:token|sig|signature|access_token|X-Amz-Signature|X-Amz-Credential|key)=)[^&\s"']+)",
                    std::regex::icase),
         "$1[REDACTED]"},
        // A userinfo segment in a URL: scheme://user:pass@host.
        {std::regex(R"((://)[^/@\s:]+:[^/@\s]+@)"), "$1[REDACTED]@"},
    };

    std::string out = text;
    for (const auto& [re, replacement] : rules) {
        out = std::regex_replace(out, re, replacement);
    }
    return out;
}

void log(Level lvl, const std::string& message) {
    if (static_cast<int>(lvl) < static_cast<int>(current())) return;

    std::lock_guard<std::mutex> lock(mutex());
    std::fprintf(stderr, "vyzorix-builder: %s: %s\n", name(lvl), redact(message).c_str());
}

}  // namespace vyzorix::okfw
