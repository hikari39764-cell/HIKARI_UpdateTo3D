#pragma once

#include <cstdint>

#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI::RENDER3D::CLUSTER {

    struct ClusteredGeometryAsset;

    enum class ClusterDebugViewMode {
        Off,
        SelectedObjectSummary,
        SelectedSurfaceBounds,
        FirstNClusterBounds,
        ClusterPageBounds,
        ClusterColorMesh,
        PageColorMesh,
        SurfaceColorMesh,
    };

    struct ClusterDebugOptions {
        ClusterDebugViewMode mode = ClusterDebugViewMode::Off;
        uint32_t selectedSurfaceIndex = 0;
        uint32_t firstClusterLimit = 32;
        uint32_t pageLimit = 16;
    };

    const char* ToString(ClusterDebugViewMode mode);
    bool IsClusterDebugColorMeshMode(ClusterDebugViewMode mode);
    void SubmitClusterDebugOverlay(
        const ClusteredGeometryAsset& asset,
        const Transform3D& transform,
        const ClusterDebugOptions& options);

} // namespace HIKARI::RENDER3D::CLUSTER
