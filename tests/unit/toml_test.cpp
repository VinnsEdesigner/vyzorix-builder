#include "support/test.hpp"

#include "okfw/toml.hpp"

using namespace vyzorix;

// The recipe parser is hand-rolled, so its edge cases are pinned here rather
// than only through the recipe mapping.

VYZ_TEST(toml_parses_tables_and_arrays_of_tables) {
    auto doc = okfw::toml_parse(
        "[build]\nversion = \"v1\"\n\n[[artifact]]\nname = \"a\"\n\n[[artifact]]\nname = \"b\"\n");
    CHECK(doc.has_value());

    const auto& d = doc.value();
    CHECK(d.at("build").table.at("version").as_string() == "v1");

    const auto& arts = d.at("artifact");
    CHECK(arts.type == okfw::TomlValue::Type::Array);
    CHECK_EQ(arts.array.size(), static_cast<size_t>(2));
    CHECK(arts.array[0].table.at("name").as_string() == "a");
    CHECK(arts.array[1].table.at("name").as_string() == "b");
}

VYZ_TEST(toml_reads_integers_bools_and_string_arrays) {
    auto doc = okfw::toml_parse(
        "[build]\nabi = [\"arm64-v8a\", \"x86_64\"]\n\n[[artifact]]\nport = 8787\nautostart = true\n");
    CHECK(doc.has_value());

    const auto& b = doc.value().at("build");
    auto abis = b.table.at("abi").as_strings();
    CHECK_EQ(abis.size(), static_cast<size_t>(2));
    CHECK(abis[1] == "x86_64");

    const auto& a = doc.value().at("artifact").array[0];
    CHECK_EQ(a.table.at("port").as_int(), 8787);
    CHECK(a.table.at("autostart").as_bool());
}

VYZ_TEST(toml_ignores_comments_and_blank_lines) {
    auto doc = okfw::toml_parse(
        "# a comment\n\n[build]   # trailing\nversion = \"v1\"  # another\n");
    CHECK(doc.has_value());
    CHECK(doc.value().at("build").table.at("version").as_string() == "v1");
}

VYZ_TEST(toml_refuses_dotted_table_header) {
    // The recipe schema is flat; a dotted header is refused by name.
    auto doc = okfw::toml_parse("[app.pwa]\nname = \"x\"\n");
    CHECK(!doc.has_value());
}

VYZ_TEST(toml_refuses_a_line_without_equals) {
    auto doc = okfw::toml_parse("[build]\nthis is not toml\n");
    CHECK(!doc.has_value());
}

VYZ_TEST(toml_handles_inline_tables_with_bare_keys) {
    auto doc = okfw::toml_parse("[[artifact]]\nenv = { MI_PORT = \"8787\", HOST = \"127.0.0.1\" }\n");
    CHECK(doc.has_value());
    const auto& env = doc.value().at("artifact").array[0].table.at("env");
    CHECK(env.type == okfw::TomlValue::Type::Table);
    CHECK(env.table.at("MI_PORT").as_string() == "8787");
}
