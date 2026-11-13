// The php adapter: The app plus its Composer dependencies, for a device with php on the rootfs.
#include "modules/adapter/adapters.hpp"

#include "modules/adapter/adapter_common.hpp"

namespace vyzorix::plan::adapter {
namespace {

class PhpAdapter final : public Adapter {
public:
    [[nodiscard]] std::string name() const override { return "php"; }

    [[nodiscard]] okfw::Result<BuildPlan> build_plan(const domain::RecipeArtifact& a,
                                                     const domain::Target& t,
                                                     const std::string& version) const override {
        BuildPlan plan;
        plan.context = source_context(a);
        plan.lang = "php";
        plan.image = builder_image("php", t.suite);
        add_build_arg(plan, "BUILD_CMD", a.build);
        plan.outputs = {dir_output(a, t, version)};
        with_cmd(plan, a);
        return okfw::Result<BuildPlan>::ok(std::move(plan));
    }
};

}  // namespace

std::shared_ptr<Adapter> make_php() { return std::make_shared<PhpAdapter>(); }

}  // namespace vyzorix::plan::adapter
