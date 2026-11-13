// The dotnet adapter: A self-contained .NET publish, no runtime needed on the device.
#include "modules/adapter/adapters.hpp"

#include "modules/adapter/adapter_common.hpp"

namespace vyzorix::plan::adapter {
namespace {

class DotnetAdapter final : public Adapter {
public:
    [[nodiscard]] std::string name() const override { return "dotnet"; }

    [[nodiscard]] okfw::Result<BuildPlan> build_plan(const domain::RecipeArtifact& a,
                                                     const domain::Target& t,
                                                     const std::string& version) const override {
        BuildPlan plan;
        plan.context = source_context(a);
        plan.lang = "dotnet";
        plan.image = builder_image("dotnet", t.suite);
        add_build_arg(plan, "DOTNET_RID", t.debarch == "arm64" ? "linux-arm64" : (t.debarch == "armhf" ? "linux-arm" : "linux-x64"));
        add_build_arg(plan, "BUILD_CMD", a.build);
        plan.outputs = {dir_output(a, t, version)};
        with_cmd(plan, a);
        return okfw::Result<BuildPlan>::ok(std::move(plan));
    }
};

}  // namespace

std::shared_ptr<Adapter> make_dotnet() { return std::make_shared<DotnetAdapter>(); }

}  // namespace vyzorix::plan::adapter
