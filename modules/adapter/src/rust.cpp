// The Rust adapter: a musl static binary.
#include "modules/adapter/adapters.hpp"

#include "modules/adapter/adapter_common.hpp"

namespace vyzorix::plan::adapter {
namespace {

class RustAdapter final : public Adapter {
public:
    [[nodiscard]] std::string name() const override { return "rust"; }

    [[nodiscard]] okfw::Result<BuildPlan> build_plan(const domain::RecipeArtifact& a,
                                                     const domain::Target& t,
                                                     const std::string& version) const override {
        BuildPlan plan;
        plan.context = source_context(a);
        plan.lang = "rust";
        plan.image = builder_image("rust", t.suite);
        add_build_arg(plan, "RUST_TARGET", rust_triple(t.abi));
        add_build_arg(plan, "BUILD_CMD", a.build);
        plan.outputs = {dir_output(a, t, version)};
        with_cmd(plan, a);
        return okfw::Result<BuildPlan>::ok(std::move(plan));
    }
};

}  // namespace

std::shared_ptr<Adapter> make_rust() { return std::make_shared<RustAdapter>(); }

}  // namespace vyzorix::plan::adapter
