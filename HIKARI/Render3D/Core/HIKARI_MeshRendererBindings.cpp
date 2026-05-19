#include "Render3D/Core/HIKARI_MeshRendererBindings.h"

#include "HIKARI_DxTexture.h"
#include "HIKARI_Services.h"
#include "Render3D/Core/HIKARI_MeshRendererRootParams.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"

namespace HIKARI::MESHRENDERER {

    void BindFrameCommonResources(
        const MeshBindingContext& ctx,
        ID3D12RootSignature* rootSig,
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddress,
        D3D12_GPU_VIRTUAL_ADDRESS lightAddress,
        D3D12_GPU_VIRTUAL_ADDRESS shadowAddress,
        D3D12_GPU_VIRTUAL_ADDRESS skyEnvironmentAddress) {
        if (ctx.cmd == nullptr || rootSig == nullptr) {
            return;
        }

        ctx.cmd->SetGraphicsRootSignature(rootSig);
        ctx.cmd->SetGraphicsRootConstantBufferView(ROOT_PARAM::Camera, cameraAddress);
        ctx.cmd->SetGraphicsRootConstantBufferView(ROOT_PARAM::Light, lightAddress);
        ctx.cmd->SetGraphicsRootConstantBufferView(ROOT_PARAM::ShadowCB, shadowAddress);
        ctx.cmd->SetGraphicsRootConstantBufferView(ROOT_PARAM::SkyEnvironment, skyEnvironmentAddress);
    }

    void BindObjectConstantBuffer(
        const MeshBindingContext& ctx,
        D3D12_GPU_VIRTUAL_ADDRESS objectAddress) {
        if (ctx.cmd == nullptr) {
            return;
        }

        ctx.cmd->SetGraphicsRootConstantBufferView(ROOT_PARAM::Object, objectAddress);
    }

    void BindMaterialTextureSet(
        const MeshBindingContext& ctx,
        const MaterialTextureHandles& textures) {
        if (ctx.cmd == nullptr) {
            return;
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE baseColorSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(textures.baseColor);
        if (baseColorSrv.ptr != 0) {
            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::BaseColor, baseColorSrv);
        }

        D3D12_GPU_DESCRIPTOR_HANDLE normalSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(textures.normal);
        if (normalSrv.ptr == 0) {
            normalSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(ctx.fallbackTextureHandle);
        }
        if (normalSrv.ptr != 0) {
            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::Normal, normalSrv);
        }

        D3D12_GPU_DESCRIPTOR_HANDLE shadowSrv = SHADOW::GetDirectionalShadowSrv();
        if (shadowSrv.ptr == 0) {
            shadowSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(ctx.fallbackTextureHandle);
        }
        if (shadowSrv.ptr != 0) {
            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::ShadowMap, shadowSrv);
        }

        D3D12_GPU_DESCRIPTOR_HANDLE emissiveSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(textures.emissive);
        if (emissiveSrv.ptr == 0) {
            emissiveSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(ctx.fallbackTextureHandle);
        }
        if (emissiveSrv.ptr != 0) {
            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::Emissive, emissiveSrv);
        }

        D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(textures.metallicRoughness);
        if (metallicRoughnessSrv.ptr == 0) {
            metallicRoughnessSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(ctx.fallbackTextureHandle);
        }
        if (metallicRoughnessSrv.ptr != 0) {
            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::MetallicRoughness, metallicRoughnessSrv);
        }

        D3D12_GPU_DESCRIPTOR_HANDLE occlusionSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(textures.occlusion);
        if (occlusionSrv.ptr == 0) {
            occlusionSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(ctx.fallbackTextureHandle);
        }
        if (occlusionSrv.ptr != 0) {
            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::Occlusion, occlusionSrv);
        }
    }

    void BindSkyCube(const MeshBindingContext& ctx) {
        if (ctx.cmd == nullptr) {
            return;
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE skyCubeSrv = ResolveSkyCubeSrv(ctx.fallbackTextureHandle);
        if (skyCubeSrv.ptr != 0) {
            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::SkyCube, skyCubeSrv);
        }
    }

    void BindSceneDepth(const MeshBindingContext& ctx) {
        if (ctx.cmd == nullptr) {
            return;
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv =
            ResolveSceneDepthSrv(ctx.depthAwarePhase, ctx.fallbackTextureHandle);
        if (sceneDepthSrv.ptr != 0) {
            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::SceneDepth, sceneDepthSrv);
        }
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveSkyCubeSrv(int fallbackTextureHandle) {
        const SKYRENDERER::SkyEnvironmentData& skyData = SKYRENDERER::GetEnvironmentData();
        if (skyData.valid && skyData.hasCubemap && skyData.cubemapSrv.ptr != 0) {
            return skyData.cubemapSrv;
        }

        return DXTEX::DxTextureManager::GetSrvGpuHandle(fallbackTextureHandle);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveSceneDepthSrv(bool depthAwarePhase, int fallbackTextureHandle) {
        if (depthAwarePhase && SERVICES::gCtx.sceneDepthSrv.ptr != 0) {
            return SERVICES::gCtx.sceneDepthSrv;
        }

        return DXTEX::DxTextureManager::GetSrvGpuHandle(fallbackTextureHandle);
    }

} // namespace HIKARI::MESHRENDERER
