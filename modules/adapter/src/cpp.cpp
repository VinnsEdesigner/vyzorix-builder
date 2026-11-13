// The C/C++ adapter: cross-compile, then bundle the shared-library closure.
#include "modules/adapter/adapters.hpp"

#include "modules/adapter/adapter_common.hpp"

namespace vyzorix::plan::adapter {
namespace {

class CppAdapter final : public Adapter {
public:
    [[nodiscard]] std::string name() const override { return "cpp"; }

    [[nodiscard]] okfw::Result<BuildPlan> build_plan(const domain::RecipeArtifact& a,
                                                     const domain::Target& t,
                                                     const std::string& version) const override {
        BuildPlan plan;
        plan.context = source_context(a);
        plan.lang = "cpp";
        plan.image = builder_image("cpp", t.suite);
        plan.libc_floor = a.glibc_floor.empty() ? t.libc_floor : a.glibc_floor;
        // The cross-toolchain package names for the target, passed explicitly so
        // the Dockerfile never has to derive them.
        const std::string tuple = gcc_tuple(t.debarch);
        const std::string prefix = tuple.substr(0, tuple.size() - std::string("linux-gnu").size());
        const std::string pkg = (t.debarch == "amd64") ? "x86-64" : (t.debarch == "arm64" ? "aarch64" : "arm");
        add_build_arg(plan, "CROSS_GCC", "gcc-" + pkg + "-linux-gnu");
        add_build_arg(plan, "CROSS_GXX", "g++-" + pkg + "-linux-gnu");
        add_build_arg(plan, "CROSS_BINUTILS", "binutils-" + pkg + "-linux-gnu");
        add_build_arg(plan, "CROSS_PREFIX", prefix);
        add_build_arg(plan, "MULTIARCH", tuple);
        add_build_arg(plan, "DEB_ARCH", t.debarch);
        add_build_arg(plan, "GLIBC_FLOOR", a.glibc_floor.empty() ? t.libc_floor : a.glibc_floor);
        add_build_arg(plan, "BUILD_CMD", a.build);
        add_build_arg(plan, "LIBS", join(a.libs));
        add_build_arg(plan, "STATIC_LIBS", join(a.statics));
        plan.outputs = {dir_output(a, t, version)};
        with_cmd(plan, a);
        return okfw::Result<BuildPlan>::ok(std::move(plan));
    }

private:
    static std::string join(const std::vector<std::string>& xs) {
        std::string out;
        for (const auto& x : xs) {
            if (!out.empty()) out.push_back(',');
            out += x;
        }
        return out;
    }
};

}  // namespace

std::shared_ptr<Adapter> make_cpp() { return std::make_shared<CppAdapter>(); }

}  // namespace vyzorix::plan::adapter
