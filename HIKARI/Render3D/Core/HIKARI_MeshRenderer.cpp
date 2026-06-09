#include "HIKARI_MeshRenderer.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
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
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPacket.h"
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
            const UINT objectDataBytes = static_cast<UINT>(sizeof(ObjectGpuData) * kMaxObjectCount);
            const UINT materialDataBytes = static_cast<UINT>(sizeof(MaterialGpuData) * kMaxMaterialDataCount);
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

            auto objectDataDesc = CD3DX12_RESOURCE_DESC::Buffer(objectDataBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &objectDataDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.objectDataBuffer.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.objectDataBuffer->Map(0, nullptr, reinterpret_cast<void**>(&g.objectDataMapped)))) {
                return false;
            }

            auto materialDataDesc = CD3DX12_RESOURCE_DESC::Buffer(materialDataBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &materialDataDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.materialDataBuffer.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.materialDataBuffer->Map(0, nullptr, reinterpret_cast<void**>(&g.materialDataMapped)))) {
                return false;
            }

            ID3D12DescriptorHeap* srvHeap = SERVICES::gCtx.srvHeap;
            if (srvHeap == nullptr) {
                return false;
            }
            const UINT descriptorSize =
                device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            const UINT objectDataSrvIndex =
                GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::MeshObjectData);
            g.objectDataSrvCpu =
                GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, objectDataSrvIndex);
            g.objectDataSrvGpu =
                GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, objectDataSrvIndex);
            const UINT materialDataSrvIndex =
                GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::MeshMaterialData);
            g.materialDataSrvCpu =
                GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, materialDataSrvIndex);
            g.materialDataSrvGpu =
                GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, materialDataSrvIndex);

            D3D12_SHADER_RESOURCE_VIEW_DESC objectDataSrv{};
            objectDataSrv.Format = DXGI_FORMAT_UNKNOWN;
            objectDataSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            objectDataSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            objectDataSrv.Buffer.FirstElement = 0;
            objectDataSrv.Buffer.NumElements = kMaxObjectCount;
            objectDataSrv.Buffer.StructureByteStride = sizeof(ObjectGpuData);
            objectDataSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            device->CreateShaderResourceView(g.objectDataBuffer.Get(), &objectDataSrv, g.objectDataSrvCpu);

            D3D12_SHADER_RESOURCE_VIEW_DESC materialDataSrv{};
            materialDataSrv.Format = DXGI_FORMAT_UNKNOWN;
            materialDataSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            materialDataSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            materialDataSrv.Buffer.FirstElement = 0;
            materialDataSrv.Buffer.NumElements = kMaxMaterialDataCount;
            materialDataSrv.Buffer.StructureByteStride = sizeof(MaterialGpuData);
            materialDataSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            device->CreateShaderResourceView(g.materialDataBuffer.Get(), &materialDataSrv, g.materialDataSrvCpu);

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

            // MeshRenderer 共通 fallback は resource handle を正として保持する。
            g.fallbackTextureResource = RENDER3D::LoadTextureResource(
                "mesh_renderer/fallback_white",
                "HIKARI/black1x1.png");
            g.fallbackTextureHandle =
                RENDER3D::GetTextureResourceBackendHandle(g.fallbackTextureResource);
            g.fallbackNormalTextureResource = RENDER3D::LoadTextureResource(
                "mesh_renderer/fallback_normal",
                "HIKARI/normal_flat_1x1.png");
            g.fallbackNormalTextureHandle =
                RENDER3D::GetTextureResourceBackendHandle(g.fallbackNormalTextureResource);
            if (g.fallbackNormalTextureHandle < 0) {
                g.fallbackNormalTextureResource = g.fallbackTextureResource;
                g.fallbackNormalTextureHandle = g.fallbackTextureHandle;
            }
            g.fallbackBlackTextureResource = g.fallbackTextureResource;
            g.fallbackBlackTextureHandle = g.fallbackTextureHandle;
            g.fallbackCubeTextureResource = RENDER3D::CreateSolidColorCubemapResource(
                "mesh_renderer/fallback_cube",
                0x000000ffu,
                RENDER3D::TextureResourceColorSpace::Linear);
            g.fallbackCubeTextureHandle =
                RENDER3D::GetTextureResourceBackendHandle(g.fallbackCubeTextureResource);
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

        bool PrepareMeshFrame(
            const Camera3D& camera,
            const SceneEnvironment& environment,
            uint32_t overrideScreenWidth = 0,
            uint32_t overrideScreenHeight = 0) {
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
            int screenW = static_cast<int>(overrideScreenWidth);
            int screenH = static_cast<int>(overrideScreenHeight);
            if (screenW <= 0 || screenH <= 0) {
                POST::PostSystem::GetSceneCaptureSize(screenW, screenH);
                if (screenW <= 0 || screenH <= 0) {
                    screenW = POST::PostSystem::GetSceneColorWidth();
                    screenH = POST::PostSystem::GetSceneColorHeight();
                }
                if (screenW <= 0 || screenH <= 0) {
                    screenW = std::max(1, SERVICES::gCtx.backBufferWidth);
                    screenH = std::max(1, SERVICES::gCtx.backBufferHeight);
                }
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
            ctx.objectDataBuffer = g.objectDataBuffer.Get();
            ctx.materialDataBuffer = g.materialDataBuffer.Get();
            ctx.jointPaletteCB = g.jointPaletteCB.Get();
            ctx.objectMapped = g.objectMapped;
            ctx.objectDataMapped = g.objectDataMapped;
            ctx.materialDataMapped = g.materialDataMapped;
            ctx.jointPaletteMapped = g.jointPaletteMapped;
            ctx.materialDataTable = &g.materialDataFrameTable;
            ctx.objectDataSrv = g.objectDataSrvGpu;
            ctx.materialDataSrv = g.materialDataSrvGpu;
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
            ctx.binding.stats = &g.debugStats;
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

        enum class SurfacePacketExecutionKind {
            Opaque,
            Transparent,
        };

        bool HasSurfacePacketExecutionPlan(
            const std::vector<uint32_t>* executableIndices,
            const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* executableCommands) {
            return
                g.surfacePacketBuilder != nullptr &&
                executableIndices != nullptr &&
                executableCommands != nullptr &&
                !executableIndices->empty() &&
                !executableCommands->empty();
        }

        bool HasSurfacePacketOpaqueExecutionPlan() {
            return HasSurfacePacketExecutionPlan(
                g.surfacePacketOpaqueExecutionIndices,
                g.surfacePacketOpaqueExecutionCommands);
        }

        bool HasSurfacePacketTransparentExecutionPlan() {
            return HasSurfacePacketExecutionPlan(
                g.surfacePacketTransparentExecutionIndices,
                g.surfacePacketTransparentExecutionCommands);
        }

        bool HasAnySurfacePacketExecutionPlan() {
            return HasSurfacePacketOpaqueExecutionPlan() ||
                HasSurfacePacketTransparentExecutionPlan();
        }

        bool RenderSurfacePacketPlan(
            SurfacePacketExecutionKind executionKind,
            MeshDrawPassKind passKind,
            size_t& objectIndex,
            D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrv,
            int fallbackAoTextureHandle) {
            const std::vector<uint32_t>* executableIndices =
                executionKind == SurfacePacketExecutionKind::Transparent
                    ? g.surfacePacketTransparentExecutionIndices
                    : g.surfacePacketOpaqueExecutionIndices;
            const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* executableCommands =
                executionKind == SurfacePacketExecutionKind::Transparent
                    ? g.surfacePacketTransparentExecutionCommands
                    : g.surfacePacketOpaqueExecutionCommands;
            if (!HasSurfacePacketExecutionPlan(executableIndices, executableCommands)) {
                return true;
            }

            const char* eventName = "SurfacePacketExecutor.ForwardOpaque";
            if (passKind == MeshDrawPassKind::GeometryBuffer) {
                eventName = "SurfacePacketExecutor.GeometryBuffer";
            } else if (executionKind == SurfacePacketExecutionKind::Transparent) {
                eventName = "SurfacePacketExecutor.ForwardTransparent";
            }
            GFX::PIX::ScopedGpuEvent pixPhase(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, eventName);

            MeshBindingStateCache bindingCache{};
            MeshDrawContext drawCtx = BuildDrawContext(false, passKind, ssaoSrv, fallbackAoTextureHandle);
            drawCtx.binding.cache = &bindingCache;
            BindSurfacePacketFrameResources(drawCtx);

            const std::vector<RENDER3D::RUNTIME::SurfaceDrawPacket>& packets =
                g.surfacePacketBuilder->GetPackets();
            for (const RENDER3D::RUNTIME::SurfaceDrawCommand& command :
                *executableCommands) {
                if (passKind == MeshDrawPassKind::GeometryBuffer && command.transparent) {
                    continue;
                }

                const SurfacePacketCommandDrawResult result = DrawSurfacePacketCommand(
                    drawCtx,
                    packets.data(),
                    packets.size(),
                    executableIndices->data(),
                    executableIndices->size(),
                    command,
                    objectIndex);

                ++g.debugStats.surfacePacketExecutorCommandCount;
                if (executionKind == SurfacePacketExecutionKind::Transparent) {
                    ++g.debugStats.surfacePacketExecutorTransparentCommandCount;
                } else {
                    ++g.debugStats.surfacePacketExecutorOpaqueCommandCount;
                }
                if (command.singlePacket) {
                    ++g.debugStats.surfacePacketExecutorSinglePacketCommandCount;
                }
                g.debugStats.surfacePacketExecutorMaxCommandPacketCount =
                    (std::max)(
                        g.debugStats.surfacePacketExecutorMaxCommandPacketCount,
                        static_cast<size_t>(command.packetCount));

                g.debugStats.surfacePacketExecutorPacketCount += result.submittedPacketCount;
                g.debugStats.surfacePacketExecutorSkippedPacketCount += result.skippedPacketCount;
                if (passKind == MeshDrawPassKind::GeometryBuffer) {
                    g.debugStats.surfacePacketExecutorGeometryDrawCount += result.drawCallCount;
                } else {
                    g.debugStats.surfacePacketExecutorForwardDrawCount += result.drawCallCount;
                    if (executionKind == SurfacePacketExecutionKind::Transparent) {
                        g.debugStats.surfacePacketExecutorTransparentDrawCount += result.drawCallCount;
                    } else {
                        g.debugStats.surfacePacketExecutorOpaqueDrawCount += result.drawCallCount;
                    }
                }
                g.debugStats.surfacePacketExecutorInstancedDrawCount += result.instancedDrawCount;
                g.debugStats.surfacePacketExecutorInstancedPacketCount += result.instancedPacketCount;
                g.debugStats.surfacePacketExecutorMaxInstanceCount =
                    (std::max)(
                        g.debugStats.surfacePacketExecutorMaxInstanceCount,
                        result.maxInstanceCount);
            }
            return true;
        }

        bool RenderMeshPhase(
            const RENDER3D::RenderQueue& queue,
            RENDER3D::RenderPhase phase,
            MeshDrawPassKind passKind,
            size_t& objectIndex,
            D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrv,
            int fallbackAoTextureHandle) {
            const bool depthAwarePhase = phase == RENDER3D::RenderPhase::DepthAware;
            const bool transparentPhase = phase == RENDER3D::RenderPhase::Transparent;
            const char* eventName = passKind == MeshDrawPassKind::GeometryBuffer
                ? "MeshRenderer.GeometryBuffer"
                : (depthAwarePhase ? "MeshRenderer.DepthAware" :
                    (transparentPhase ? "MeshRenderer.Transparent" : "MeshRenderer.Opaque"));
            GFX::PIX::ScopedGpuEvent pixPhase(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, eventName);
            MeshBindingStateCache bindingCache{};
            MeshDrawContext drawCtx = BuildDrawContext(depthAwarePhase, passKind, ssaoSrv, fallbackAoTextureHandle);
            drawCtx.binding.cache = &bindingCache;

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
            if (!queue.HasPhase(RENDER3D::RenderPhase::Opaque) && !HasSurfacePacketOpaqueExecutionPlan()) {
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
            const bool packetOk = RenderSurfacePacketPlan(
                SurfacePacketExecutionKind::Opaque,
                MeshDrawPassKind::GeometryBuffer,
                geometryObjectIndex,
                {},
                g.fallbackTextureHandle);
            const bool queueOk = packetOk && RenderMeshPhase(
                queue,
                RENDER3D::RenderPhase::Opaque,
                MeshDrawPassKind::GeometryBuffer,
                geometryObjectIndex,
                {},
                g.fallbackTextureHandle);
            geometryBuffer.EndNormalRoughnessPass(SERVICES::gCtx.cmdList);
            POST::PostSystem::RebindCurrentRenderTarget();
            return packetOk && queueOk;
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

        void SubmitStaticDrawItem(
            const ModelAsset& asset,
            const Transform3D& transform,
            const std::string& materialFxProfileId,
            uint32_t postGroupMask,
            const DirectX::XMFLOAT4(&materialFxParamValues)[VFX::kMaterialFxUserCount],
            bool materialFxValuesInitialized,
            bool receiveShadow,
            MeshRenderDebugMode renderDebugMode,
            const Material* materialOverride,
            bool usePrimitiveFilter,
            uint32_t meshIndex,
            uint32_t primitiveIndex) {

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
            item.usePrimitiveFilter = usePrimitiveFilter;
            item.meshIndexFilter = meshIndex;
            item.primitiveIndexFilter = primitiveIndex;
            item.receiveShadow = receiveShadow;
            item.renderDebugMode = renderDebugMode;
            ResolveDrawVariant(item);
            ++g.debugStats.staticDrawItemCount;
            if (renderDebugMode != MeshRenderDebugMode::Normal) {
                ++g.debugStats.wireDrawItemCount;
            }
            g.drawItems.push_back(std::move(item));
        }
    }

    void Reset() {
        g.drawItems.clear();
        g.renderQueue.Clear();
        g.frameObjectIndex = 0;
        g.materialDataFrameTable.Clear();
        g.surfacePacketBuilder = nullptr;
        g.surfacePacketOpaqueExecutionIndices = nullptr;
        g.surfacePacketOpaqueExecutionCommands = nullptr;
        g.surfacePacketTransparentExecutionIndices = nullptr;
        g.surfacePacketTransparentExecutionCommands = nullptr;
        g.debugStats = {};
    }

    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4(&materialFxParamValues)[VFX::kMaterialFxUserCount], bool materialFxValuesInitialized, bool receiveShadow, MeshRenderDebugMode renderDebugMode, const Material* materialOverride) {
        SubmitStaticDrawItem(
            asset,
            transform,
            materialFxProfileId,
            postGroupMask,
            materialFxParamValues,
            materialFxValuesInitialized,
            receiveShadow,
            renderDebugMode,
            materialOverride,
            false,
            0,
            0);
    }

    void SubmitStaticSubmesh(const ModelAsset& asset, const Transform3D& transform, uint32_t meshIndex, uint32_t primitiveIndex, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4(&materialFxParamValues)[VFX::kMaterialFxUserCount], bool materialFxValuesInitialized, bool receiveShadow, MeshRenderDebugMode renderDebugMode, const Material* materialOverride) {
        SubmitStaticDrawItem(
            asset,
            transform,
            materialFxProfileId,
            postGroupMask,
            materialFxParamValues,
            materialFxValuesInitialized,
            receiveShadow,
            renderDebugMode,
            materialOverride,
            true,
            meshIndex,
            primitiveIndex);
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

    void SetSurfaceDrawPacketExecutionPlans(
        const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder* builder,
        const std::vector<uint32_t>* opaqueExecutablePacketIndices,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* opaqueExecutableCommands,
        const std::vector<uint32_t>* transparentExecutablePacketIndices,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* transparentExecutableCommands) {
        g.surfacePacketBuilder = builder;
        g.surfacePacketOpaqueExecutionIndices = opaqueExecutablePacketIndices;
        g.surfacePacketOpaqueExecutionCommands = opaqueExecutableCommands;
        g.surfacePacketTransparentExecutionIndices = transparentExecutablePacketIndices;
        g.surfacePacketTransparentExecutionCommands = transparentExecutableCommands;
    }

    bool HasSubmittedItems() {
        return !g.drawItems.empty() || HasAnySurfacePacketExecutionPlan();
    }

    bool BeginFrame(const Camera3D& camera, const SceneEnvironment& environment) {
        if (!EnsureInitialized()) {
            return false;
        }
        if (!PrepareMeshFrame(camera, environment)) {
            return false;
        }

        ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
        if (cmd == nullptr ||
            g.objectMapped == nullptr ||
            g.objectCB == nullptr ||
            g.objectDataMapped == nullptr ||
            g.objectDataBuffer == nullptr ||
            g.materialDataMapped == nullptr ||
            g.materialDataBuffer == nullptr) {
            return false;
        }

        g.frameObjectIndex = 0;
        g.materialDataFrameTable.Clear();
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
        if (srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            cmd->SetDescriptorHeaps(1, heaps);
        }

        return true;
    }

    bool BeginFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        uint32_t screenWidth,
        uint32_t screenHeight) {

        if (!EnsureInitialized()) {
            return false;
        }
        // Capture 逕ｨ縺ｮ蝗ｺ螳夊ｧ｣蜒丞ｺｦ繧・camera constants 縺ｫ蜿肴丐縺吶ｋ縲・
        if (!PrepareMeshFrame(camera, environment, screenWidth, screenHeight)) {
            return false;
        }

        ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
        if (cmd == nullptr ||
            g.objectMapped == nullptr ||
            g.objectCB == nullptr ||
            g.objectDataMapped == nullptr ||
            g.objectDataBuffer == nullptr ||
            g.materialDataMapped == nullptr ||
            g.materialDataBuffer == nullptr) {
            return false;
        }

        g.frameObjectIndex = 0;
        g.materialDataFrameTable.Clear();
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
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
        // SurfacePacket は queue を経由せず、先に opaque plan を直接実行する。
        const bool packetOk = RenderSurfacePacketPlan(
            SurfacePacketExecutionKind::Opaque,
            MeshDrawPassKind::Forward,
            g.frameObjectIndex,
            ssaoSrv,
            fallbackAoTextureHandle);
        const bool queueOk = packetOk && RenderMeshPhase(
            queue,
            RENDER3D::RenderPhase::Opaque,
            MeshDrawPassKind::Forward,
            g.frameObjectIndex,
            ssaoSrv,
            fallbackAoTextureHandle);
        return packetOk && queueOk;
    }

    bool RenderForwardTransparentPass(
        const RENDER3D::RenderQueue& queue,
        D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrv,
        int fallbackAoTextureHandle) {
        // Transparent は独立 plan として、opaque/depth-aware の後に実行する。
        const bool packetOk = RenderSurfacePacketPlan(
            SurfacePacketExecutionKind::Transparent,
            MeshDrawPassKind::Forward,
            g.frameObjectIndex,
            ssaoSrv,
            fallbackAoTextureHandle);
        const bool queueOk = packetOk && RenderMeshPhase(
            queue,
            RENDER3D::RenderPhase::Transparent,
            MeshDrawPassKind::Forward,
            g.frameObjectIndex,
            ssaoSrv,
            fallbackAoTextureHandle);
        return packetOk && queueOk;
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
        g.surfacePacketBuilder = nullptr;
        g.surfacePacketOpaqueExecutionIndices = nullptr;
        g.surfacePacketOpaqueExecutionCommands = nullptr;
        g.surfacePacketTransparentExecutionIndices = nullptr;
        g.surfacePacketTransparentExecutionCommands = nullptr;
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
