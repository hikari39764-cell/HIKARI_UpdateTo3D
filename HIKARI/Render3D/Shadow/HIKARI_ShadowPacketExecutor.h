#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <d3d12.h>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI {
    class Mesh;
    class ModelAsset;
    struct MaterialAsset;
    struct MeshPrimitive;
}

namespace HIKARI::RENDER3D::RUNTIME {
    struct SurfaceDrawPacket;
    struct SurfaceDrawCommand;
}

namespace HIKARI::SHADOW::PACKET {

    constexpr UINT kShadowStaticRootParamCamera = 0;
    constexpr UINT kShadowStaticRootParamObject = 1;
    constexpr UINT kShadowStaticRootParamBaseColorTexture = 2;
    constexpr UINT kShadowStaticRootParamObjectData = 3;
    constexpr UINT kShadowStaticRootParamObjectDataControl = 4;
    constexpr UINT kShadowSkinnedRootParamJointPalette = 3;

    struct ShadowPacketObjectData {
        MATH::Mat4 world{};
        uint32_t materialFlags = 0;
        float alphaCutoff = 0.5f;
        float padding[2]{};
    };

    static_assert(sizeof(ShadowPacketObjectData) == 80u);

    using ResolveShadowPacketMeshFn = Mesh* (*)(const MeshPrimitive& primitive);
    using ResolveShadowPacketTextureFn =
        RENDER3D::TextureResourceHandle (*)(const ModelAsset& asset, const MaterialAsset* materialAsset);

    struct ShadowPacketExecutorContext {
        ID3D12GraphicsCommandList* cmd = nullptr;
        ID3D12RootSignature* staticRootSig = nullptr;
        ID3D12PipelineState* staticPso = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddress = 0;
        ResolveShadowPacketMeshFn resolveStaticMesh = nullptr;
        ResolveShadowPacketTextureFn resolveBaseColorTexture = nullptr;
    };

    struct ShadowPacketDrawResult {
        size_t submittedPacketCount = 0;
        size_t skippedPacketCount = 0;
        size_t drawCallCount = 0;
        size_t instancedDrawCount = 0;
        size_t instancedPacketCount = 0;
        size_t maxInstanceCount = 0;
        size_t commandCount = 0;
        size_t singlePacketCommandCount = 0;
        size_t maxCommandPacketCount = 0;
    };

    bool InitializeShadowPacketExecutor(ID3D12Device* device);
    void ResetShadowPacketExecutor();
    D3D12_GPU_DESCRIPTOR_HANDLE GetShadowPacketObjectDataSrv();
    void BindLegacyShadowObjectDataMode(ID3D12GraphicsCommandList* cmd);

    ShadowPacketDrawResult DrawShadowPacketCommands(
        const ShadowPacketExecutorContext& ctx,
        const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
        size_t packetCount,
        const uint32_t* executablePacketIndices,
        size_t executablePacketIndexCount,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>& commands,
        size_t& objectIndex);

} // namespace HIKARI::SHADOW::PACKET
