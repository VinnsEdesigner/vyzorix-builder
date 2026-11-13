#include "modules/plan/catalog.hpp"

#include <algorithm>

namespace vyzorix::plan {

void Catalog::add(std::shared_ptr<Adapter> adapter) {
    if (adapter) adapters_[adapter->name()] = std::move(adapter);
}

okfw::Result<std::shared_ptr<Adapter>> Catalog::lookup(const std::string& lang) const {
    auto it = adapters_.find(lang);
    if (it == adapters_.end()) {
        std::string known;
        for (const auto& l : languages()) {
            if (!known.empty()) known += ", ";
            known += l;
        }
        return okfw::Result<std::shared_ptr<Adapter>>::fail(
            okfw::error("unsupported lang '" + lang + "' (known: " + known + ")"));
    }
    return okfw::Result<std::shared_ptr<Adapter>>::ok(it->second);
}

std::vector<std::string> Catalog::languages() const {
    std::vector<std::string> out;
    out.reserve(adapters_.size());
    for (const auto& [name, _] : adapters_) out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace vyzorix::plan
