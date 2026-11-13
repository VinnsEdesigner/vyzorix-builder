// The zig adapter: A static binary via the zig cross compiler.
#include "modules/adapter/adapters.hpp"

#include "modules/adapter/adapter_common.hpp"

namespace vyzorix::plan::adapter {
namespace {

class ZigAdapter final : public Adapter {
public:
    [[nodiscard]] std::string name() const override { return "zig"; }

    [[nodiscard]] okfw::Result<BuildPlan> build_plan(const domain::RecipeArtifact& a,
                                                     const domain::Target& t,
                                                     const std::string& version) const override {
        BuildPlan plan;
        plan.context = source_context(a);
        plan.lang = "zig";
        plan.image = builder_image("zig", t.suite);
        add_build_arg(plan, "ZIG_TARGET", t.debarch == "arm64" ? "aarch64-linux-musl" : (t.debarch == "armhf" ? "arm-linux-musleabihf" : "x86_64-linux-musl"));
        add_build_arg(plan, "BUILD_CMD", a.build);
        plan.outputs = {dir_output(a, t, version)};
        with_cmd(plan, a);
        return okfw::Result<BuildPlan>::ok(std::move(plan));
    }
};

}  // namespace

std::shared_ptr<Adapter> make_zig() { return std::make_shared<ZigAdapter>(); }

}  // namespace vyzorix::plan::adapter
