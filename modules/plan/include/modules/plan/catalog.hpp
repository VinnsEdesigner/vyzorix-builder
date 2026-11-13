// plan — the adapter registry: a lang to its adapter, or a refusal.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "modules/plan/adapter.hpp"
#include "okfw/result.hpp"

namespace vyzorix::plan {

class Catalog {
public:
    void add(std::shared_ptr<Adapter> adapter);

    // lookup returns the adapter for a lang, or a refusal naming the lang and
    // the languages that are known.
    [[nodiscard]] okfw::Result<std::shared_ptr<Adapter>> lookup(const std::string& lang) const;

    [[nodiscard]] std::vector<std::string> languages() const;

private:
    std::map<std::string, std::shared_ptr<Adapter>> adapters_;
};

}  // namespace vyzorix::plan
