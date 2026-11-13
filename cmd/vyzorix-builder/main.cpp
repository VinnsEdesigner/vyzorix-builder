#include "cli.hpp"

#include "okfw/interrupt.hpp"

int main(int argc, char** argv) {
    // Make SIGINT/SIGTERM a cooperative cancellation, so a build that is stopped
    // unwinds and removes the container and image it created (contract R3).
    vyzorix::okfw::install_signal_handlers();
    return vyzorix::cli::main_entry(argc, argv);
}
