// okfw — running a subprocess and capturing its output (how docker is driven).
#pragma once

#include <string>
#include <vector>

#include "okfw/result.hpp"

namespace vyzorix::okfw {

struct ProcessOutput {
    int exit_code = 0;
    std::string out;
    std::string err;
};

// run executes argv[0] with args, optionally in working_dir (empty = inherit),
// optionally feeding stdin_data (empty = none), and captures stdout/stderr.
//
// It returns a ProcessOutput even on a non-zero exit: a build tool that fails is
// a normal outcome the caller classifies, not an exception. A failure to *start*
// the program (not found, not executable) is the only Result failure.
[[nodiscard]] Result<ProcessOutput> run(const std::vector<std::string>& argv,
                                        const std::string& working_dir = "",
                                        const std::string& stdin_data = "");

// run_with_timeout is run() with a wall-clock bound. When the bound elapses the
// child is terminated (SIGTERM, then SIGKILL) and the call returns a refusal.
// This is what keeps a wedged `docker build` from hanging the builder forever.
//
// timeout_seconds <= 0 means no bound (run()'s behaviour).
[[nodiscard]] Result<ProcessOutput> run_with_timeout(const std::vector<std::string>& argv,
                                                     int timeout_seconds,
                                                     const std::string& working_dir = "",
                                                     const std::string& stdin_data = "");

// run_quiet runs a command whose failure is not actionable (removing a
// container or image). The result is discarded deliberately.
void run_quiet(const std::vector<std::string>& argv);

// which resolves a program on PATH, or returns an empty string.
[[nodiscard]] std::string which(const std::string& program);

// error_first_line keeps a multi-line diagnostic to its first line, so a tool's
// stack of output does not swamp the builder's one-line refusal style.
[[nodiscard]] std::string error_first_line(const std::string& text);

// error_last_line keeps the last non-empty line, which is where docker's own
// tooling puts the real cause ("ERROR: failed to build: ..."); its preamble is
// the first line and says nothing useful.
[[nodiscard]] std::string error_last_line(const std::string& text);

}  // namespace vyzorix::okfw
