#include "support/test.hpp"

#include "okfw/retry.hpp"

#include <chrono>
#include <string>

using namespace vyzorix;

VYZ_TEST(retry_transient_status_classification) {
    // A 5xx and a 429 are worth another attempt; the rest of the 4xx are not.
    CHECK(okfw::is_transient_status(500));
    CHECK(okfw::is_transient_status(502));
    CHECK(okfw::is_transient_status(503));
    CHECK(okfw::is_transient_status(599));
    CHECK(okfw::is_transient_status(429));

    CHECK(!okfw::is_transient_status(400));
    CHECK(!okfw::is_transient_status(401));
    CHECK(!okfw::is_transient_status(403));
    CHECK(!okfw::is_transient_status(404));
    CHECK(!okfw::is_transient_status(200));
}

VYZ_TEST(retry_succeeds_after_transient_failures) {
    int calls = 0;
    auto sleeps = 0;

    auto r = okfw::retry<int>(
        3, std::chrono::milliseconds(1), [](const okfw::Error&) { return true; },
        [&]() -> okfw::Result<int> {
            calls++;
            if (calls < 3) return okfw::Result<int>::fail(okfw::error("transient"));
            return okfw::Result<int>::ok(42);
        },
        [&](std::chrono::milliseconds) { sleeps++; });

    CHECK(r.has_value());
    if (r.has_value()) CHECK_EQ(r.value(), 42);
    CHECK_EQ(calls, 3);
    CHECK_EQ(sleeps, 2);  // one wait between each pair of attempts
}

VYZ_TEST(retry_gives_up_after_the_attempt_budget) {
    int calls = 0;
    auto r = okfw::retry<int>(
        3, std::chrono::milliseconds(1), [](const okfw::Error&) { return true; },
        [&]() -> okfw::Result<int> {
            calls++;
            return okfw::Result<int>::fail(okfw::error("still transient"));
        },
        [](std::chrono::milliseconds) {});

    CHECK(!r.has_value());
    CHECK_EQ(calls, 3);  // attempts, not attempts+1
}

VYZ_TEST(retry_does_not_retry_a_definitive_failure) {
    // A 401 is not transient: retrying it wastes time and hides the real error.
    int calls = 0;
    auto r = okfw::retry<int>(
        5, std::chrono::milliseconds(1),
        [](const okfw::Error& e) { return e.message.find("transient") != std::string::npos; },
        [&]() -> okfw::Result<int> {
            calls++;
            return okfw::Result<int>::fail(okfw::error("definitive refusal"));
        },
        [](std::chrono::milliseconds) {});

    CHECK(!r.has_value());
    CHECK_EQ(calls, 1);  // no retry
}
