#include "domain/naming.hpp"

#include <map>
#include <vector>

namespace vyzorix::domain {
namespace {

// kebab lowercases and replaces non-alphanumeric runs with single dashes, so a
// segment is always filename-safe even if a caller passed something odd.
std::string kebab(const std::string& in) {
    std::string out;
    bool last_dash = false;
    for (char c : in) {
        unsigned char u = static_cast<unsigned char>(c);
        if ((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || c == '-' || c == '_' || c == '.') {
            out.push_back(static_cast<char>(c == '_' ? '-' : c));
            last_dash = false;
        } else if (u >= 'A' && u <= 'Z') {
            out.push_back(static_cast<char>(u - 'A' + 'a'));
            last_dash = false;
        } else if (!last_dash && !out.empty()) {
            out.push_back('-');
            last_dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

}  // namespace

std::string arch_segment(const std::string& abi) {
    if (abi.empty() || abi == "all" || abi == "any") return "all";
    return kebab(abi);
}

std::string artifact_file_name(const std::string& app,
                               const std::string& artifact,
                               const std::string& os,
                               const std::string& abi,
                               const std::string& version) {
    std::string v = kebab(version);
    if (!v.empty() && v.front() == 'v' && v.size() > 1 && v[1] >= '0' && v[1] <= '9') {
        v = v.substr(1);
    }

    std::string name = kebab(app) + "-" + kebab(artifact) + "-" + kebab(os) + "-" + arch_segment(abi);
    if (!v.empty()) name += "-v" + v;
    return name + ".tar.gz";
}

std::string tag_from_file_name(const std::string& file_name) {
    static const std::string suffix = ".tar.gz";
    if (file_name.size() > suffix.size() &&
        file_name.compare(file_name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return file_name.substr(0, file_name.size() - suffix.size());
    }
    return file_name;
}

okfw::Result<void> require_unique_file_names(const std::vector<std::string>& file_names) {
    std::map<std::string, int> seen;  // filename -> the index that claimed it
    for (size_t i = 0; i < file_names.size(); i++) {
        const std::string& name = file_names[i];
        auto it = seen.find(name);
        if (it != seen.end()) {
            return okfw::Result<void>::fail(okfw::error(
                "two artifacts produce the same filename \"" + name + "\" (artifact " +
                std::to_string(it->second + 1) + " and artifact " + std::to_string(i + 1) +
                "); give them distinct names or apps"));
        }
        seen.emplace(name, static_cast<int>(i));
    }
    return okfw::Result<void>::ok();
}

}  // namespace vyzorix::domain
