// The built-in adapter wiring: the one place concrete adapters are bound to the
// plan port's Catalog. It lives in the adapter layer because plan defines the port
#include "modules/adapter/adapters.hpp"

#include "modules/plan/catalog.hpp"

namespace vyzorix::plan::adapter {

plan::Catalog built_in_catalog() {
    plan::Catalog c;
    c.add(make_golang());
    c.add(make_rust());
    c.add(make_cpp());
    c.add(make_node_webroot());
    c.add(make_node_server());
    c.add(make_deno());
    c.add(make_bun());
    c.add(make_zig());
    c.add(make_dotnet());
    c.add(make_ruby());
    c.add(make_php());
    c.add(make_python());
    c.add(make_jvm());
    c.add(make_containerfile());
    return c;
}

}  // namespace vyzorix::plan::adapter
