#include "support/test.hpp"

#include "okfw/result.hpp"

using namespace vyzorix::okfw;

VYZ_TEST(result_carries_a_value_or_an_error) {
    auto ok = Result<int>::ok(7);
    CHECK(ok.has_value());
    CHECK_EQ(ok.value(), 7);

    auto bad = Result<int>::fail(error("nope"));
    CHECK(!bad.has_value());
    CHECK_EQ(bad.error().message, "nope");
}

VYZ_TEST(result_void_carries_only_an_error) {
    auto ok = Result<void>::ok();
    CHECK(ok.has_value());

    auto bad = Result<void>::fail(error("refused"));
    CHECK(!bad.has_value());
    CHECK_EQ(bad.error().message, "refused");
}

VYZ_TEST(result_explicit_bool) {
    auto ok = Result<int>::ok(1);
    if (ok) {
        CHECK(true);
    } else {
        CHECK(false);
    }
}
