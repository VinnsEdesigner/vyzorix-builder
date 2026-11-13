// The bun adapter: A single self-contained binary via `bun build --compile`.
#include "modules/adapter/adapters.hpp"

#include "modules/adapter/adapter_common.hpp"

namespace vyzorix::plan::adapter {
namespace {

class BunAdapter final : public Adapter {
public:
    [[nodiscard]] std::string name() const override { return "bun"; }

    [[nodiscard]] okfw::Result<BuildPlan> build_plan(const domain::RecipeArtifact& a,
                                                     const domain::Target& t,
                                                     const std::string& version) const override {
        BuildPlan plan;
        plan.context = source_context(a);
        plan.lang = "bun";
        plan.image = builder_image("bun", t.suite);
        add_build_arg(plan, "BUN_TARGET", t.abi == "arm64-v8a" ? "bun-linux-arm64" : (t.abi == "x86_64" ? "bun-linux-x64" : "bun-linux-arm64"));
        add_build_arg(plan, "BUILD_CMD", a.build);
        plan.outputs = {dir_output(a, t, version)};
        with_cmd(plan, a);
        return okfw::Result<BuildPlan>::ok(std::move(plan));
    }
};

}  // namespace

std::shared_ptr<Adapter> make_bun() { return std::make_shared<BunAdapter>(); }

}  // namespace vyzorix::plan::adapter
