#pragma once

#include <d3d12.h>

namespace HIKARI::MESHRENDERER {

    struct MaterialTextureHandles {
        int baseColor = -1;
        int normal = -1;
        int emissive = -1;
        int metallicRoughness = -1;
        int occlusion = -1;
    };

    struct MeshBindingContext {
        ID3D12GraphicsCommandList* cmd = nullptr;
        bool depthAwarePhase = false;
        int fallbackTextureHandle = -1;
        int fallbackNormalTextureHandle = -1;
    };

    void BindFrameCommonResources(
        const MeshBindingContext& ctx,
        ID3D12RootSignature* rootSig,
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddress,
        D3D12_GPU_VIRTUAL_ADDRESS lightAddress,
        D3D12_GPU_VIRTUAL_ADDRESS shadowAddress,
        D3D12_GPU_VIRTUAL_ADDRESS skyEnvironmentAddress);

    void BindObjectConstantBuffer(
        const MeshBindingContext& ctx,
        D3D12_GPU_VIRTUAL_ADDRESS objectAddress);

    void BindMaterialTextureSet(
        const MeshBindingContext& ctx,
        const MaterialTextureHandles& textures);

    void BindSkyCube(const MeshBindingContext& ctx);
    void BindSceneDepth(const MeshBindingContext& ctx);
    void BindSceneColor(const MeshBindingContext& ctx);

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveSkyCubeSrv(int fallbackTextureHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE ResolveSceneDepthSrv(bool depthAwarePhase, int fallbackTextureHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE ResolveSceneColorSrv(int fallbackTextureHandle);

} // namespace HIKARI::MESHRENDERER
