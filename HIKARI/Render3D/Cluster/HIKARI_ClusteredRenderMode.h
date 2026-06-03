#pragma once

namespace HIKARI::RENDER3D::CLUSTER {

    enum class ClusteredRenderMode {
        Off,
        SelectedPreview,
        CpuReference,
    };

    inline const char* ToString(ClusteredRenderMode mode) {
        switch (mode) {
        case ClusteredRenderMode::SelectedPreview:
            return "Selected Preview";
        case ClusteredRenderMode::CpuReference:
            return "CPU Reference";
        case ClusteredRenderMode::Off:
        default:
            return "Off";
        }
    }

} // namespace HIKARI::RENDER3D::CLUSTER
