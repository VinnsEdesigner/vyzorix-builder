// The JVM adapter: a fat jar, native-imaged by default.
#include "modules/adapter/adapters.hpp"

#include "modules/adapter/adapter_common.hpp"

namespace vyzorix::plan::adapter {
namespace {

class JvmAdapter final : public Adapter {
public:
    [[nodiscard]] std::string name() const override { return "jvm"; }

    [[nodiscard]] okfw::Result<BuildPlan> build_plan(const domain::RecipeArtifact& a,
                                                     const domain::Target& t,
                                                     const std::string& version) const override {
        BuildPlan plan;
        plan.context = source_context(a);
        plan.lang = "jvm";
        plan.image = builder_image("jvm", t.suite);
        plan.libc_floor = t.libc_floor;

        std::string mode = "native-image";
        auto it = a.opts.find("jvm");
        if (it != a.opts.end()) mode = it->second;

        add_build_arg(plan, "DEB_ARCH", t.debarch);
        add_build_arg(plan, "JVM_MODE", mode);
        add_build_arg(plan, "BUILD_CMD", a.build);
        plan.outputs = {dir_output(a, t, version)};
        with_cmd(plan, a);
        return okfw::Result<BuildPlan>::ok(std::move(plan));
    }
};

}  // namespace

std::shared_ptr<Adapter> make_jvm() { return std::make_shared<JvmAdapter>(); }

}  // namespace vyzorix::plan::adapter
