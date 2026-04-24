#pragma once
#include <string>
#include <cstdint>
#include <DirectXMath.h>

#include "HIKARI_IComponent.h"
#include "Assets/HIKARI_Assets.h"

namespace HIKARI {

    class ModelComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override { return "ModelComponent"; }

        void SetModelHandle(ASSET::AssetHandle<ASSET::ModelAsset> handle);
        ASSET::AssetHandle<ASSET::ModelAsset> GetModelHandle() const;

        void SetVisible(bool visible);
        bool IsVisible() const;
        void SetCastShadow(bool castShadow);
        bool CastShadow() const;
        void SetReceiveShadow(bool receiveShadow);
        bool ReceiveShadow() const;
        void SetRenderLayerMask(uint32_t mask);
        uint32_t GetRenderLayerMask() const;

        void RenderImGui() override;
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

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
        ASSET::AssetHandle<ASSET::ModelAsset> modelHandle_{};
        std::string modelPath_{};
        std::string modelAssetId_{};
        bool visible_ = true;
        bool castShadow_ = true;
        bool receiveShadow_ = true;
        uint32_t renderLayerMask_ = 0xFFFFFFFFu;
        uint32_t postGroupMask_ = 0;
        std::string materialFxProfileId_{};
        DirectX::XMFLOAT4 materialFxParamValues_[4]{};
        bool materialFxValuesInitialized_ = false;
    };

} // namespace HIKARI
