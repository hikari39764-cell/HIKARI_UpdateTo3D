#pragma once
#include <string>
#include <cstdint>
#include <memory>
#include <vector>
#include <DirectXMath.h>
#include <Vfx/Common/HIKARI_FxTypes.h>

#include "Assets/HIKARI_AssetGuid.h"
#include "HIKARI_IComponent.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"


namespace HIKARI {

    class ModelAsset;
    class Material;

    enum class ModelRenderDebugMode {
        Normal,
        WireOverlay,
        WireOnly,
        BoundsOnly,
    };

    struct ModelMaterialOverrideSlot {
        uint32_t slotIndex = 0;
        AssetGuid materialAssetGuid{};
    };

    class ModelComponent final :
        public IComponent,
        public IGeometryFitProvider {
    public:
        ~ModelComponent() override;

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
        void SetCastShadow(bool enabled);
        bool GetCastShadow() const;
        void SetReceiveShadow(bool enabled);
        bool GetReceiveShadow() const;
        void SetRenderStatic(bool enabled);
        bool IsRenderStatic() const;
        void SetRenderDebugMode(ModelRenderDebugMode mode);
        ModelRenderDebugMode GetRenderDebugMode() const;
        void SetWireColor(uint32_t color);
        uint32_t GetWireColor() const;
        void SetMaxWireLines(uint32_t count);
        uint32_t GetMaxWireLines() const;
        bool GetWirePerPrimitiveColor() const;

        void RenderImGui() override;
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;
        bool QueryGeometryFit(
            GeometryFitDesc& outFit) const noexcept override;

        const std::string& GetAssetId() const;
        void SetAssetId(std::string assetId);

        void SetPostGroupMask(uint32_t mask);
        uint32_t GetPostGroupMask() const;

        void SetMaterialFxProfileId(std::string profileId);
        const std::string& GetMaterialFxProfileId() const;
        const std::vector<ModelMaterialOverrideSlot>& GetMaterialOverrides() const;
        void SetMaterialOverride(uint32_t slotIndex, AssetGuid materialGuid);
        void ClearMaterialOverride(uint32_t slotIndex);
        const Material* GetRuntimeMaterialOverride() const;
        void SetRuntimeMaterialOverride(std::unique_ptr<Material> material, AssetGuid guid);
        void ClearRuntimeMaterialOverride();
        const AssetGuid& GetRuntimeMaterialOverrideGuid() const;
        DirectX::XMFLOAT4 (&GetMaterialFxParamValues())[VFX::kMaterialFxUserCount];
        const DirectX::XMFLOAT4 (&GetMaterialFxParamValues() const)[VFX::kMaterialFxUserCount];
        bool AreMaterialFxValuesInitialized() const;
        void SetMaterialFxParamValues(const DirectX::XMFLOAT4 (&values)[VFX::kMaterialFxUserCount], bool initialized);
        bool SetMaterialFxFloat(const std::string& key, float value);
        bool SetMaterialFxFloat2(const std::string& key, const DirectX::XMFLOAT2& value);
        bool SetMaterialFxFloat3(const std::string& key, const DirectX::XMFLOAT3& value);
        bool SetMaterialFxFloat4(const std::string& key, const DirectX::XMFLOAT4& value);
        bool GetMaterialFxFloat(const std::string& key, float& out) const;
        void ResetMaterialFxToProfileDefaults();

    private:
        void NotifyRenderStateDirty();

        ModelAsset* asset_ = nullptr;
        std::string assetId_{};
        bool visible_ = true;
        bool showSkeletonDebug_ = false;
        bool skeletonDebugXRay_ = false;
        bool castShadow_ = true;
        bool receiveShadow_ = true;
        bool renderStatic_ = false;
        ModelRenderDebugMode debugRenderMode_ = ModelRenderDebugMode::Normal;
        uint32_t wireColor_ = 0x00FFAAFF;
        uint32_t maxWireLines_ = 20000;
        bool wirePerPrimitiveColor_ = true;
        uint32_t postGroupMask_ = 0;
        std::string materialFxProfileId_{};
        std::vector<ModelMaterialOverrideSlot> materialOverrides_{};
        std::unique_ptr<Material> runtimeMaterialOverride_{};
        AssetGuid runtimeMaterialOverrideGuid_{};
        DirectX::XMFLOAT4 materialFxParamValues_[VFX::kMaterialFxUserCount]{};
        bool materialFxValuesInitialized_ = false;
    };

} // namespace HIKARI
