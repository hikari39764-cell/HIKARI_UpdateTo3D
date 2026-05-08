#pragma once
#include <string>
#include <cstdint>
#include <DirectXMath.h>

#include "HIKARI_IComponent.h"

namespace HIKARI {

    class ModelAsset;

    class ModelComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override { return "ModelComponent"; }

        void SetModelAsset(ModelAsset* asset);
        ModelAsset* GetModelAsset();
        const ModelAsset* GetModelAsset() const;

        void SetAsset(ModelAsset* asset); // legacy alias
        ModelAsset* GetAsset(); // legacy alias
        const ModelAsset* GetAsset() const; // legacy alias

        void SetVisible(bool visible);
        bool IsVisible() const;
        void SetSkeletonDebugVisible(bool visible);
        bool IsSkeletonDebugVisible() const;
        void SetSkeletonDebugXRay(bool enabled);
        bool IsSkeletonDebugXRay() const;

        void RenderImGui() override;
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        const std::string& GetAssetId() const;
        void SetAssetId(std::string assetId);

        void SetPostGroupMask(uint32_t mask);
        uint32_t GetPostGroupMask() const;

        void SetMaterialFxProfileId(std::string profileId);
        const std::string& GetMaterialFxProfileId() const;
        DirectX::XMFLOAT4 (&GetMaterialFxParamValues())[4];
        const DirectX::XMFLOAT4 (&GetMaterialFxParamValues() const)[4];
        bool AreMaterialFxValuesInitialized() const;
        bool SetMaterialFxFloat(const std::string& key, float value);
        bool SetMaterialFxFloat2(const std::string& key, const DirectX::XMFLOAT2& value);
        bool SetMaterialFxFloat3(const std::string& key, const DirectX::XMFLOAT3& value);
        bool SetMaterialFxFloat4(const std::string& key, const DirectX::XMFLOAT4& value);
        bool GetMaterialFxFloat(const std::string& key, float& out) const;
        void ResetMaterialFxToProfileDefaults();

    private:
        ModelAsset* asset_ = nullptr;
        std::string assetId_{};
        bool visible_ = true;
        bool showSkeletonDebug_ = false;
        bool skeletonDebugXRay_ = false;
        uint32_t postGroupMask_ = 0;
        std::string materialFxProfileId_{};
        DirectX::XMFLOAT4 materialFxParamValues_[4]{};
        bool materialFxValuesInitialized_ = false;
    };

} // namespace HIKARI
