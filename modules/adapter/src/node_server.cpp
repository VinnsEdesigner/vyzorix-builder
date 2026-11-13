// The Node/TS server adapter: an app plus its production dependencies.
//
// Unlike the webroot adapter (static files), a server runs on the device, so the
// artifact carries the app and its production node_modules. Node itself is
// expected on the device rootfs; a project that needs a bundled runtime is the
// Containerfile escape hatch.
#include "modules/adapter/adapters.hpp"

#include "modules/adapter/adapter_common.hpp"

namespace vyzorix::plan::adapter {
namespace {

class NodeServerAdapter final : public Adapter {
public:
    [[nodiscard]] std::string name() const override { return "node"; }

    [[nodiscard]] okfw::Result<BuildPlan> build_plan(const domain::RecipeArtifact& a,
                                                     const domain::Target& t,
                                                     const std::string& version) const override {
        BuildPlan plan;
        plan.context = source_context(a);
        plan.lang = "node";
        plan.image = builder_image("node", t.suite);
        add_build_arg(plan, "BUILD_CMD", a.build);
        add_build_arg(plan, "DIST", a.dist);
        plan.outputs = {dir_output(a, t, version)};
        with_cmd(plan, a);
        return okfw::Result<BuildPlan>::ok(std::move(plan));
    }
};

}  // namespace

std::shared_ptr<Adapter> make_node_server() { return std::make_shared<NodeServerAdapter>(); }

}  // namespace vyzorix::plan::adapter
