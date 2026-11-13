// domain — path policy: a recipe's paths must stay inside the project.
//
// A recipe is untrusted input (it is authored per project, and a build is
// arbitrary code). The builder reads the recipe's `path` (the build context)
// and `file` (the Containerfile) relative to `projectDir`; a value like
// `../../etc` or an absolute path would let a recipe read the host outside the
// project. This is a pure policy: no I/O, so it is unit-testable without a
// filesystem, and the engine applies it before it hands a path to docker.
#pragma once

#include <string>

#include "okfw/result.hpp"

namespace vyzorix::domain {

// ContainsPath reports whether `rel` is a safe project-relative path: not
// absolute, and free of a `..` component that would escape the project root.
// An empty path is the project root and is contained.
//
// This is a lexical check, not a realpath: the builder must not resolve
// symlinks or touch the host filesystem to make the decision, and a build that
// could follow a symlink out of the context is a separate concern (the docker
// build context is the project, and docker itself does not follow context
// symlinks outside it).
[[nodiscard]] bool contains_path(const std::string& rel);

// RequireProjectPath returns ok when `rel` is contained, else a refusal naming
// the field and the offending value — a refusal is how a recipe learns which
// field is wrong, rather than a build that reads the wrong tree.
[[nodiscard]] okfw::Result<void> require_project_path(const std::string& field,
                                                      const std::string& rel);

}  // namespace vyzorix::domain
