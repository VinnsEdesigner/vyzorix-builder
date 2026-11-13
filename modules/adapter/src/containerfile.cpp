// The Containerfile adapter: the escape hatch.
#include "modules/adapter/adapters.hpp"

#include "modules/adapter/adapter_common.hpp"

namespace vyzorix::plan::adapter {
namespace {

class ContainerfileAdapter final : public Adapter {
public:
    [[nodiscard]] std::string name() const override { return "containerfile"; }

    [[nodiscard]] okfw::Result<BuildPlan> build_plan(const domain::RecipeArtifact& a,
                                                     const domain::Target& t,
                                                     const std::string& version) const override {
        if (a.file.empty()) {
            return okfw::Result<BuildPlan>::fail(okfw::error("artifact '" + a.name +
                                                             "': lang = \"containerfile\" needs `file`"));
        }

        BuildPlan plan;
        plan.lang = "containerfile";
        plan.containerfile = a.file;
        plan.context = source_context(a);
        add_build_arg(plan, "DEB_ARCH", t.debarch);
        add_build_arg(plan, "TARGET_ABI", t.abi);
        // The escape hatch owns its build, so it is the one place the engine
        // sets --platform (contract R9): a Containerfile that must build *on*
        // the target architecture (a foreign toolchain, a qemu-emulated build)
        // otherwise cannot request it. A first-class adapter must NOT set it —
        // those cross-compile natively on the host, so forcing the platform would
        // demand emulation and break the build.
        with_target(plan, t);

        Output o;
        o.name = a.name;
        o.arch = a.arch.empty() ? t.abi : a.arch;
        o.kind = a.kind.empty() ? "dir" : a.kind;
        // The Containerfile packs its own archive; the engine copies it out. The
        // recipe names the archive path, or the derived name is used.
        o.file_name = domain::artifact_file_name(a.app, a.name, t.os, o.arch, version);
        o.archive = a.out.empty() ? "/out/" + o.file_name : a.out;
        plan.outputs = {o};
        with_cmd(plan, a);
        with_target(plan, t);
        return okfw::Result<BuildPlan>::ok(std::move(plan));
    }
};

}  // namespace

std::shared_ptr<Adapter> make_containerfile() { return std::make_shared<ContainerfileAdapter>(); }

}  // namespace vyzorix::plan::adapter
