#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Render3D/Core/HIKARI_MeshMaterialResolver.h"
#include "Render3D/Core/HIKARI_MeshPrimitiveCache.h"
#include "Render3D/Core/HIKARI_MeshRendererPso.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Pipeline/HIKARI_RenderQueue.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::RENDER3D::RUNTIME {
    class SurfaceDrawPacketBuilder;
    struct SurfaceDrawCommand;
}

namespace HIKARI::MESHRENDERER {

    struct MeshRendererState {
        bool initialized = false;

        MeshPipelineStore pipelines;

        Microsoft::WRL::ComPtr<ID3D12Resource> cameraCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> objectCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> objectDataBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> materialDataBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> lightCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> shadowCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> skyEnvironmentCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> jointPaletteCB;

        CameraCB* cameraMapped = nullptr;
        ObjectCB* objectMapped = nullptr;
        ObjectGpuData* objectDataMapped = nullptr;
        MaterialGpuData* materialDataMapped = nullptr;
        LightCB* lightMapped = nullptr;
        ShadowCB* shadowMapped = nullptr;
        SkyEnvironmentCB* skyEnvironmentMapped = nullptr;
        JointPaletteCB* jointPaletteMapped = nullptr;

        std::vector<DrawItem> drawItems;
        MeshRendererDebugStats debugStats;

        RENDER3D::TextureResourceHandle fallbackTextureResource{};
        RENDER3D::TextureResourceHandle fallbackNormalTextureResource{};
        RENDER3D::TextureResourceHandle fallbackBlackTextureResource{};
        RENDER3D::TextureResourceHandle fallbackCubeTextureResource{};

        int fallbackTextureHandle = -1;
        int fallbackNormalTextureHandle = -1;
        int fallbackBlackTextureHandle = -1;
        int fallbackCubeTextureHandle = -1;

        MeshPrimitiveCache primitiveCache;
        MeshMaterialResolver materialResolver;
        RENDER3D::RenderQueue renderQueue;
        size_t frameObjectIndex = 0;
        MaterialDataFrameTable materialDataFrameTable{};
        D3D12_CPU_DESCRIPTOR_HANDLE objectDataSrvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE objectDataSrvGpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE materialDataSrvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE materialDataSrvGpu{};

        const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder* surfacePacketBuilder = nullptr;
        const std::vector<uint32_t>* surfacePacketOpaqueExecutionIndices = nullptr;
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* surfacePacketOpaqueExecutionCommands = nullptr;
        const std::vector<uint32_t>* surfacePacketTransparentExecutionIndices = nullptr;
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* surfacePacketTransparentExecutionCommands = nullptr;

        float elapsedTimeSec = 0.0f;
    };

} // namespace HIKARI::MESHRENDERER
