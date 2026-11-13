// The Python adapter: the app plus wheels for the target platform.
#include "modules/adapter/adapters.hpp"

#include "modules/adapter/adapter_common.hpp"

namespace vyzorix::plan::adapter {
namespace {

class PythonAdapter final : public Adapter {
public:
    [[nodiscard]] std::string name() const override { return "python"; }

    [[nodiscard]] okfw::Result<BuildPlan> build_plan(const domain::RecipeArtifact& a,
                                                     const domain::Target& t,
                                                     const std::string& version) const override {
        BuildPlan plan;
        plan.context = source_context(a);
        plan.lang = "python";
        plan.image = builder_image("python", t.suite);
        plan.libc_floor = t.libc_floor;
        add_build_arg(plan, "PY_PLATFORM", pyplatform(t.abi));
        add_build_arg(plan, "PY_VER", "311");  // bookworm's python3 is 3.11
        add_build_arg(plan, "DEB_ARCH", t.debarch);
        add_build_arg(plan, "BUILD_CMD", a.build);
        plan.outputs = {dir_output(a, t, version)};
        with_cmd(plan, a);
        return okfw::Result<BuildPlan>::ok(std::move(plan));
    }
};

}  // namespace

std::shared_ptr<Adapter> make_python() { return std::make_shared<PythonAdapter>(); }

}  // namespace vyzorix::plan::adapter
