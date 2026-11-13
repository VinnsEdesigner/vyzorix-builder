// cmd/vyzorix-builder — the composition root.
//
// This is the only place concrete implementations meet interfaces: the catalog of
// adapters, the container builder, and the store backend are built here and handed to
// the run loop. Nothing below this layer knows which classes exist.
#pragma once

namespace vyzorix::cli {

// run is the whole program for `build`. It reads a BuildRequest from stdin,
// builds and publishes each artifact, and writes a BuildResult to stdout.
// Returns the process exit code: 0 ok, 1 refusal, 2 usage/local failure.
int run_build();

// main_entry dispatches the subcommands and returns the exit code.
int main_entry(int argc, char** argv);

}  // namespace vyzorix::cli
