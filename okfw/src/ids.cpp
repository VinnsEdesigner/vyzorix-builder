#include "okfw/ids.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <unistd.h>

namespace vyzorix::okfw {

std::string run_token() {
    static std::atomic<unsigned long> counter{0};

    const unsigned long n = counter.fetch_add(1);
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d-%lx-%lx",
                  static_cast<int>(::getpid()),
                  static_cast<unsigned long>(nanos),
                  n);
    return buf;
}

}  // namespace vyzorix::okfw
