#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <d3d12.h>

#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"

namespace HIKARI::MESHRENDERER {

    class MeshPrimitiveCache;
    struct MeshRendererDebugStats;

    struct MeshRendererTraditionalIndirectHydrationContext {
        ID3D12Device* device = nullptr;
        MeshPrimitiveCache* primitiveCache = nullptr;
        MeshRendererDebugStats* debugStats = nullptr;
        JointPaletteCB* jointPaletteMapped = nullptr;
        ID3D12Resource* jointPaletteBuffer = nullptr;
    };

    // GpuDrivenSceneSource の traditionalIndirect は参照 view なので、
    // MeshRenderer がフレームを跨いで使う command/record/palette の寿命をここで所有する。
    // Mesh の GPU view 復元と joint palette の水合も、この sidecar 境界に閉じ込める。
    class MeshRendererTraditionalIndirectOwner {
    public:
        void Clear();

        void CopyFromSceneSource(
            const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source);
        void AttachToSceneSource(
            RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source) const;
        void RefreshForActivePipeline(
            RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source,
            const MeshRendererTraditionalIndirectHydrationContext& context);

        bool HasSourceCommands(
            const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source) const;
        RENDER3D::GPUDRIVEN::GpuDrivenTraditionalIndirectView GetView(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind) const;

    private:
        struct OwnedStream {
            std::vector<RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord> records{};
            std::vector<uint32_t> executableRecordIndices{};
            std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand> commands{};
            std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance> instances{};
            std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource> materialSources{};
            std::vector<std::vector<MATH::Mat4>> jointPalettes{};
            std::vector<VFX::VariantKey> bucketVariants{};
            uint32_t gpuSceneBaseIndex = 0;
            uint32_t gpuSceneInstanceCount = 0;
            uint32_t staticCommandCount = 0;
            uint32_t skinnedCommandCount = 0;

            void Clear();
            bool CopyFrom(
                const RENDER3D::GPUDRIVEN::GpuDrivenTraditionalIndirectView& view);
            void AttachTo(
                RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass) const;
            RENDER3D::GPUDRIVEN::GpuDrivenTraditionalIndirectView GetView() const;
        };

        void HydrateStream(
            OwnedStream& stream,
            const MeshRendererTraditionalIndirectHydrationContext& context);

        std::array<
            OwnedStream,
            RENDER3D::GPUDRIVEN::kGpuDrivenPassCount>
            streams_{};
    };

} // namespace HIKARI::MESHRENDERER
