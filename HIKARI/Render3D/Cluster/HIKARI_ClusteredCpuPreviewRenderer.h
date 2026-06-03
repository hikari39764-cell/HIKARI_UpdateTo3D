#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <DirectXMath.h>

#include "Render3D/Cluster/HIKARI_ClusteredGeometryAsset.h"
#include "Render3D/Cluster/HIKARI_ClusteredRenderMode.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/HIKARI_Transform3D.h"
#include <Vfx/Common/HIKARI_FxTypes.h>

namespace HIKARI {
    class Material;
    class ModelAsset;
}

namespace HIKARI::RENDER3D::CLUSTER {

    struct ClusteredCpuPreviewStats {
        bool enabled = false;
        ClusteredRenderMode mode = ClusteredRenderMode::Off;
        uint32_t candidateObjectCount = 0;
        uint32_t submittedObjectCount = 0;
        uint32_t submittedSurfaceCount = 0;
        uint32_t selectedPreviewObjectCount = 0;
        uint32_t fallbackObjectCount = 0;
        uint32_t fallbackSurfaceCount = 0;
        uint32_t transparentFallbackSurfaceCount = 0;
        uint32_t unsupportedFallbackSurfaceCount = 0;
        uint32_t cachedPreviewModelCount = 0;
        uint32_t rebuiltPreviewModelCount = 0;
    };

    class ClusteredCpuPreviewRenderer {
    public:
        void SetMode(ClusteredRenderMode mode);
        ClusteredRenderMode GetMode() const;
        void SetEnabled(bool enabled);
        bool IsEnabled() const;
        bool IsCpuReferenceMode() const;

        bool SubmitSelectedObjectPreview(
            const ClusteredGeometryAsset& clusteredGeometry,
            const Transform3D& transform,
            const ModelAsset* sourceModel = nullptr,
            bool receiveShadow = true,
            MESHRENDERER::MeshRenderDebugMode debugMode = MESHRENDERER::MeshRenderDebugMode::Normal,
            const Material* materialOverride = nullptr);

        bool CanSubmitCompleteReference(const ClusteredGeometryAsset& clusteredGeometry) const;
        bool SubmitReferenceObject(
            const ClusteredGeometryAsset& clusteredGeometry,
            const Transform3D& transform,
            const ModelAsset* sourceModel,
            const std::string& materialFxProfileId,
            uint32_t postGroupMask,
            const DirectX::XMFLOAT4(&materialFxParamValues)[VFX::kMaterialFxUserCount],
            bool materialFxValuesInitialized,
            bool receiveShadow = true,
            MESHRENDERER::MeshRenderDebugMode debugMode = MESHRENDERER::MeshRenderDebugMode::Normal,
            const Material* materialOverride = nullptr);

        void RecordReferenceCandidate();
        void RecordFallbackObject(uint32_t surfaceCount = 0);

        void ResetFrameStats();
        void ClearCache();
        const ClusteredCpuPreviewStats& GetStats() const;

    private:
        ModelAsset* GetOrBuildPreviewModel(
            const ClusteredGeometryAsset& clusteredGeometry,
            const ModelAsset* sourceModel);

        ClusteredRenderMode mode_ = ClusteredRenderMode::Off;
        ClusteredCpuPreviewStats stats_{};
        std::unordered_map<const ClusteredGeometryAsset*, std::unique_ptr<ModelAsset>> previewModels_{};
    };

    ClusteredCpuPreviewRenderer& GetClusteredCpuPreviewRenderer();

} // namespace HIKARI::RENDER3D::CLUSTER
