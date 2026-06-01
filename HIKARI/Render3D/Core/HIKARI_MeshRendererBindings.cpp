#include "Render3D/Core/HIKARI_MeshRendererBindings.h"

#include "HIKARI_DxTexture.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "HIKARI_Services.h"
#include "Render3D/Core/HIKARI_MeshRendererRootParams.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::MESHRENDERER {
	// フレーム全体で共通のリソースをバインドする。これには、カメラ、ライト、シャドウ、スカイ環境の定数バッファが含まれる。ルートシグネチャも設定される。
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
	// オブジェクト固有の定数バッファをバインドする。これには、モデル行列やマテリアルプロパティなどが含まれる。ルートパラメータのObjectスロットにバインドされる。
    void BindObjectConstantBuffer(
        const MeshBindingContext& ctx,
        D3D12_GPU_VIRTUAL_ADDRESS objectAddress) {
        if (ctx.cmd == nullptr) {
            return;
        }

        ctx.cmd->SetGraphicsRootConstantBufferView(ROOT_PARAM::Object, objectAddress);
    }
	// マテリアルに関連するテクスチャセットをバインドする
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
            normalSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(ctx.fallbackNormalTextureHandle);
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
            emissiveSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(ctx.fallbackBlackTextureHandle);
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
	// スカイキューブマップをバインドする。スカイレンダラーから環境データを取得し、キューブマップSRVが有効な場合はそれを使用する。そうでない場合は、フォールバックテクスチャが使用される。
    void BindSkyCube(const MeshBindingContext& ctx) {
        if (ctx.cmd == nullptr) {
            return;
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE skyCubeSrv = ResolveSkyCubeSrv(ctx.fallbackTextureHandle);
        if (skyCubeSrv.ptr != 0) {
            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::SkyCube, skyCubeSrv);
        }
    }
	// シーンの深度テクスチャをバインドする。深度アウェアな描画フェーズの場合、サービスコンテキストからシーン深度SRVを取得し、利用可能であればそれを使用する。そうでない場合は、フォールバックテクスチャが使用される。
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
    // シーンのカラー（アルベド）テクスチャをバインドする。ポストシステムからシーンカラーSRVを取得し、利用可能であればそれを使用する。そうでない場合は、フォールバックテクスチャが使用される。
    void BindSceneColor(const MeshBindingContext& ctx) {
        if (ctx.cmd == nullptr) {
            return;
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv =
            ResolveSceneColorSrv(ctx.fallbackTextureHandle);
        if (sceneColorSrv.ptr != 0) {
            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::SceneColor, sceneColorSrv);
        }
    }

    void BindIblResources(const MeshBindingContext& ctx) {
        if (ctx.cmd == nullptr) {
            return;
        }
        // IBL resource binding の境界を PIX 上で追えるようにする。
        GFX::PIX::ScopedGpuEvent pixIbl(ctx.cmd, GFX::PIX::kColorRender, "IBL.BindResources");

        const D3D12_GPU_DESCRIPTOR_HANDLE irradianceSrv =
            ResolveIblIrradianceSrv(ctx.fallbackTextureHandle);
        if (irradianceSrv.ptr != 0) {
            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::IblIrradiance, irradianceSrv);
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE prefilteredSrv =
            ResolveIblPrefilteredSrv(ctx.fallbackTextureHandle);
        if (prefilteredSrv.ptr != 0) {
            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::IblPrefiltered, prefilteredSrv);
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE brdfLutSrv =
            ResolveIblBrdfLutSrv(ctx.fallbackTextureHandle);
        if (brdfLutSrv.ptr != 0) {
            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::IblBrdfLut, brdfLutSrv);
        }
    }

    void BindReflectionProbeResources(const MeshBindingContext& ctx) {
        if (ctx.cmd == nullptr) {
            return;
        }
        GFX::PIX::ScopedGpuEvent pixProbe(ctx.cmd, GFX::PIX::kColorRender, "ReflectionProbe.BindResources");

        const D3D12_GPU_DESCRIPTOR_HANDLE prefilteredSrv =
            ResolveReflectionProbePrefilteredSrv(ctx.fallbackCubeTextureHandle);
        if (prefilteredSrv.ptr != 0) {
            ctx.cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::ReflectionProbePrefiltered, prefilteredSrv);
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

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveSceneColorSrv(int fallbackTextureHandle) {
        const D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv = POST::PostSystem::GetSceneColorSrv();
        if (sceneColorSrv.ptr != 0) {
            return sceneColorSrv;
        }

        return DXTEX::DxTextureManager::GetSrvGpuHandle(fallbackTextureHandle);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveIblIrradianceSrv(int fallbackTextureHandle) {
        const D3D12_GPU_DESCRIPTOR_HANDLE srv = IBL::GetIrradianceSrv();
        if (srv.ptr != 0) {
            return srv;
        }

        return DXTEX::DxTextureManager::GetSrvGpuHandle(fallbackTextureHandle);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveIblPrefilteredSrv(int fallbackTextureHandle) {
        const D3D12_GPU_DESCRIPTOR_HANDLE srv = IBL::GetPrefilteredSrv();
        if (srv.ptr != 0) {
            return srv;
        }

        // Sky cubemap を prefiltered IBL の代替として扱わない。
        return DXTEX::DxTextureManager::GetSrvGpuHandle(fallbackTextureHandle);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveIblBrdfLutSrv(int fallbackTextureHandle) {
        const D3D12_GPU_DESCRIPTOR_HANDLE srv = IBL::GetBrdfLutSrv();
        if (srv.ptr != 0) {
            return srv;
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE probeSrv = REFLECTION::GetBrdfLutSrv();
        if (probeSrv.ptr != 0) {
            return probeSrv;
        }

        return DXTEX::DxTextureManager::GetSrvGpuHandle(fallbackTextureHandle);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveReflectionProbePrefilteredSrv(int fallbackCubeTextureHandle) {
        const REFLECTION::ReflectionProbeRuntimeData& probe = REFLECTION::GetActiveProbe();
        if (probe.valid && probe.hasPrefiltered && probe.prefilteredSrv.ptr != 0) {
            return probe.prefilteredSrv;
        }

        return DXTEX::DxTextureManager::GetSrvGpuHandle(fallbackCubeTextureHandle);
    }

} // namespace HIKARI::MESHRENDERER
