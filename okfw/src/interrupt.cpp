#include "okfw/interrupt.hpp"

#include <csignal>

namespace vyzorix::okfw {
namespace {

// volatile sig_atomic_t is the only type a handler may portably write.
volatile std::sig_atomic_t g_interrupted = 0;

extern "C" void on_signal(int) {
    g_interrupted = 1;
}

}  // namespace

void install_signal_handlers() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
}

bool interrupted() { return g_interrupted != 0; }

}  // namespace vyzorix::okfw
