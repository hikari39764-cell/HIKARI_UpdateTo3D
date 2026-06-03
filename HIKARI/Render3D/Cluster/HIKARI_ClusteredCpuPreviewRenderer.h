#pragma once

#include <memory>
#include <unordered_map>

#include "Render3D/Cluster/HIKARI_ClusteredGeometryAsset.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI {
    class Material;
    class ModelAsset;
}

namespace HIKARI::RENDER3D::CLUSTER {

    struct ClusteredCpuPreviewStats {
        bool enabled = false;
        uint32_t submittedObjectCount = 0;
        uint32_t submittedSurfaceCount = 0;
        uint32_t fallbackSurfaceCount = 0;
        uint32_t cachedPreviewModelCount = 0;
        uint32_t rebuiltPreviewModelCount = 0;
    };

    class ClusteredCpuPreviewRenderer {
    public:
        void SetEnabled(bool enabled);
        bool IsEnabled() const;

        bool SubmitSelectedObjectPreview(
            const ClusteredGeometryAsset& clusteredGeometry,
            const Transform3D& transform,
            const ModelAsset* sourceModel = nullptr,
            bool receiveShadow = true,
            MESHRENDERER::MeshRenderDebugMode debugMode = MESHRENDERER::MeshRenderDebugMode::Normal,
            const Material* materialOverride = nullptr);

        void ResetFrameStats();
        void ClearCache();
        const ClusteredCpuPreviewStats& GetStats() const;

    private:
        ModelAsset* GetOrBuildPreviewModel(
            const ClusteredGeometryAsset& clusteredGeometry,
            const ModelAsset* sourceModel);

        bool enabled_ = false;
        ClusteredCpuPreviewStats stats_{};
        std::unordered_map<const ClusteredGeometryAsset*, std::unique_ptr<ModelAsset>> previewModels_{};
    };

    ClusteredCpuPreviewRenderer& GetClusteredCpuPreviewRenderer();

} // namespace HIKARI::RENDER3D::CLUSTER
