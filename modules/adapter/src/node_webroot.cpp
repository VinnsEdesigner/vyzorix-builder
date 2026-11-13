// The Node/TS webroot adapter: a static site, architecture-free.
#include "modules/adapter/adapters.hpp"

#include "modules/adapter/adapter_common.hpp"

namespace vyzorix::plan::adapter {
namespace {

class NodeWebrootAdapter final : public Adapter {
public:
    [[nodiscard]] std::string name() const override { return "node-webroot"; }

    [[nodiscard]] okfw::Result<BuildPlan> build_plan(const domain::RecipeArtifact& a,
                                                     const domain::Target& t,
                                                     const std::string& version) const override {
        BuildPlan plan;
        plan.context = source_context(a);
        plan.lang = "node-webroot";
        plan.image = builder_image("node", t.suite);
        add_build_arg(plan, "BUILD_CMD", a.build);
        add_build_arg(plan, "DIST", a.dist);
        plan.outputs = {webroot_output(a, t, version)};
        with_cmd(plan, a);
        return okfw::Result<BuildPlan>::ok(std::move(plan));
    }
};

}  // namespace

std::shared_ptr<Adapter> make_node_webroot() { return std::make_shared<NodeWebrootAdapter>(); }

}  // namespace vyzorix::plan::adapter
