// okfw — retrying a transient failure, with backoff.
//
// A registry push, a schema fetch, or an anonymous-fetch check can fail for a
// reason that is not the request's fault: a connection reset, a 5xx, a 429. Those
// are worth another attempt; a 401/403/404 is not — retrying a definitive refusal
// only wastes time and hides the real error (contract R14).
#pragma once

#include <chrono>
#include <functional>
#include <thread>

#include "okfw/result.hpp"

namespace vyzorix::okfw {

// is_transient_status reports whether an HTTP status is worth retrying: a server
// error (5xx) or a rate limit (429). A 4xx that is not 429 is definitive.
[[nodiscard]] inline bool is_transient_status(int http_code) {
    return http_code == 429 || (http_code >= 500 && http_code <= 599);
}

// retry runs `attempt`, and on a failure that `is_transient(error)` reports,
// waits with exponential backoff and tries again, up to `attempts` times total.
// The final failure is returned unchanged, so the caller sees the real reason.
//
// `sleep` is injected so a test can run the loop without real delays.
template <typename T>
Result<T> retry(int attempts,
                std::chrono::milliseconds base_delay,
                const std::function<bool(const Error&)>& is_transient,
                const std::function<Result<T>()>& attempt,
                const std::function<void(std::chrono::milliseconds)>& sleep =
                    [](std::chrono::milliseconds d) { std::this_thread::sleep_for(d); }) {
    if (attempts < 1) attempts = 1;

    Result<T> result = attempt();
    for (int i = 1; i < attempts && !result; i++) {
        if (!is_transient(result.error())) return result;

        // 1x, 2x, 4x, ... of the base delay, capped so a long run stays bounded.
        auto delay = base_delay * (1 << (i - 1));
        if (delay > std::chrono::milliseconds(30000)) delay = std::chrono::milliseconds(30000);
        sleep(delay);

        result = attempt();
    }
    return result;
}

}  // namespace vyzorix::okfw
