#include "support/test.hpp"

#include "okfw/json.hpp"

using vyzorix::okfw::Json;
using vyzorix::okfw::decode_string_array;

VYZ_TEST(json_parses_nested_objects) {
    auto parsed = Json::parse(R"({"a":{"b":[1,2,3],"c":"x"},"d":true,"e":null})");
    CHECK(parsed.has_value());

    const Json* a = parsed.value().find("a");
    CHECK(a != nullptr);
    CHECK_EQ(a->find("c")->as_string(), "x");

    const Json* b = a->find("b");
    CHECK(b != nullptr);
    CHECK_EQ(b->as_array()->size(), static_cast<size_t>(3));
    CHECK_EQ((*b->as_array())[2].as_number(), 3.0);

    CHECK(parsed.value().find("d")->as_bool());
    CHECK(parsed.value().find("e")->type == vyzorix::okfw::JsonType::Null);
}

VYZ_TEST(json_dump_round_trips) {
    auto parsed = Json::parse(R"({"url":"https://e/x","sha256":"abc","size":42})");
    CHECK(parsed.has_value());
    auto again = Json::parse(parsed.value().dump());
    CHECK(again.has_value());
    CHECK_EQ(again.value().find("url")->as_string(), "https://e/x");
    CHECK_EQ(again.value().find("size")->as_number(), 42.0);
}

VYZ_TEST(json_refuses_malformed) {
    CHECK(!Json::parse("{").has_value());
    CHECK(!Json::parse("{\"a\":}").has_value());
    CHECK(!Json::parse("[1,2").has_value());
    CHECK(!Json::parse("nope").has_value());
}

VYZ_TEST(json_escapes_strings) {
    auto j = Json::of(std::string("line1\nline2\t\"quoted\""));
    std::string dumped = j.dump();
    auto back = Json::parse(dumped);
    CHECK(back.has_value());
    CHECK_EQ(back.value().as_string(), "line1\nline2\t\"quoted\"");
}

VYZ_TEST(json_parses_escapes_and_unicode) {
    auto parsed = Json::parse(R"({"s":"a\nb\t\"q\"","u":"\u0041"})");
    CHECK(parsed.has_value());
    CHECK_EQ(parsed.value().find("s")->as_string(), "a\nb\t\"q\"");
    CHECK_EQ(parsed.value().find("u")->as_string(), "A");
}

VYZ_TEST(json_missing_key_returns_nullptr) {
    auto parsed = Json::parse(R"({"a":1})");
    CHECK(parsed.has_value());
    CHECK(parsed.value().find("nope") == nullptr);
}

VYZ_TEST(json_type_mismatch_yields_the_fallback) {
    auto parsed = Json::parse(R"({"n":5,"s":"x"})");
    CHECK(parsed.has_value());
    CHECK_EQ(parsed.value().find("n")->as_string("fallback"), "fallback");
    CHECK_EQ(parsed.value().find("s")->as_number(42.0), 42.0);
}

VYZ_TEST(json_decode_string_array_tolerates_absence_and_junk) {
    auto parsed = Json::parse(R"({"a":["x","y"],"b":"notarray","c":[1,2]})");
    CHECK(parsed.has_value());
    CHECK_EQ(decode_string_array(parsed.value().find("a")).size(), static_cast<size_t>(2));
    CHECK_EQ(decode_string_array(parsed.value().find("b")).size(), static_cast<size_t>(0));
    CHECK_EQ(decode_string_array(parsed.value().find("c")).size(), static_cast<size_t>(0));
    CHECK_EQ(decode_string_array(nullptr).size(), static_cast<size_t>(0));
}
