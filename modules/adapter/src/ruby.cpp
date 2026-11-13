// The ruby adapter: The app plus its gems, for a device with ruby on the rootfs.
#include "modules/adapter/adapters.hpp"

#include "modules/adapter/adapter_common.hpp"

namespace vyzorix::plan::adapter {
namespace {

class RubyAdapter final : public Adapter {
public:
    [[nodiscard]] std::string name() const override { return "ruby"; }

    [[nodiscard]] okfw::Result<BuildPlan> build_plan(const domain::RecipeArtifact& a,
                                                     const domain::Target& t,
                                                     const std::string& version) const override {
        BuildPlan plan;
        plan.context = source_context(a);
        plan.lang = "ruby";
        plan.image = builder_image("ruby", t.suite);
        add_build_arg(plan, "BUILD_CMD", a.build);
        plan.outputs = {dir_output(a, t, version)};
        with_cmd(plan, a);
        return okfw::Result<BuildPlan>::ok(std::move(plan));
    }
};

}  // namespace

std::shared_ptr<Adapter> make_ruby() { return std::make_shared<RubyAdapter>(); }

}  // namespace vyzorix::plan::adapter
