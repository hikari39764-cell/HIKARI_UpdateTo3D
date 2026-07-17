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

    struct ComponentTypePresentation {
        std::string displayName{};
        std::string category{ "Other" };
        std::string description{};
        std::string featureId{};
    };

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
        ComponentTypePresentation presentation{};
    };

    class ComponentRegistry {
    public:
        bool Register(ComponentTypeInfo info);
        void Clear() noexcept;
        const ComponentTypeInfo* Find(std::string_view typeName) const;
        std::vector<std::string> GetTypeNames() const;
        std::vector<const ComponentTypeInfo*> GetTypeInfos() const;

        IComponent* AddComponentToObject(GameObject& object, std::string_view typeName) const;

    private:
        std::unordered_map<std::string, ComponentTypeInfo> byName_{};
    };

} // namespace HIKARI
