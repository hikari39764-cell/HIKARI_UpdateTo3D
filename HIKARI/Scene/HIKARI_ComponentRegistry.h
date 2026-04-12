#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace HIKARI {

    class GameObject;
    class IComponent;

    struct ComponentTypeInfo {
        using FactoryFn = std::function<std::unique_ptr<IComponent>()>;
        std::string typeName{};
        FactoryFn factory{};
    };

    class ComponentRegistry {
    public:
        void Register(ComponentTypeInfo info);
        const ComponentTypeInfo* Find(std::string_view typeName) const;
        std::vector<std::string> GetTypeNames() const;

        IComponent* AddComponentToObject(GameObject& object, std::string_view typeName) const;

    private:
        std::unordered_map<std::string, ComponentTypeInfo> byName_{};
    };

} // namespace HIKARI
