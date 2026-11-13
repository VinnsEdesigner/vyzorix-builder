// okfw — a unique token for this process's build run.
//
// Two builds on one host must not share an image tag, a container name, or a
// scratch directory (contract R6): a CI machine can run several builds at once.
// A token derived from the process id, a monotonic clock, and a counter is
// unique per run within a host and needs no randomness or network.
#pragma once

#include <string>

namespace vyzorix::okfw {

// run_token returns a short token unique to this process and this call, safe in a
// filename, an image tag, and a container name (lowercase alphanumerics).
[[nodiscard]] std::string run_token();

}  // namespace vyzorix::okfw
