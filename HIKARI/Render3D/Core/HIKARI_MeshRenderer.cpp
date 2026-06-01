#include "HIKARI_MeshRenderer.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <d3dx12.h>
#include <wrl/client.h>

#include "HIKARI_DxTexture.h"
#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ResourceStateTracker.h"
#include "HIKARI_Services.h"
#include "Core/HIKARI_TimeService.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshDrawExecutor.h"
#include "Render3D/Core/HIKARI_MeshRendererBindings.h"
#include "Render3D/Core/HIKARI_MeshRendererPso.h"
#include "Render3D/Core/HIKARI_MeshRendererRootParams.h"
#include "Render3D/Core/HIKARI_MeshRendererState.h"
#include "Render3D/Core/HIKARI_MeshRendererUpload.h"
#include "Render3D/Core/HIKARI_MeshVariantResolver.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Render3D/Pipeline/HIKARI_RenderQueue.h"
#include "Render3D/ScreenSpace/HIKARI_SceneGeometryBuffer.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::MESHRENDERER {

    namespace {
        MeshRendererState g;

        bool CreateBuffers(ID3D12Device* device) {
            const UINT cameraBytes = AlignConstantBufferSize(sizeof(CameraCB));
            const UINT objectBytes = AlignConstantBufferSize(sizeof(ObjectCB)) * kMaxObjectCount;
            const UINT lightBytes = AlignConstantBufferSize(sizeof(LightCB));
            const UINT shadowBytes = AlignConstantBufferSize(sizeof(ShadowCB));
            const UINT skyEnvironmentBytes = AlignConstantBufferSize(sizeof(SkyEnvironmentCB));
            const UINT jointPaletteStride = AlignConstantBufferSize(sizeof(JointPaletteCB));
            const UINT jointPaletteBytes = jointPaletteStride * kMaxObjectCount;

            auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto cameraDesc = CD3DX12_RESOURCE_DESC::Buffer(cameraBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &cameraDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.cameraCB.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.cameraCB->Map(0, nullptr, reinterpret_cast<void**>(&g.cameraMapped)))) {
                return false;
            }

            auto objectDesc = CD3DX12_RESOURCE_DESC::Buffer(objectBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &objectDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.objectCB.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.objectCB->Map(0, nullptr, reinterpret_cast<void**>(&g.objectMapped)))) {
                return false;
            }

            auto lightDesc = CD3DX12_RESOURCE_DESC::Buffer(lightBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &lightDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.lightCB.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.lightCB->Map(0, nullptr, reinterpret_cast<void**>(&g.lightMapped)))) {
                return false;
            }

            auto shadowDesc = CD3DX12_RESOURCE_DESC::Buffer(shadowBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &shadowDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.shadowCB.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.shadowCB->Map(0, nullptr, reinterpret_cast<void**>(&g.shadowMapped)))) {
                return false;
            }

            auto skyEnvironmentDesc = CD3DX12_RESOURCE_DESC::Buffer(skyEnvironmentBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &skyEnvironmentDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.skyEnvironmentCB.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.skyEnvironmentCB->Map(0, nullptr, reinterpret_cast<void**>(&g.skyEnvironmentMapped)))) {
                return false;
            }

            auto jointPaletteDesc = CD3DX12_RESOURCE_DESC::Buffer(jointPaletteBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &jointPaletteDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.jointPaletteCB.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.jointPaletteCB->Map(0, nullptr, reinterpret_cast<void**>(&g.jointPaletteMapped)))) {
                return false;
            }

            return true;
        }

        bool EnsureInitialized() {
            if (g.initialized) {
                return true;
            }
            auto* device = SERVICES::gCtx.device;
            if (!device) {
                return false;
            }

            if (!CreateBuffers(device)) {
                return false;
            }
            if (!InitializeMeshPipelines(device, g.pipelines)) {
                return false;
            }

            g.fallbackTextureHandle = DXTEX::DxTextureManager::LoadTexture("mesh_renderer/fallback_white", "HIKARI/black1x1.png");
            g.fallbackNormalTextureHandle = DXTEX::DxTextureManager::LoadTexture("mesh_renderer/fallback_normal", "HIKARI/normal_flat_1x1.png");
            if (g.fallbackNormalTextureHandle < 0) {
                g.fallbackNormalTextureHandle = g.fallbackTextureHandle;
            }
            g.fallbackBlackTextureHandle = g.fallbackTextureHandle;
            g.fallbackCubeTextureHandle = DXTEX::DxTextureManager::CreateSolidColorCubemap(
                "mesh_renderer/fallback_cube",
                0x000000ffu,
                DXTEX::TextureColorSpace::Linear);
            if (g.fallbackCubeTextureHandle < 0) {
                HIKARI_LOG_WARN("[MeshRenderer] fallback cubemap creation failed.");
            }
            MeshMaterialResolverFallbacks fallbacks{};
            fallbacks.whiteTexture = g.fallbackTextureHandle;
            fallbacks.normalTexture = g.fallbackNormalTextureHandle;
            fallbacks.blackTexture = g.fallbackBlackTextureHandle;
            g.materialResolver.SetFallbacks(fallbacks);

            g.initialized = true;
            return true;
        }

        bool PrepareMeshFrame(const Camera3D& camera, const SceneEnvironment& environment) {
            if (g.cameraMapped == nullptr || g.lightMapped == nullptr || g.shadowMapped == nullptr || g.skyEnvironmentMapped == nullptr) {
                return false;
            }

            g.cameraMapped->viewProj = camera.GetViewProj();
            g.cameraMapped->invViewProj = MATH::Inverse(g.cameraMapped->viewProj);
            const MATH::Vec3 cameraPos = camera.GetPosition();
            g.cameraMapped->cameraPos = { cameraPos.x, cameraPos.y, cameraPos.z, 1.0f };
            const FrameContext& frame = TIME::GetFrameContext();
            g.elapsedTimeSec += std::max(0.0f, frame.unscaledDt);
            g.cameraMapped->timeParams = { g.elapsedTimeSec, frame.unscaledDt, frame.gameDt, static_cast<float>(frame.frameIndex) };
            int screenW = POST::PostSystem::GetSceneColorWidth();
            int screenH = POST::PostSystem::GetSceneColorHeight();
            if (screenW <= 0 || screenH <= 0) {
                POST::PostSystem::GetSceneCaptureSize(screenW, screenH);
            }
            if (screenW <= 0 || screenH <= 0) {
                screenW = std::max(1, SERVICES::gCtx.backBufferWidth);
                screenH = std::max(1, SERVICES::gCtx.backBufferHeight);
            }
            g.cameraMapped->screenParams = {
                static_cast<float>(screenW),
                static_cast<float>(screenH),
                1.0f / static_cast<float>(screenW),
                1.0f / static_cast<float>(screenH)
            };

            FillLightCB(environment, *g.lightMapped, g.debugStats);
            FillShadowCB(environment, *g.shadowMapped);
            FillSkyEnvironmentCB(environment, *g.skyEnvironmentMapped);
            return true;
        }

        MeshDrawContext BuildDrawContext(
            bool depthAwarePhase,
            MeshDrawPassKind passKind,
            D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrv,
            int fallbackAoTextureHandle) {
            MeshDrawContext ctx{};
            ctx.cmd = SERVICES::gCtx.cmdList;
            ctx.staticRootSig = GetStaticRootSignature(g.pipelines);
            ctx.skinnedRootSig = GetSkinnedRootSignature(g.pipelines);
            ctx.objectCB = g.objectCB.Get();
            ctx.jointPaletteCB = g.jointPaletteCB.Get();
            ctx.objectMapped = g.objectMapped;
            ctx.jointPaletteMapped = g.jointPaletteMapped;
            ctx.cameraAddress = g.cameraCB ? g.cameraCB->GetGPUVirtualAddress() : 0;
            ctx.lightAddress = g.lightCB ? g.lightCB->GetGPUVirtualAddress() : 0;
            ctx.shadowAddress = g.shadowCB ? g.shadowCB->GetGPUVirtualAddress() : 0;
            ctx.skyEnvironmentAddress = g.skyEnvironmentCB ? g.skyEnvironmentCB->GetGPUVirtualAddress() : 0;
            ctx.passKind = passKind;
            ctx.binding.cmd = ctx.cmd;
            ctx.binding.depthAwarePhase = depthAwarePhase;
            ctx.binding.fallbackTextureHandle = g.fallbackTextureHandle;
            ctx.binding.fallbackNormalTextureHandle = g.fallbackNormalTextureHandle;
            ctx.binding.fallbackBlackTextureHandle = g.fallbackBlackTextureHandle;
            ctx.binding.fallbackCubeTextureHandle = g.fallbackCubeTextureHandle;
            ctx.binding.fallbackAoTextureHandle = fallbackAoTextureHandle >= 0 ? fallbackAoTextureHandle : g.fallbackTextureHandle;
            ctx.binding.ssaoSrv = ssaoSrv;
            ctx.materialFill.fallbackTextureHandle = g.fallbackTextureHandle;
            ctx.materialFill.fallbackNormalTextureHandle = g.fallbackNormalTextureHandle;
            ctx.materialFill.fallbackBlackTextureHandle = g.fallbackBlackTextureHandle;
            ctx.materialFill.stats = &g.debugStats;
            ctx.services.device = SERVICES::gCtx.device;
            ctx.services.primitiveCache = &g.primitiveCache;
            ctx.services.materialResolver = &g.materialResolver;
            ctx.services.pipelines = &g.pipelines;
            ctx.services.stats = &g.debugStats;
            return ctx;
        }

        bool RenderMeshPhase(
            const RENDER3D::RenderQueue& queue,
            RENDER3D::RenderPhase phase,
            MeshDrawPassKind passKind,
            size_t& objectIndex,
            D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrv,
            int fallbackAoTextureHandle) {
            const bool depthAwarePhase = phase == RENDER3D::RenderPhase::DepthAware;
            const char* eventName = passKind == MeshDrawPassKind::GeometryBuffer
                ? "MeshRenderer.GeometryBuffer"
                : (depthAwarePhase ? "MeshRenderer.DepthAware" : "MeshRenderer.Opaque");
            GFX::PIX::ScopedGpuEvent pixPhase(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, eventName);
            const MeshDrawContext drawCtx = BuildDrawContext(depthAwarePhase, passKind, ssaoSrv, fallbackAoTextureHandle);

            for (const DrawItem* item : queue.GetPhase(phase)) {
                if (item == nullptr) {
                    continue;
                }
                if (!DrawMeshItem(drawCtx, *item, objectIndex)) {
                    return false;
                }
            }

            return true;
        }

        bool RenderGeometryBufferPassInternal(
            const RENDER3D::RenderQueue& queue,
            RENDER3D::SCREENSPACE::SceneGeometryBuffer& geometryBuffer) {
            if (!queue.HasPhase(RENDER3D::RenderPhase::Opaque)) {
                return false;
            }

            const uint32_t width = static_cast<uint32_t>(std::max(1.0f, g.cameraMapped ? g.cameraMapped->screenParams.x : 1.0f));
            const uint32_t height = static_cast<uint32_t>(std::max(1.0f, g.cameraMapped ? g.cameraMapped->screenParams.y : 1.0f));
            if (!geometryBuffer.EnsureSize(width, height)) {
                return false;
            }

            D3D12_CPU_DESCRIPTOR_HANDLE depthDsv = POST::PostSystem::GetCurrentRenderTargetDsv();
            if (depthDsv.ptr == 0) {
                return false;
            }

            GFX::PIX::ScopedGpuEvent pixGeometry(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, "SceneGeometryBuffer");
            geometryBuffer.BeginNormalRoughnessPass(SERVICES::gCtx.cmdList, depthDsv);
            size_t geometryObjectIndex = 0;
            const bool ok = RenderMeshPhase(queue, RENDER3D::RenderPhase::Opaque, MeshDrawPassKind::GeometryBuffer, geometryObjectIndex, {}, g.fallbackTextureHandle);
            geometryBuffer.EndNormalRoughnessPass(SERVICES::gCtx.cmdList);
            POST::PostSystem::RebindCurrentRenderTarget();
            return ok;
        }

        struct DepthAwarePhaseScope {
            bool active = false;
            bool postSystem = false;
            D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        };

        bool BeginDepthAwarePhase(DepthAwarePhaseScope& scope) {
            scope = {};

            POST::PostSystem::CaptureSceneColorSnapshot();

            if (POST::PostSystem::BeginCurrentRenderTargetDepthRead()) {
                scope.active = true;
                scope.postSystem = true;
                return true;
            }

            ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
            ID3D12Resource* sceneDepthResource = SERVICES::gCtx.sceneDepthResource;
            if (cmd == nullptr || sceneDepthResource == nullptr) {
                return false;
            }

            if (SERVICES::gCtx.resourceStates != nullptr) {
                SERVICES::gCtx.resourceStates->Transition(
                    cmd,
                    sceneDepthResource,
                    D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
            else {
                auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                    sceneDepthResource,
                    D3D12_RESOURCE_STATE_DEPTH_WRITE,
                    D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                cmd->ResourceBarrier(1, &barrier);
            }

            scope.rtv = SERVICES::gCtx.rtv;
            const D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsv = SERVICES::gCtx.readOnlyDsv;
            cmd->OMSetRenderTargets(1, &scope.rtv, FALSE, &readOnlyDsv);

            scope.active = true;
            return true;
        }

        void RestoreFallbackSceneDepthBinding() {
            ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
            if (cmd == nullptr) {
                return;
            }

            const D3D12_GPU_DESCRIPTOR_HANDLE fallbackSceneDepthSrv =
                ResolveSceneDepthSrv(false, g.fallbackTextureHandle);
            if (fallbackSceneDepthSrv.ptr != 0) {
                cmd->SetGraphicsRootDescriptorTable(ROOT_PARAM::SceneDepth, fallbackSceneDepthSrv);
            }
        }

        void EndDepthAwarePhase(const DepthAwarePhaseScope& scope) {
            if (!scope.active) {
                return;
            }

            RestoreFallbackSceneDepthBinding();

            if (scope.postSystem) {
                POST::PostSystem::EndCurrentRenderTargetDepthRead();
                return;
            }

            ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
            ID3D12Resource* sceneDepthResource = SERVICES::gCtx.sceneDepthResource;
            if (cmd == nullptr || sceneDepthResource == nullptr) {
                return;
            }

            D3D12_CPU_DESCRIPTOR_HANDLE rtv = scope.rtv;
            cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

            if (SERVICES::gCtx.resourceStates != nullptr) {
                SERVICES::gCtx.resourceStates->Transition(
                    cmd,
                    sceneDepthResource,
                    D3D12_RESOURCE_STATE_DEPTH_WRITE);
            }
            else {
                auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                    sceneDepthResource,
                    D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_DEPTH_WRITE);
                cmd->ResourceBarrier(1, &barrier);
            }

            D3D12_CPU_DESCRIPTOR_HANDLE writableDsv = SERVICES::gCtx.dsv;
            cmd->OMSetRenderTargets(1, &rtv, FALSE, &writableDsv);
        }
    }

    void Reset() {
        g.drawItems.clear();
        g.renderQueue.Clear();
        g.frameObjectIndex = 0;
        g.debugStats = {};
    }

    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4(&materialFxParamValues)[VFX::kMaterialFxUserCount], bool materialFxValuesInitialized, bool receiveShadow, MeshRenderDebugMode renderDebugMode, const Material* materialOverride) {
        DrawItem item{};
        item.asset = &asset;
        item.materialOverride = materialOverride;
        item.transform = transform;
        item.materialFxProfileId = materialFxProfileId;
        item.postGroupMask = postGroupMask;
        for (size_t i = 0; i < item.materialFxParamValues.size(); ++i) {
            item.materialFxParamValues[i] = materialFxParamValues[i];
        }
        item.materialFxValuesInitialized = materialFxValuesInitialized;
        item.receiveShadow = receiveShadow;
        item.renderDebugMode = renderDebugMode;
        ResolveDrawVariant(item);
        ++g.debugStats.staticDrawItemCount;
        if (renderDebugMode != MeshRenderDebugMode::Normal) {
            ++g.debugStats.wireDrawItemCount;
        }
        g.drawItems.push_back(std::move(item));
    }

    void SubmitSkinnedMesh(const ModelAsset& asset, const Transform3D& transform, const std::vector<MATH::Mat4>& jointPalette, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4(&materialFxParamValues)[VFX::kMaterialFxUserCount], bool materialFxValuesInitialized, bool receiveShadow, MeshRenderDebugMode renderDebugMode, const Material* materialOverride) {
        DrawItem item{};
        item.asset = &asset;
        item.materialOverride = materialOverride;
        item.transform = transform;
        item.jointPalette = jointPalette;
        item.materialFxProfileId = materialFxProfileId;
        item.postGroupMask = postGroupMask;
        for (size_t i = 0; i < item.materialFxParamValues.size(); ++i) {
            item.materialFxParamValues[i] = materialFxParamValues[i];
        }
        item.materialFxValuesInitialized = materialFxValuesInitialized;
        item.receiveShadow = receiveShadow;
        item.renderDebugMode = renderDebugMode;
        ResolveDrawVariant(item);
        ++g.debugStats.skinnedDrawItemCount;
        if (renderDebugMode != MeshRenderDebugMode::Normal) {
            ++g.debugStats.wireDrawItemCount;
        }
        g.drawItems.push_back(std::move(item));
    }

    bool HasSubmittedItems() {
        return !g.drawItems.empty();
    }

    bool BeginFrame(const Camera3D& camera, const SceneEnvironment& environment) {
        if (!EnsureInitialized()) {
            return false;
        }
        if (!PrepareMeshFrame(camera, environment)) {
            return false;
        }

        ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
        if (cmd == nullptr || g.objectMapped == nullptr || g.objectCB == nullptr) {
            return false;
        }

        g.frameObjectIndex = 0;
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12DescriptorHeap* srvHeap = DXTEX::DxTextureManager::GetSrvHeap();
        if (srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            cmd->SetDescriptorHeaps(1, heaps);
        }

        return true;
    }

    const RENDER3D::RenderQueue& BuildRenderQueue() {
        g.renderQueue.Clear();
        g.renderQueue.Build(g.drawItems);
        return g.renderQueue;
    }

    const CameraCB* GetCameraConstants() {
        return g.cameraMapped;
    }

    bool RenderGeometryBufferPass(
        const RENDER3D::RenderQueue& queue,
        RENDER3D::SCREENSPACE::SceneGeometryBuffer& geometryBuffer) {
        return RenderGeometryBufferPassInternal(queue, geometryBuffer);
    }

    bool RenderForwardOpaquePass(
        const RENDER3D::RenderQueue& queue,
        D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrv,
        int fallbackAoTextureHandle) {
        // MeshRenderer は mesh draw を担当し、screen-space pass の順序は外部で決める。
        return RenderMeshPhase(
            queue,
            RENDER3D::RenderPhase::Opaque,
            MeshDrawPassKind::Forward,
            g.frameObjectIndex,
            ssaoSrv,
            fallbackAoTextureHandle);
    }

    bool RenderDepthAwarePass(
        const RENDER3D::RenderQueue& queue,
        D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrv,
        int fallbackAoTextureHandle) {
        if (!queue.HasPhase(RENDER3D::RenderPhase::DepthAware)) {
            return true;
        }

        DepthAwarePhaseScope depthAwareScope{};
        if (!BeginDepthAwarePhase(depthAwareScope)) {
            return false;
        }

        const bool ok = RenderMeshPhase(
            queue,
            RENDER3D::RenderPhase::DepthAware,
            MeshDrawPassKind::Forward,
            g.frameObjectIndex,
            ssaoSrv,
            fallbackAoTextureHandle);
        EndDepthAwarePhase(depthAwareScope);
        return ok;
    }

    void SetAmbientOcclusionRuntimeEnabled(bool enabled) {
        if (g.skyEnvironmentMapped != nullptr) {
            g.skyEnvironmentMapped->aoParams.x = enabled ? 1.0f : 0.0f;
        }
    }

    void EndFrame() {
        g.drawItems.clear();
        g.renderQueue.Clear();
        g.frameObjectIndex = 0;
    }

    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment) {
        (void)RENDER3D::PIPELINE::RenderMeshLightingFrame(camera, environment);
    }

    const MeshRendererDebugStats& GetDebugStats() {
        const MaterialFxProfileCacheStats fxCacheStats = MaterialFxProfile::GetCacheStats();
        g.debugStats.materialFxProfileCacheHitCount = fxCacheStats.hitCount;
        g.debugStats.materialFxProfileCacheMissCount = fxCacheStats.missCount;
        g.debugStats.materialFxProfileCacheFailCount = fxCacheStats.failCount;
        return g.debugStats;
    }

} // namespace HIKARI::MESHRENDERER
