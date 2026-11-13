#include "domain/paths.hpp"

#include <vector>

namespace vyzorix::domain {
namespace {

// split splits on '/' only: the recipe is a portable path, and a backslash is a
// legal filename character on the target, not a separator.
std::vector<std::string> split(const std::string& path) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : path) {
        if (c == '/') {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    parts.push_back(cur);
    return parts;
}

}  // namespace

bool contains_path(const std::string& rel) {
    if (rel.empty()) return true;

    // An absolute path is never project-relative. A leading "~" is a shell
    // expansion the builder will not perform, so it is a refusal, not a home.
    if (rel.front() == '/') return false;
    if (rel.front() == '~') return false;

    // Depth is tracked by walking the components: a `..` that takes depth below
    // zero escapes the project. This is the lexical equivalent of joining and
    // checking the prefix, without touching the filesystem.
    int depth = 0;
    for (const auto& part : split(rel)) {
        if (part.empty() || part == ".") continue;
        if (part == "..") {
            if (--depth < 0) return false;
            continue;
        }
        ++depth;
    }
    return true;
}

okfw::Result<void> require_project_path(const std::string& field, const std::string& rel) {
    if (contains_path(rel)) return okfw::Result<void>::ok();
    return okfw::Result<void>::fail(okfw::error(
        field + " \"" + rel + "\" escapes the project directory; a path must be relative and must not climb out with \"..\""));
}

}  // namespace vyzorix::domain
