#include "Scene/HIKARI_SystemTypeRegistry.h"

#include <utility>

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

bool SystemTypeRegistry::Register(SystemTypeInfo info) {
    if (info.systemId.empty() || !info.factory) {
        return false;
    }
    if (info.displayName.empty()) {
        info.displayName = info.systemId;
    }
    const std::string systemId = info.systemId;
    return byName_.emplace(systemId, std::move(info)).second;
}

void SystemTypeRegistry::Clear() noexcept {
    byName_.clear();
}

const SystemTypeInfo* SystemTypeRegistry::Find(std::string_view systemId) const {
    const auto it = byName_.find(std::string(systemId));
    if (it == byName_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::unique_ptr<ISystem> SystemTypeRegistry::Create(
    std::string_view systemId,
    const nlohmann::json& settings) const {

    const SystemTypeInfo* info = Find(systemId);
    if (!info) {
        return nullptr;
    }
    return info->factory(settings);
}

std::vector<std::string> SystemTypeRegistry::GetTypeNames() const {
    std::vector<std::string> names;
    names.reserve(byName_.size());
    for (const auto& [name, _] : byName_) {
        names.push_back(name);
    }
    return names;
}

} // namespace HIKARI
