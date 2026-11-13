#include "modules/containerbuild/image_pins.hpp"

#include "modules/containerbuild/builder_images.hpp"

#include <sstream>

namespace vyzorix::containerbuild {
namespace {

// parse_pins reads the embedded pins.txt: one `<ref> = <digest>` per line, with
// `#` comments and blank lines ignored.
std::vector<ImagePin> parse_pins(const std::string& text) {
    std::vector<ImagePin> pins;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        auto trim = [](std::string s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
            return s;
        };

        std::string ref = trim(line.substr(0, eq));
        std::string digest = trim(line.substr(eq + 1));
        if (!ref.empty() && !digest.empty()) pins.push_back({ref, digest});
    }
    return pins;
}

}  // namespace

const std::vector<ImagePin>& image_pins() {
    static const std::vector<ImagePin> pins = parse_pins(kImagePins);
    return pins;
}

std::string apply_image_pins(const std::string& dockerfile, const std::vector<ImagePin>& pins) {
    std::istringstream in(dockerfile);
    std::string out;
    std::string line;
    bool first = true;

    while (std::getline(in, line)) {
        if (!first) out += "\n";
        first = false;

        // A FROM line: `FROM <ref>` (optionally `FROM <ref> AS <stage>`).
        auto trimmed = line;
        while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());

        if (trimmed.rfind("FROM ", 0) == 0) {
            std::string rest = trimmed.substr(5);
            while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());

            // Split off an `AS <stage>` suffix.
            std::string ref = rest;
            std::string suffix;
            auto as_pos = rest.find(" AS ");
            if (as_pos == std::string::npos) as_pos = rest.find(" as ");
            if (as_pos != std::string::npos) {
                ref = rest.substr(0, as_pos);
                suffix = rest.substr(as_pos);
            }

            // Already digest-pinned: leave it alone.
            if (ref.find('@') == std::string::npos) {
                for (const auto& p : pins) {
                    if (p.reference == ref) {
                        ref += "@" + p.digest;
                        break;
                    }
                }
            }

            // Preserve the original leading whitespace and the `FROM` keyword case
            // is not tracked; docker accepts lowercase, and the bundled files use
            // uppercase, so re-emit uppercase.
            out += "FROM ";
            out += ref;
            out += suffix;
            continue;
        }

        out += line;
    }

    // A file that ended with a newline keeps it.
    if (!dockerfile.empty() && dockerfile.back() == '\n') out += "\n";
    return out;
}

}  // namespace vyzorix::containerbuild
