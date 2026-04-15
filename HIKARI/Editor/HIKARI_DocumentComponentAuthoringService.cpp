#include "HIKARI_DocumentComponentAuthoringService.h"

#include <sstream>

namespace HIKARI {

    ComponentAddResult DocumentComponentAuthoringService::AddComponent(const ComponentRegistry& registry, SceneObjectData& object, std::string_view typeName) const {
        ComponentAddResult result{};
        if (!AddSingleComponent(registry, object, typeName, false, result)) {
            return result;
        }

        if (result.autoAddedDependencies.empty()) {
            result.message = "Added component: " + std::string(typeName);
        } else {
            std::ostringstream oss;
            oss << "Added component: " << typeName << " (auto-added dependencies: ";
            for (size_t i = 0; i < result.autoAddedDependencies.size(); ++i) {
                if (i > 0) {
                    oss << ", ";
                }
                oss << result.autoAddedDependencies[i];
            }
            oss << ")";
            result.message = oss.str();
        }
        return result;
    }

    bool DocumentComponentAuthoringService::HasComponent(const SceneObjectData& object, std::string_view typeName) const {
        for (const SceneComponentData& component : object.components) {
            if (component.type == typeName) {
                return true;
            }
        }
        return false;
    }

    bool DocumentComponentAuthoringService::ContainsTypeName(const std::vector<std::string>& values, std::string_view typeName) const {
        for (const std::string& value : values) {
            if (value == typeName) {
                return true;
            }
        }
        return false;
    }

    bool DocumentComponentAuthoringService::AddSingleComponent(
        const ComponentRegistry& registry,
        SceneObjectData& object,
        std::string_view typeName,
        bool isDependency,
        ComponentAddResult& result) const {
        const ComponentTypeInfo* info = registry.Find(typeName);
        if (!info) {
            result.success = false;
            result.message = "Unknown component type: " + std::string(typeName);
            return false;
        }

        if (!info->allowMultiple && HasComponent(object, typeName)) {
            result.success = false;
            result.message = "Component already exists and does not allow multiple: " + std::string(typeName);
            return false;
        }

        for (const std::string& incompatibleType : info->incompatibleComponents) {
            if (HasComponent(object, incompatibleType)) {
                result.success = false;
                result.message = "Component conflict: " + std::string(typeName) + " is incompatible with " + incompatibleType;
                return false;
            }
        }

        for (const SceneComponentData& existing : object.components) {
            const ComponentTypeInfo* existingInfo = registry.Find(existing.type);
            if (!existingInfo) {
                continue;
            }
            if (ContainsTypeName(existingInfo->incompatibleComponents, typeName)) {
                result.success = false;
                result.message = "Component conflict: " + existing.type + " is incompatible with " + std::string(typeName);
                return false;
            }
        }

        for (const std::string& dependencyType : info->requiredComponents) {
            if (HasComponent(object, dependencyType)) {
                continue;
            }

            if (!AddSingleComponent(registry, object, dependencyType, true, result)) {
                if (result.message.empty()) {
                    result.message = "Failed to add required component: " + dependencyType;
                }
                return false;
            }
        }

        nlohmann::json properties = nlohmann::json::object();
        if (info->initializeDefaults) {
            info->initializeDefaults(object, properties);
        }

        object.components.push_back(SceneComponentData{ info->typeName, std::move(properties) });
        result.documentChanged = true;
        result.addedComponents.push_back(info->typeName);
        if (isDependency) {
            result.autoAddedDependencies.push_back(info->typeName);
        }
        result.success = true;
        return true;
    }

} // namespace HIKARI
