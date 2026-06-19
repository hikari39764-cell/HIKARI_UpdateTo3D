#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <d3d12.h>

#include "Render3D/GpuDriven/HIKARI_GeometryBackendContext.h"
#include "Render3D/GpuDriven/HIKARI_GpuCommandBuildResult.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenPass.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDrivenTraditionalIndirectView;

    enum class GpuDrivenCommandProducerKind : uint32_t {
        None,
        GpuCommandBuilder,
        GpuSceneRegistry,
        CpuScenePlanner,
        FutureGpuCompute,
    };

    constexpr size_t kGpuDrivenDrawCommandBackendSlotCount = 4u;

    size_t ToDrawCommandBackendSlot(GeometryBackendKind backend);

    struct GpuDrivenDrawCommandRange {
        GpuDrivenPassKind pass = GpuDrivenPassKind::ForwardOpaque;
        GpuDrivenPassKind sourcePass = GpuDrivenPassKind::ForwardOpaque;
        GeometryBackendKind backend = GeometryBackendKind::CpuDirect;
        GpuDrivenCommandProducerKind producer = GpuDrivenCommandProducerKind::None;

        const GpuDrivenCommandPassLayout* gpuCommandLayout = nullptr;
        const GpuDrivenTraditionalIndirectView* traditionalIndirect = nullptr;
        ID3D12Resource* argumentBuffer = nullptr;
        ID3D12CommandSignature* commandSignature = nullptr;

        uint32_t gpuSceneBaseIndex = 0;
        size_t commandCount = 0;
        size_t packetCount = 0;
        size_t instanceCount = 0;
        bool consumable = false;
        bool gpuAuthored = false;

        void Reset();
        bool IsActive() const;
        bool HasTraditionalIndirectView() const;
        bool HasGpuCommandLayout() const;
    };

    struct GpuDrivenPassDrawCommandStream {
        std::array<GpuDrivenDrawCommandRange, kGpuDrivenDrawCommandBackendSlotCount> ranges{};

        void Reset(GpuDrivenPassKind pass);
        GpuDrivenDrawCommandRange& GetOrCreateRange(GeometryBackendKind backend);
        const GpuDrivenDrawCommandRange* FindRange(GeometryBackendKind backend) const;
        GpuDrivenDrawCommandRange* FindRange(GeometryBackendKind backend);
        size_t CountActiveRanges() const;
    };

    struct GpuDrivenDrawCommandStream {
        std::array<GpuDrivenPassDrawCommandStream, kGpuDrivenPassCount> passes{};

        void Reset();
        GpuDrivenPassDrawCommandStream& GetPass(GpuDrivenPassKind pass);
        const GpuDrivenPassDrawCommandStream& GetPass(GpuDrivenPassKind pass) const;
        GpuDrivenDrawCommandRange& SetRange(const GpuDrivenDrawCommandRange& range);
        const GpuDrivenDrawCommandRange* FindRange(
            GpuDrivenPassKind pass,
            GeometryBackendKind backend) const;
        size_t CountActivePasses() const;
        size_t CountActiveRanges() const;
        size_t CountCpuAuthoredCommands() const;
        size_t CountGpuAuthoredCommands() const;
        size_t CountTraditionalIndirectCommands() const;
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
