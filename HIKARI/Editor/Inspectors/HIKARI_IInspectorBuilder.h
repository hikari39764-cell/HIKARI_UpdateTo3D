#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace HIKARI {

    enum class AssetType;
    class AssetDatabase;
    class AssetRegistry;
    class SceneCatalog;

    struct InspectorContext {
        AssetRegistry* assetRegistry = nullptr;
        AssetDatabase* assetDatabase = nullptr;
        SceneCatalog* sceneCatalog = nullptr;
    };

    class IInspectorBuilder {
    public:
        virtual ~IInspectorBuilder() = default;

        virtual void SetContext(const InspectorContext& context) = 0;

        virtual bool Bool(std::string_view label, bool& value) = 0;
        virtual bool Int(std::string_view label, int& value) = 0;
        virtual bool Float(std::string_view label, float& value) = 0;
        virtual bool String(std::string_view label, std::string& value) = 0;
        virtual bool Vec2(std::string_view label, float& x, float& y) = 0;
        virtual bool AssetIdPicker(std::string_view label, AssetType assetType, std::string& value) = 0;
        virtual bool SceneIdPicker(std::string_view label, std::string& value) = 0;
    };

} // namespace HIKARI
