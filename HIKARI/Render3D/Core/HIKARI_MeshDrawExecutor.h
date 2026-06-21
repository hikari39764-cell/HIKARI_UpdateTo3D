#pragma once

#include <cstddef>
#include <cstdint>

#include <d3d12.h>

#include "Render3D/Core/HIKARI_MeshRendererBindings.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Core/HIKARI_MeshRendererUpload.h"

namespace HIKARI {
    class Mesh;
    struct MaterialAsset;
    struct MeshPrimitive;
}

namespace HIKARI::RENDER3D::RUNTIME {
    struct SurfaceDrawCommand;
    struct SurfaceGpuSceneInstance;
    struct SurfaceGpuSceneMaterialSource;
}

namespace HIKARI::RENDER3D::GPUDRIVEN {
    struct GpuSceneSurfaceRecord;
    class SurfaceGpuSceneFrameBuffer;
    class SurfaceIndirectDrawBuffer;
}

namespace HIKARI::MESHRENDERER {

    class MeshMaterialResolver;
    class MeshPrimitiveCache;
    struct MeshPipelineStore;

    struct MeshDrawServices {
        ID3D12Device* device = nullptr;
        MeshPrimitiveCache* primitiveCache = nullptr;
        MeshMaterialResolver* materialResolver = nullptr;
        MeshPipelineStore* pipelines = nullptr;
        MeshRendererDebugStats* stats = nullptr;
    };

    enum class MeshDrawPassKind {
        Forward,
        GeometryAux,
        DepthPrepass,
    };

    struct MeshDrawContext {
        ID3D12GraphicsCommandList* cmd = nullptr;
        ID3D12RootSignature* staticRootSig = nullptr;
        ID3D12RootSignature* skinnedRootSig = nullptr;
        ID3D12Resource* objectCB = nullptr;
        ID3D12Resource* objectDataBuffer = nullptr;
        ID3D12Resource* materialDataBuffer = nullptr;
        ID3D12Resource* jointPaletteCB = nullptr;
        ObjectCB* objectMapped = nullptr;
        ObjectGpuData* objectDataMapped = nullptr;
        MaterialGpuData* materialDataMapped = nullptr;
        JointPaletteCB* jointPaletteMapped = nullptr;
        MaterialDataFrameTable* materialDataTable = nullptr;
        D3D12_GPU_DESCRIPTOR_HANDLE objectDataSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE materialDataSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrv{};
        RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBuffer* surfaceGpuSceneFrameBuffer = nullptr;
        RENDER3D::GPUDRIVEN::SurfaceIndirectDrawBuffer* surfaceIndirectDrawBuffer = nullptr;
        size_t surfaceGpuSceneBaseOffset = 0;
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS lightAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS shadowAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS skyEnvironmentAddress = 0;
        MeshDrawPassKind passKind = MeshDrawPassKind::Forward;
        MeshBindingContext binding{};
        MeshMaterialFillContext materialFill{};
        MeshDrawServices services{};
    };

    void BindSurfaceRecordFrameResources(const MeshDrawContext& ctx);

    bool PrepareSurfaceRecordGpuSceneMaterials(
        const MeshDrawContext& ctx,
        const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
        size_t recordCount,
        const uint32_t* executableRecordIndices,
        size_t executableRecordIndexCount,
        const RENDER3D::RUNTIME::SurfaceDrawCommand* commands,
        size_t commandCount);

    bool PrepareSurfaceGpuSceneInstanceMaterials(
        const MeshDrawContext& ctx,
        const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
        size_t recordCount,
        const RENDER3D::RUNTIME::SurfaceGpuSceneInstance* instances,
        size_t instanceCount);

    bool PrepareSurfaceGpuSceneMaterialSources(
        const MeshDrawContext& ctx,
        const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* sources,
        size_t sourceCount);

} // namespace HIKARI::MESHRENDERER
