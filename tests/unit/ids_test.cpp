#include "support/test.hpp"

#include "okfw/ids.hpp"

#include <set>
#include <string>

using namespace vyzorix;

VYZ_TEST(ids_run_token_is_unique_per_call) {
    // The token names a build's image tag and scratch dir, so two calls in one
    // process must never collide (concurrent builds must not share
    // a name). A set of many tokens catches a counter that does not advance.
    std::set<std::string> seen;
    for (int i = 0; i < 1000; i++) {
        auto t = okfw::run_token();
        CHECK(!t.empty());
        seen.insert(t);
    }
    CHECK_EQ(seen.size(), static_cast<size_t>(1000));
}

VYZ_TEST(ids_run_token_is_filename_safe) {
    // It goes into a path, an image tag, and a container name: only lowercase
    // alphanumerics and dashes, no slash or colon.
    auto t = okfw::run_token();
    for (char c : t) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        CHECK(ok);
    }
}
