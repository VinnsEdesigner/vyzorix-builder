// okfw — cooperative cancellation on SIGINT/SIGTERM.
//
// A signal handler may only touch an async-signal-safe value, so it sets a flag;
// the long operations (a poll loop over a child process) check the flag and abort,
// which lets the normal unwinding run its cleanup (removing the container and
// image a build created). This is what makes the builder cancellable without
// leaving a container behind.
#pragma once

namespace vyzorix::okfw {

// install_signal_handlers makes SIGINT and SIGTERM set the interrupt flag rather
// than killing the process outright. It is called once, from the entry point.
void install_signal_handlers();

// interrupted reports whether a SIGINT/SIGTERM has been received.
[[nodiscard]] bool interrupted();

}  // namespace vyzorix::okfw
