#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <json.hpp>

namespace HIKARI {

    class GameObject;
    class IComponent;
    struct SceneObjectData;

    struct ComponentTypeInfo {
        using FactoryFn = std::function<std::unique_ptr<IComponent>()>;
        using InitializeDefaultsFn = std::function<void(const SceneObjectData& object, nlohmann::json& properties)>;

        std::string typeName{};
        FactoryFn factory{};
        std::vector<std::string> requiredComponents{};
        std::vector<std::string> optionalComponents{};
        std::vector<std::string> incompatibleComponents{};
        bool allowMultiple = false;
        InitializeDefaultsFn initializeDefaults{};
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
