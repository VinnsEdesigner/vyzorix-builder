// adapter — the built-in adapters, one factory per language, and the catalog wiring.
#pragma once

#include <memory>

#include "modules/plan/adapter.hpp"
#include "modules/plan/catalog.hpp"

namespace vyzorix::plan::adapter {

std::shared_ptr<Adapter> make_golang();
std::shared_ptr<Adapter> make_rust();
std::shared_ptr<Adapter> make_cpp();
std::shared_ptr<Adapter> make_node_webroot();
std::shared_ptr<Adapter> make_node_server();
std::shared_ptr<Adapter> make_deno();
std::shared_ptr<Adapter> make_bun();
std::shared_ptr<Adapter> make_zig();
std::shared_ptr<Adapter> make_dotnet();
std::shared_ptr<Adapter> make_ruby();
std::shared_ptr<Adapter> make_php();
std::shared_ptr<Adapter> make_python();
std::shared_ptr<Adapter> make_jvm();
std::shared_ptr<Adapter> make_containerfile();

// built_in_catalog returns a catalog populated with every built-in adapter.
//
// It lives here, above plan, because wiring concrete adapters to the plan port is
// the adapter layer's job: plan defines the port and must not depend on what
// implements it. The composition root and the tests call this rather than
// reaching into plan for a list of languages it should not know.
[[nodiscard]] plan::Catalog built_in_catalog();

}  // namespace vyzorix::plan::adapter
