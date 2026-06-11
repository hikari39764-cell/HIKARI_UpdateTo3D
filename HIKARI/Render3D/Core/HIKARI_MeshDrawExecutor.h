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
    struct SurfaceDrawPacket;
    struct SurfaceDrawCommand;
}

namespace HIKARI::RENDER3D::CORE {
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
        GeometryBuffer,
    };

    using MeshDrawCommandFilter =
        bool (*)(const RENDER3D::RUNTIME::SurfaceDrawCommand& command, const void* userData);

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
        RENDER3D::CORE::SurfaceGpuSceneFrameBuffer* surfaceGpuSceneFrameBuffer = nullptr;
        RENDER3D::CORE::SurfaceIndirectDrawBuffer* surfaceIndirectDrawBuffer = nullptr;
        size_t surfaceGpuSceneBaseOffset = 0;
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS lightAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS shadowAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS skyEnvironmentAddress = 0;
        MeshDrawPassKind passKind = MeshDrawPassKind::Forward;
        MeshBindingContext binding{};
        MeshMaterialFillContext materialFill{};
        MeshDrawServices services{};
        MeshDrawCommandFilter surfaceIndirectCommandFilter = nullptr;
        const void* surfaceIndirectCommandFilterUserData = nullptr;
    };

    bool DrawMeshItem(
        const MeshDrawContext& ctx,
        const DrawItem& item,
        size_t& objectIndex);

    void BindSurfacePacketFrameResources(const MeshDrawContext& ctx);

    bool PrepareSurfacePacketIndirectDrawBindings(
        const MeshDrawContext& ctx,
        const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
        size_t packetCount,
        const uint32_t* executablePacketIndices,
        size_t executablePacketIndexCount,
        const RENDER3D::RUNTIME::SurfaceDrawCommand* commands,
        size_t commandCount);

    bool PrepareSurfacePacketGpuSceneMaterials(
        const MeshDrawContext& ctx,
        const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
        size_t packetCount,
        const uint32_t* executablePacketIndices,
        size_t executablePacketIndexCount,
        const RENDER3D::RUNTIME::SurfaceDrawCommand* commands,
        size_t commandCount);

    struct SurfacePacketCommandDrawResult {
        size_t submittedPacketCount = 0;
        size_t skippedPacketCount = 0;
        size_t drawCallCount = 0;
        size_t instancedDrawCount = 0;
        size_t instancedPacketCount = 0;
        size_t maxInstanceCount = 0;
    };

    SurfacePacketCommandDrawResult DrawSurfacePacketCommandRange(
        const MeshDrawContext& ctx,
        const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
        size_t packetCount,
        const uint32_t* executablePacketIndices,
        size_t executablePacketIndexCount,
        const RENDER3D::RUNTIME::SurfaceDrawCommand* commands,
        size_t commandCount,
        size_t& commandIndex,
        size_t& objectIndex);

    SurfacePacketCommandDrawResult DrawSurfacePacketCommand(
        const MeshDrawContext& ctx,
        const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
        size_t packetCount,
        const uint32_t* executablePacketIndices,
        size_t executablePacketIndexCount,
        const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
        size_t& objectIndex);

} // namespace HIKARI::MESHRENDERER
