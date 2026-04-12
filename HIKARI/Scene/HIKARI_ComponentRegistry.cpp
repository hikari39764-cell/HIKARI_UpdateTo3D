#include "HIKARI_ComponentRegistry.h"

#include "HIKARI_GameObject.h"

namespace HIKARI {

    void ComponentRegistry::Register(ComponentTypeInfo info) {
        if (info.typeName.empty() || !info.factory) {
            return;
        }
        byName_[info.typeName] = std::move(info);
    }

    const ComponentTypeInfo* ComponentRegistry::Find(std::string_view typeName) const {
        const auto it = byName_.find(std::string(typeName));
        if (it == byName_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    std::vector<std::string> ComponentRegistry::GetTypeNames() const {
        std::vector<std::string> names;
        names.reserve(byName_.size());
        for (const auto& [name, _] : byName_) {
            names.push_back(name);
        }
        return names;
    }

    IComponent* ComponentRegistry::AddComponentToObject(GameObject& object, std::string_view typeName) const {
        const ComponentTypeInfo* info = Find(typeName);
        if (!info) {
            return nullptr;
        }

        std::unique_ptr<IComponent> component = info->factory();
        return object.AddComponentInstance(std::move(component));
    }

} // namespace HIKARI
