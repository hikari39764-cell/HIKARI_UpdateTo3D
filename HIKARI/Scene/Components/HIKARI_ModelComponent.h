#pragma once
#include <string>
#include <cstdint>
#include <DirectXMath.h>

#include "HIKARI_IComponent.h"

namespace HIKARI {

    class ModelAsset;

    enum class ModelSourceKind {
        Asset,
        Procedural,
    };

    enum class ProceduralMeshKind {
        Plane,
        GridPlane,
        Box,
        Sphere,
    };

    enum class ModelRenderDebugMode {
        Normal,
        WireOverlay,
        WireOnly,
        BoundsOnly,
    };

    struct ProceduralModelSettings {
        ProceduralMeshKind kind = ProceduralMeshKind::GridPlane;
        float width = 10.0f;
        float height = 10.0f;
        float depth = 1.0f;
        uint32_t segmentsX = 10;
        uint32_t segmentsY = 10;
        uint32_t segmentsZ = 1;
        uint32_t sphereSlices = 32;
        uint32_t sphereStacks = 16;
        bool doubleSided = true;
        bool generateTangents = true;
    };

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
        void SetCastShadow(bool enabled);
        bool GetCastShadow() const;
        void SetReceiveShadow(bool enabled);
        bool GetReceiveShadow() const;
        void SetSourceKind(ModelSourceKind kind);
        ModelSourceKind GetSourceKind() const;
        void SetProceduralSettings(const ProceduralModelSettings& settings);
        const ProceduralModelSettings& GetProceduralSettings() const;
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
        bool castShadow_ = true;
        bool receiveShadow_ = true;
        ModelSourceKind sourceKind_ = ModelSourceKind::Asset;
        ProceduralModelSettings procedural_{};
        ModelRenderDebugMode debugRenderMode_ = ModelRenderDebugMode::Normal;
        uint32_t wireColor_ = 0x00FFAAFF;
        uint32_t maxWireLines_ = 20000;
        bool wirePerPrimitiveColor_ = true;
        uint32_t postGroupMask_ = 0;
        std::string materialFxProfileId_{};
        DirectX::XMFLOAT4 materialFxParamValues_[4]{};
        bool materialFxValuesInitialized_ = false;
    };

} // namespace HIKARI
