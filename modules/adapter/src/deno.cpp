// The Deno adapter: a single self-contained binary per ABI.
//
// `deno compile` embeds the runtime and the script into one executable, so the
// device needs nothing but the binary. Architecture-specific.
#include "modules/adapter/adapters.hpp"

#include "modules/adapter/adapter_common.hpp"

namespace vyzorix::plan::adapter {
namespace {

class DenoAdapter final : public Adapter {
public:
    [[nodiscard]] std::string name() const override { return "deno"; }

    [[nodiscard]] okfw::Result<BuildPlan> build_plan(const domain::RecipeArtifact& a,
                                                     const domain::Target& t,
                                                     const std::string& version) const override {
        BuildPlan plan;
        plan.context = source_context(a);
        plan.lang = "deno";
        plan.image = builder_image("deno", t.suite);
        add_build_arg(plan, "DENO_TARGET", deno_target(t.abi));
        add_build_arg(plan, "BUILD_CMD", a.build);
        plan.outputs = {dir_output(a, t, version)};
        with_cmd(plan, a);
        return okfw::Result<BuildPlan>::ok(std::move(plan));
    }
};

}  // namespace

std::shared_ptr<Adapter> make_deno() { return std::make_shared<DenoAdapter>(); }

}  // namespace vyzorix::plan::adapter
