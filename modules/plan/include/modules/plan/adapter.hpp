// plan — the adapter port. Adding a language is adding one implementation.
#pragma once

#include <string>

#include "domain/recipe.hpp"
#include "domain/target.hpp"
#include "modules/plan/plan.hpp"
#include "okfw/result.hpp"

namespace vyzorix::plan {

class Adapter {
public:
    virtual ~Adapter() = default;
    Adapter() = default;
    Adapter(const Adapter&) = delete;
    Adapter& operator=(const Adapter&) = delete;
    Adapter(Adapter&&) = delete;
    Adapter& operator=(Adapter&&) = delete;

    // name is the recipe's `lang` value this adapter answers to.
    [[nodiscard]] virtual std::string name() const = 0;

    // plan decides the image, args and outputs for one artifact and target.
    [[nodiscard]] virtual okfw::Result<BuildPlan> build_plan(const domain::RecipeArtifact& artifact,
                                                            const domain::Target& target,
                                                            const std::string& version) const = 0;
};

}  // namespace vyzorix::plan
