#include "HIKARI_MeshRenderer.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <vector>

#if defined(HIKARI_WITH_EDITOR)
#include <d3dx12.h>
#endif

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_PixProfiler.h"
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
#include "Render3D/GpuDriven/Backend/HIKARI_GeometryBackendPolicy.h"
#include "Render3D/GpuDriven/CommandStream/HIKARI_GpuTraditionalCommandStreamBuffer.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenDrawCommandStream.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenWorkBuilder.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpaceGeometryAux.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::MESHRENDERER {

    namespace {
        MeshRendererState g;
        constexpr size_t kMaxMaterialTextureGpuLoadsPerFrame = 2;

#if defined(HIKARI_WITH_EDITOR)
        struct MeshFrameBindingOverrides {
            D3D12_GPU_VIRTUAL_ADDRESS cameraAddress = 0;
            D3D12_GPU_VIRTUAL_ADDRESS cullingCameraAddress = 0;
            D3D12_GPU_VIRTUAL_ADDRESS lightAddress = 0;
            D3D12_GPU_VIRTUAL_ADDRESS shadowAddress = 0;
            D3D12_GPU_VIRTUAL_ADDRESS skyEnvironmentAddress = 0;
            RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamBuffer*
                traditionalCommandStreamBuffer = nullptr;
        };
#endif

        MeshRendererFrameResources& ActiveFrameResources() {
            return g.frameResources.Active();
        }

        MATH::Mat4 ResolveGpuDrivenCullingViewProj() {
            const MeshRendererFrameResources& frame = ActiveFrameResources();
            return frame.cullingCameraMapped != nullptr
                ? frame.cullingCameraMapped->viewProj
                : MATH::Mat4::Identity();
        }

        MATH::Vec3 ResolveGpuDrivenCullingCameraPosition() {
            const MeshRendererFrameResources& frame = ActiveFrameResources();
            return frame.cullingCameraMapped != nullptr
                ? MATH::Vec3{
                    frame.cullingCameraMapped->cameraPos.x,
                    frame.cullingCameraMapped->cameraPos.y,
                    frame.cullingCameraMapped->cameraPos.z
                }
                : MATH::Vec3{};
        }

        D3D12_GPU_VIRTUAL_ADDRESS ResolveCameraAddressForPass(MeshDrawPassKind passKind) {
            const MeshRendererFrameResources& frame = ActiveFrameResources();
            if (passKind == MeshDrawPassKind::DepthPrepass) {
                return frame.cameraCB != nullptr
                    ? frame.cameraCB->GetGPUVirtualAddress()
                    : 0;
            }
            return frame.cameraCB != nullptr
                ? frame.cameraCB->GetGPUVirtualAddress()
                : 0;
        }

        D3D12_GPU_VIRTUAL_ADDRESS ResolveCullingCameraAddress() {
            const MeshRendererFrameResources& frame = ActiveFrameResources();
            return frame.cullingCameraCB != nullptr
                ? frame.cullingCameraCB->GetGPUVirtualAddress()
                : (frame.cameraCB != nullptr
                    ? frame.cameraCB->GetGPUVirtualAddress()
                    : 0);
        }

        void UpdateMeshletBackendDebugStats();
        void SyncGpuDrivenBackendAvailability();
        void UpdateGpuDrivenWorkReadyDebugStats();
        void UpdateGpuDrivenCommandStreamDebugStats();
        void BuildGpuDrivenFrameState();
        void UpdateGpuDrivenWorkOwnershipDebugStats();
        void BuildGpuDrivenWorkFrame(
            const HIKARI::RENDER3D::DEPTH::DepthPyramidView* depthPyramid = nullptr,
            uint32_t passMask = 0xffffffffu,
            bool collectCounterReadback = true);

        const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& GetSceneSourcePass(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind) {

            return g.gpuDrivenSceneSource.GetPass(passKind);
        }

        bool HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind) {

            return GetSceneSourcePass(passKind).HasGpuSceneRange();
        }

        RENDER3D::GPUDRIVEN::GpuDrivenPassSource& GetMutableSceneSourcePass(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind) {

            return g.gpuDrivenSceneSource.GetPass(passKind);
        }

        MeshRendererTraditionalIndirectHydrationContext
        BuildTraditionalIndirectHydrationContext() {

            MeshRendererTraditionalIndirectHydrationContext context{};
            context.device = SERVICES::gCtx.device;
            context.primitiveCache = &g.primitiveCache;
            context.debugStats = &g.debugStats;
            context.jointPaletteMapped =
                ActiveFrameResources().jointPaletteMapped;
            context.jointPaletteBuffer =
                ActiveFrameResources().jointPaletteCB.Get();
            return context;
        }

        void UploadMeshShaderJointPalettes() {
            MeshRendererFrameResources& frame = ActiveFrameResources();
            const auto* palettes =
                g.gpuDrivenSceneSource.meshShaderJointPalettes;
            if (frame.jointPaletteMapped == nullptr || palettes == nullptr) {
                return;
            }

            const size_t paletteCount =
                (std::min)(palettes->size(), static_cast<size_t>(kMaxObjectCount));
            for (size_t paletteIndex = 0; paletteIndex < paletteCount; ++paletteIndex) {
                const std::vector<MATH::Mat4>& palette = (*palettes)[paletteIndex];
                if (palette.empty()) {
                    continue;
                }
                (void)UploadJointPalette(
                    frame.jointPaletteMapped,
                    paletteIndex,
                    palette);
            }
        }

        D3D12_GPU_DESCRIPTOR_HANDLE ResolveClusterGeometryPoolSrv() {
            D3D12_GPU_DESCRIPTOR_HANDLE handle{};
            ID3D12Device* device = SERVICES::gCtx.device;
            ID3D12DescriptorHeap* heap = RENDER3D::GetTextureResourceSrvHeap();
            if (device == nullptr || heap == nullptr) {
                return handle;
            }

            const UINT descriptorSize =
                device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return GFX::DESCRIPTOR::GpuAt(
                heap,
                descriptorSize,
                GFX::DESCRIPTOR::kSystemSrvDynamicBegin);
        }

        bool CreateBuffers(ID3D12Device* device) {
            ID3D12DescriptorHeap* srvHeap = SERVICES::gCtx.srvHeap;
            if (device == nullptr || srvHeap == nullptr) {
                return false;
            }

            const UINT descriptorSize =
                device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            const UINT surfaceGpuSceneSrvIndex =
                GFX::DESCRIPTOR::ToFrameIndex(
                    GFX::DESCRIPTOR::SystemSrv::MeshSurfaceGpuSceneFrame0,
                    0u);
            const D3D12_CPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrvCpu =
                GFX::DESCRIPTOR::CpuAt(
                    srvHeap,
                    descriptorSize,
                    surfaceGpuSceneSrvIndex);
            const D3D12_GPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrvGpu =
                GFX::DESCRIPTOR::GpuAt(
                    srvHeap,
                    descriptorSize,
                    surfaceGpuSceneSrvIndex);
            if (!g.surfaceGpuSceneBuffer.Initialize(
                    device,
                    surfaceGpuSceneSrvCpu,
                    surfaceGpuSceneSrvGpu,
                    descriptorSize)) {
                DEBUGLOG::PushRenderError(
                    "[MeshRenderer][WARN] SurfaceGpuScene buffer initialization failed. GPU-driven mesh pass will be unavailable.");
            }

            if (!g.frameResources.Initialize(device, srvHeap)) {
                return false;
            }
            g.frameResources.Activate(SERVICES::gCtx.frameIndex);
            return true;
        }

#if defined(HIKARI_WITH_EDITOR)
        template <typename T>
        bool CreateEditorInteractiveConstantBuffer(
            ID3D12Device* device,
            Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
            T*& mapped) {

            if (device == nullptr) {
                return false;
            }
            const UINT byteSize = AlignConstantBufferSize(sizeof(T));
            const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            const auto desc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
            if (FAILED(device->CreateCommittedResource(
                    &heap,
                    D3D12_HEAP_FLAG_NONE,
                    &desc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(resource.ReleaseAndGetAddressOf())))) {
                return false;
            }
            if (FAILED(resource->Map(
                    0,
                    nullptr,
                    reinterpret_cast<void**>(&mapped)))) {
                resource.Reset();
                mapped = nullptr;
                return false;
            }
            return true;
        }

        void ResetEditorInteractiveResources() {
            for (EditorInteractiveMeshFrameResources& frame :
                g.editorInteractive.frames) {

                if (frame.cameraCB != nullptr && frame.cameraMapped != nullptr) {
                    frame.cameraCB->Unmap(0, nullptr);
                }
                if (frame.lightCB != nullptr && frame.lightMapped != nullptr) {
                    frame.lightCB->Unmap(0, nullptr);
                }
                if (frame.shadowCB != nullptr && frame.shadowMapped != nullptr) {
                    frame.shadowCB->Unmap(0, nullptr);
                }
                if (frame.skyEnvironmentCB != nullptr &&
                    frame.skyEnvironmentMapped != nullptr) {
                    frame.skyEnvironmentCB->Unmap(0, nullptr);
                }
                frame = {};
            }
            g.editorInteractive.traditionalCommandStreamBuffer = {};
            g.editorInteractive.gpuDrivenLayer = {};
            g.editorInteractive.initialized = false;
        }

        bool EnsureEditorInteractiveResources() {
            if (g.editorInteractive.initialized) {
                return true;
            }
            ID3D12Device* device = SERVICES::gCtx.device;
            if (device == nullptr ||
                GetStaticRootSignature(g.pipelines) == nullptr ||
                GetSkinnedRootSignature(g.pipelines) == nullptr) {
                return false;
            }

            for (EditorInteractiveMeshFrameResources& frame :
                g.editorInteractive.frames) {

                if (!CreateEditorInteractiveConstantBuffer(
                        device,
                        frame.cameraCB,
                        frame.cameraMapped) ||
                    !CreateEditorInteractiveConstantBuffer(
                        device,
                        frame.lightCB,
                        frame.lightMapped) ||
                    !CreateEditorInteractiveConstantBuffer(
                        device,
                        frame.shadowCB,
                        frame.shadowMapped) ||
                    !CreateEditorInteractiveConstantBuffer(
                        device,
                        frame.skyEnvironmentCB,
                        frame.skyEnvironmentMapped)) {

                    ResetEditorInteractiveResources();
                    return false;
                }
            }

            if (!g.editorInteractive.traditionalCommandStreamBuffer.Initialize(
                    device,
                    GetStaticRootSignature(g.pipelines),
                    ROOT_PARAM::SurfaceGpuSceneControl,
                    4u) ||
                !g.editorInteractive.traditionalCommandStreamBuffer
                    .InitializeSkinnedCommandStream(
                        device,
                        GetSkinnedRootSignature(g.pipelines),
                        ROOT_PARAM::SurfaceGpuSceneControl,
                        ROOT_PARAM::JointPalette)) {

                ResetEditorInteractiveResources();
                return false;
            }

            g.editorInteractive.gpuDrivenLayer.Attach(
                &g.surfaceGpuSceneBuffer,
                &g.editorInteractive.traditionalCommandStreamBuffer,
                nullptr);
            if (!g.editorInteractive.gpuDrivenLayer.Initialize(
                    device,
                    GetStaticRootSignature(g.pipelines),
                    ROOT_PARAM::SurfaceGpuSceneControl,
                    4u)) {

                ResetEditorInteractiveResources();
                return false;
            }
            g.editorInteractive.initialized = true;
            return true;
        }
#endif

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
            if (!g.gpuMaterialRegistry.Initialize(
                    kMaxMaterialDataCount,
                    GFX::kFrameResourceCount)) {
                DEBUGLOG::PushRenderError(
                    "[MeshRenderer][ERROR] GPU material registry initialization failed.");
                return false;
            }
            if (!InitializeMeshPipelines(device, g.pipelines)) {
                return false;
            }
            if (!g.traditionalCommandStreamBuffer.Initialize(
                device,
                GetStaticRootSignature(g.pipelines),
                ROOT_PARAM::SurfaceGpuSceneControl,
                4u)) {
                DEBUGLOG::PushRenderError("[MeshRenderer][WARN] GPU Traditional Command Stream draw buffer initialization failed. GPU-compacted surface stream will be unavailable.");
            } else if (!g.traditionalCommandStreamBuffer.InitializeSkinnedCommandStream(
                device,
                GetSkinnedRootSignature(g.pipelines),
                ROOT_PARAM::SurfaceGpuSceneControl,
                ROOT_PARAM::JointPalette)) {
                DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Surface skinned indirect command signature initialization failed. Skinned GPU-driven indirect stream will be unavailable.");
            }
            if (!g.clusterGpuCullingPass.Initialize(
                device,
                GetStaticRootSignature(g.pipelines),
                ROOT_PARAM::SurfaceGpuSceneControl,
                4u)) {
                DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Cluster GPU culling pass initialization failed. Cluster draw seeds will be disabled.");
            }
            if (!g.meshletRenderBackend.Initialize(
                device,
                GetStaticRootSignature(g.pipelines),
                RENDER3D::MESHLET::MeshletPipelineMask::MainRenderer)) {
                DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Meshlet render backend is not ready. GPU-driven mesh shader route will be unavailable.");
            }
            g.clusterGpuDrivenProducer.Attach(&g.clusterGpuCullingPass);
            g.gpuDrivenLayer.Attach(
                &g.surfaceGpuSceneBuffer,
                &g.traditionalCommandStreamBuffer,
                &g.clusterGpuDrivenProducer);
            if (!g.gpuDrivenLayer.Initialize(
                device,
                GetStaticRootSignature(g.pipelines),
                ROOT_PARAM::SurfaceGpuSceneControl,
                4u)) {
                DEBUGLOG::PushRenderError("[MeshRenderer][WARN] GPU-driven layer initialization failed. GPU-driven draws will be unavailable.");
            }
            UpdateMeshletBackendDebugStats();

            // MeshRenderer 蜈ｱ騾・fallback 縺ｯ resource handle 繧呈ｭ｣縺ｨ縺励※菫晄戟縺吶ｋ縲・
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
            HIKARI_LOG_INFO(
                "[MeshRenderer][Initialize] ready meshlet=" +
                std::string(g.debugStats.meshletBackendPipelineReady ? "yes" : "no"));
            return true;
        }

        MeshDrawContext BuildDrawContext(
            bool depthAwarePhase,
            MeshDrawPassKind passKind,
            const MeshPassResources& passResources
#if defined(HIKARI_WITH_EDITOR)
            , const MeshFrameBindingOverrides* overrides = nullptr
#endif
        );

        void UploadGpuDrivenSceneFrame() {
            g.gpuDrivenLayer.BeginFrame(&g.gpuDrivenSceneSource);

            RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadDesc uploadDesc{};
            uploadDesc.commandList = SERVICES::gCtx.cmdList;
            uploadDesc.residency = &g.gpuDrivenSceneResidency;
            uploadDesc.frameIndex = SERVICES::gCtx.frameIndex;
            const std::span<const uint32_t> materialBindings =
                g.gpuMaterialRegistry.GetSourceBindings();
            uploadDesc.materialSlotBySourceRecord = materialBindings.data();
            uploadDesc.materialSourceRecordCount = materialBindings.size();
            uploadDesc.materialBindingVersion =
                g.gpuMaterialRegistry.GetBindingVersion();
            const RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadStats& uploadStats =
                g.gpuDrivenLayer.UploadSceneFrame(uploadDesc);

            g.debugStats.surfaceGpuSceneOpaqueInstanceCount =
                uploadStats.passInstanceCounts[
                    RENDER3D::GPUDRIVEN::ToPassIndex(
                        RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque)];
            g.debugStats.surfaceGpuSceneDepthPrepassInstanceCount =
                uploadStats.passInstanceCounts[
                    RENDER3D::GPUDRIVEN::ToPassIndex(
                        RENDER3D::GPUDRIVEN::GpuDrivenPassKind::DepthPrepass)];
            g.debugStats.surfaceGpuSceneDepthAwareInstanceCount =
                uploadStats.passInstanceCounts[
                    RENDER3D::GPUDRIVEN::ToPassIndex(
                        RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware)];
            g.debugStats.surfaceGpuSceneTransparentInstanceCount =
                uploadStats.passInstanceCounts[
                    RENDER3D::GPUDRIVEN::ToPassIndex(
                        RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent)];
            g.debugStats.surfaceGpuSceneShadowInstanceCount =
                uploadStats.passInstanceCounts[
                    RENDER3D::GPUDRIVEN::ToPassIndex(
                        RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow)];

            const RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBufferStats& gpuSceneStats =
                uploadStats.bufferStats;
            g.debugStats.surfaceGpuSceneCapacity = gpuSceneStats.capacity;
            g.debugStats.surfaceGpuSceneRequestedInstanceCount = gpuSceneStats.requestedInstanceCount;
            g.debugStats.surfaceGpuSceneUploadedInstanceCount = gpuSceneStats.uploadedInstanceCount;
            g.debugStats.surfaceGpuSceneCommittedInstanceCount = gpuSceneStats.committedInstanceCount;
            g.debugStats.surfaceGpuSceneCommittedBytes = gpuSceneStats.committedBytes;
            g.debugStats.surfaceGpuSceneFullUploadCount =
                uploadStats.uploadedFullScene ? 1u : 0u;
            g.debugStats.surfaceGpuSceneDirtyPatchCount =
                uploadStats.patchedDirtyRanges ? 1u : 0u;
            g.debugStats.surfaceGpuSceneReuseCount =
                uploadStats.reusedResidentFrame ? 1u : 0u;
            g.debugStats.surfaceGpuSceneOverflowInstanceCount = gpuSceneStats.overflowInstanceCount;
            g.debugStats.surfaceGpuSceneUploadCallCount = gpuSceneStats.uploadCallCount;
            g.debugStats.surfaceGpuSceneMaterialPatchCount =
                gpuSceneStats.materialBindingVisitCount;
            g.debugStats.surfaceGpuSceneMaterialPatchChangedCount =
                gpuSceneStats.materialPatchChangedCount;
            g.debugStats.surfaceGpuSceneMaterialPatchUnchangedCount =
                gpuSceneStats.materialPatchUnchangedCount;
            g.debugStats.surfaceGpuSceneMaterialPatchFailCount =
                gpuSceneStats.materialBindingMissingCount;
            g.debugStats.surfaceGpuSceneSrvValid = gpuSceneStats.srv.ptr != 0;
            g.debugStats.surfaceGpuSceneBufferReady = gpuSceneStats.initialized;
        }

        void CommitActiveMaterialDataFrame(ID3D12GraphicsCommandList* commandList) {
            CPU_PROFILE::ScopedCpuTimer cpuTimer(
                CPU_PROFILE::Pass::MaterialPrepare);
            MeshRendererFrameResources& frame = ActiveFrameResources();
            if (!g.gpuMaterialRegistry.StageFrame(
                    g.frameResources.GetActiveFrameIndex(),
                    frame.materialDataMapped,
                    kMaxMaterialDataCount,
                    g.materialUploadRanges)) {
                return;
            }

            g.frameResources.CommitActiveMaterialRanges(
                commandList,
                sizeof(MaterialGpuData),
                g.materialUploadRanges);

            const RENDER3D::MATERIAL::GpuMaterialRegistryStats& stats =
                g.gpuMaterialRegistry.GetStats();
            g.debugStats.materialDataGpuUploadBytes = stats.frameUploadBytes;
            g.debugStats.materialDataGpuUploadCallCount =
                stats.frameUploadRangeCount;
            g.debugStats.materialDataWriteCount = stats.frameUpdatedSlotCount;
            g.debugStats.materialDataCacheHitCount = stats.frameSourceReuseCount;
            g.debugStats.materialDataCacheMissCount =
                stats.frameCreatedSlotCount + stats.frameUpdatedSlotCount;
            g.debugStats.materialDataOverflowCount = stats.frameOverflowCount;
            g.debugStats.materialDataCachedCount = stats.residentSlotCount;
        }

        void SyncSurfaceGpuSceneMaterialFrame() {
            CPU_PROFILE::ScopedCpuTimer cpuTimer(
                CPU_PROFILE::Pass::MaterialPrepare);

            const RENDER3D::MATERIAL::GpuMaterialSourceSyncMode syncMode =
                g.gpuMaterialRegistry.BeginSourceSync(
                    reinterpret_cast<uintptr_t>(g.gpuDrivenSceneSourceIdentity),
                    g.gpuDrivenSceneSource.layoutVersion,
                    g.gpuDrivenSceneSource.sourceVersion,
                    g.gpuDrivenSceneSource.dirtyBaseSourceVersion,
                    g.gpuDrivenSceneSource.sourceRecordCount);
            if (syncMode ==
                RENDER3D::MATERIAL::GpuMaterialSourceSyncMode::None) {
                return;
            }

            MeshDrawContext drawCtx = BuildDrawContext(false, MeshDrawPassKind::Forward, {});
            const auto prepareMaterialSources =
                [&](uint32_t baseIndex,
                    const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>* sources,
                    size_t firstSource,
                    size_t sourceCount) {
                drawCtx.surfaceGpuSceneBaseOffset = baseIndex;
                if (sources != nullptr &&
                    firstSource < sources->size() &&
                    sourceCount != 0u) {
                    const size_t clampedCount =
                        (std::min)(sourceCount, sources->size() - firstSource);
                    PrepareSurfaceGpuSceneMaterialSources(
                        drawCtx,
                        sources->data() + firstSource,
                        clampedCount);
                }
            };

            constexpr std::array<RENDER3D::GPUDRIVEN::GpuDrivenPassKind, 4>
                kMaterialPasses{
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque,
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::DepthPrepass,
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware,
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent,
                };

            if (syncMode ==
                RENDER3D::MATERIAL::GpuMaterialSourceSyncMode::Incremental) {
                for (const RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind :
                    kMaterialPasses) {
                    const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass =
                        GetSceneSourcePass(passKind);
                    for (const RENDER3D::GPUDRIVEN::GpuSceneDirtyRange& range :
                        pass.dirtyRanges) {
                        prepareMaterialSources(
                            pass.gpuSceneBaseIndex,
                            pass.materialSources,
                            range.firstInstance,
                            range.instanceCount);
                    }
                }
            } else {
                std::vector<const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>*>
                    visitedPrimarySources{};
                for (const RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind :
                    kMaterialPasses) {
                    const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass =
                        GetSceneSourcePass(passKind);
                    if (pass.materialSources != nullptr &&
                        std::find(
                            visitedPrimarySources.begin(),
                            visitedPrimarySources.end(),
                            pass.materialSources) == visitedPrimarySources.end()) {
                        visitedPrimarySources.push_back(pass.materialSources);
                        prepareMaterialSources(
                            pass.gpuSceneBaseIndex,
                            pass.materialSources,
                            0u,
                            pass.materialSources->size());
                    }
                    prepareMaterialSources(
                        pass.traditionalIndirect.gpuSceneBaseIndex,
                        pass.traditionalIndirect.materialSources,
                        0u,
                        pass.traditionalIndirect.materialSources != nullptr
                            ? pass.traditionalIndirect.materialSources->size()
                            : 0u);
                }
            }

            g.gpuMaterialRegistry.EndSourceSync();
        }

        void UpdateTraditionalCommandStreamStats() {
            const RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamStats& indirectStats =
                g.gpuDrivenLayer.GetCommandFrameStats().traditionalCommandStreamStats;
            g.debugStats.traditionalCommandStreamCommandCapacity = indirectStats.capacity;
            g.debugStats.traditionalCommandStreamRequestedCommandCount = indirectStats.requestedCommandCount;
            g.debugStats.traditionalCommandStreamUploadedCommandCount = indirectStats.uploadedCommandCount;
            g.debugStats.traditionalCommandStreamOverflowCommandCount = indirectStats.overflowCommandCount;
            g.debugStats.traditionalCommandStreamMissingDrawArgsCommandCount = indirectStats.missingDrawArgsCommandCount;
            g.debugStats.traditionalCommandStreamUploadCallCount = indirectStats.uploadCallCount;
            g.debugStats.traditionalCommandStreamInputUploadBytes =
                indirectStats.inputUploadBytes;
            g.debugStats.traditionalCommandStreamInputUploadCopyCount =
                indirectStats.inputUploadCopyCount;
            g.debugStats.traditionalCommandStreamResidentInputReuseCount =
                indirectStats.reusedResidentInput ? 1u : 0u;
            g.debugStats.traditionalCommandStreamCommandStride = indirectStats.commandStride;
            g.debugStats.traditionalCommandStreamArgumentBufferReady = indirectStats.initialized;
            g.debugStats.traditionalCommandStreamCommandSignatureReady = indirectStats.commandSignatureReady;
        }

        void UpdateGpuDrivenWorklistDebugStats() {
            g.debugStats.gpuDrivenWorklistPassCount =
                g.gpuDrivenFrame.CountActivePasses();
            g.debugStats.gpuDrivenWorklistClusterPassCount =
                g.gpuDrivenFrame.CountClusterEligiblePasses();
            g.debugStats.gpuDrivenWorklistSourceInstanceCount =
                g.gpuDrivenFrame.CountSourceInstances();
            g.debugStats.gpuDrivenWorklistClusterInstanceCount =
                g.gpuDrivenFrame.CountClusterEligibleInstances();
        }

        uint32_t MakePreDepthGpuDrivenPassMask() {
            using RENDER3D::GPUDRIVEN::GpuDrivenPassKind;
            using RENDER3D::GPUDRIVEN::MakeGpuDrivenPassMask;
            // DepthPrepass は HZB 構築前に描くため、occlusion なし (frustum のみ)
            // の BeginFrame ビルドで work を確定させる。
            // Shadow はここに含めない: shadow map は ShadowMapRenderer が光源
            // 行列で専用の cull チェーンを回すため、主カメラでの Shadow cull は
            // 消費者が存在しない。
            return MakeGpuDrivenPassMask(GpuDrivenPassKind::DepthPrepass);
        }

        uint32_t MakeMainCameraGpuDrivenPassMask() {
            using RENDER3D::GPUDRIVEN::GpuDrivenPassKind;
            using RENDER3D::GPUDRIVEN::MakeGpuDrivenPassMask;
            // DepthPrepass も finalize で再ビルドする。visibility dispatch は
            // frameContext を丸ごと差し替えるため、ここに含めないと BeginFrame
            // で作った prepass work が finalize 後に消える。
            return
                MakeGpuDrivenPassMask(GpuDrivenPassKind::ForwardOpaque) |
                MakeGpuDrivenPassMask(GpuDrivenPassKind::ForwardDepthAware) |
                MakeGpuDrivenPassMask(GpuDrivenPassKind::ForwardTransparent) |
                MakeGpuDrivenPassMask(GpuDrivenPassKind::GeometryAux) |
                MakeGpuDrivenPassMask(GpuDrivenPassKind::DepthPrepass);
        }

        void BuildStrictGpuDrivenCommandFrame() {
            const MATH::Mat4 viewProj = ResolveGpuDrivenCullingViewProj();
            RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDesc{};
            commandFrameDesc.commandList = SERVICES::gCtx.cmdList;
            commandFrameDesc.cullViewProj = &viewProj;
            commandFrameDesc.frameIndex = SERVICES::gCtx.frameIndex;
            g.gpuDrivenLayer.BuildCommandFrame(commandFrameDesc);
            UpdateTraditionalCommandStreamStats();
            UpdateGpuDrivenCommandStreamDebugStats();
        }

        void BuildGpuDrivenFrameState() {
            const RENDER3D::GPUDRIVEN::GpuDrivenFrameBuildInput input =
                RENDER3D::GPUDRIVEN::BuildGpuDrivenFrameInput(
                    g.gpuDrivenSceneSource);
            g.gpuDrivenFrame =
                RENDER3D::GPUDRIVEN::BuildGpuDrivenFrame(input);
            UpdateGpuDrivenWorklistDebugStats();
        }

        void ResetGpuDrivenFrameState() {
            UploadGpuDrivenSceneFrame();
            g.gpuDrivenFrame.Reset();
            UpdateGpuDrivenWorklistDebugStats();
            g.clusterGpuDrivenProducer.BeginFrame(false);
            g.gpuDrivenLayer.ImportProducerOutput(
                g.clusterGpuDrivenProducer.BuildFrameOutput());
            g.meshletRenderBackend.ResetFrame();
            UpdateMeshletBackendDebugStats();
            UpdateGpuDrivenWorkReadyDebugStats();
            UpdateGpuDrivenWorkOwnershipDebugStats();
            RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDesc{};
            commandFrameDesc.commandList = SERVICES::gCtx.cmdList;
            commandFrameDesc.frameIndex = SERVICES::gCtx.frameIndex;
            g.gpuDrivenLayer.BuildCommandFrame(commandFrameDesc);
            UpdateTraditionalCommandStreamStats();
            UpdateGpuDrivenCommandStreamDebugStats();
        }

        void PrepareGpuDrivenFrameState() {
            CPU_PROFILE::ScopedCpuTimer cpuTimer(
                CPU_PROFILE::Pass::MeshPrepare);
            UploadGpuDrivenSceneFrame();
            if (!g.gpuDrivenSceneResidency.resident) {
                g.gpuDrivenFrame.Reset();
                UpdateGpuDrivenWorklistDebugStats();
                g.clusterGpuDrivenProducer.BeginFrame(false);
                g.gpuDrivenLayer.ImportProducerOutput(
                    g.clusterGpuDrivenProducer.BuildFrameOutput());
                g.meshletRenderBackend.ResetFrame();
                UpdateMeshletBackendDebugStats();
                UpdateGpuDrivenWorkReadyDebugStats();
                UpdateGpuDrivenWorkOwnershipDebugStats();
                RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDesc{};
                commandFrameDesc.commandList = SERVICES::gCtx.cmdList;
                commandFrameDesc.frameIndex = SERVICES::gCtx.frameIndex;
                g.gpuDrivenLayer.BuildCommandFrame(commandFrameDesc);
                UpdateTraditionalCommandStreamStats();
                UpdateGpuDrivenCommandStreamDebugStats();
                return;
            }
            BuildGpuDrivenFrameState();
            BuildGpuDrivenWorkFrame(
                nullptr,
                MakePreDepthGpuDrivenPassMask(),
                false);
            g.meshletRenderBackend.ResetFrame();
            UpdateMeshletBackendDebugStats();
            UpdateGpuDrivenWorkReadyDebugStats();
            UpdateGpuDrivenWorkOwnershipDebugStats();
            BuildStrictGpuDrivenCommandFrame();
        }

        void BuildGpuDrivenWorkFrame(
            const HIKARI::RENDER3D::DEPTH::DepthPyramidView* depthPyramid,
            uint32_t passMask,
            bool collectCounterReadback) {
            const MATH::Mat4 viewProj = ResolveGpuDrivenCullingViewProj();
            const MATH::Vec3 cameraPosition = ResolveGpuDrivenCullingCameraPosition();
            ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
            if (SERVICES::gCtx.cmdList != nullptr && srvHeap != nullptr) {
                ID3D12DescriptorHeap* heaps[] = { srvHeap };
                SERVICES::gCtx.cmdList->SetDescriptorHeaps(1, heaps);
            }
            RENDER3D::GPUDRIVEN::GpuDrivenWorkContext workContext{};
            workContext.producer = &g.clusterGpuDrivenProducer;
            workContext.commandList = SERVICES::gCtx.cmdList;
            workContext.viewProj = viewProj;
            workContext.cameraPosition = cameraPosition;
            workContext.geometryPoolSrv = ResolveClusterGeometryPoolSrv();
            workContext.surfaceGpuSceneGpuAddress =
                g.surfaceGpuSceneBuffer.GetGpuVirtualAddress();
            workContext.frame = &g.gpuDrivenFrame;
            workContext.passMask = passMask;
            workContext.collectCounterReadback = collectCounterReadback;
            if (depthPyramid != nullptr &&
                depthPyramid->valid &&
                depthPyramid->pyramidSrv.ptr != 0 &&
                depthPyramid->width != 0 &&
                depthPyramid->height != 0 &&
                depthPyramid->viewProjValid) {

                workContext.depthOcclusion.enabled = true;
                workContext.depthOcclusion.hzbSrv = depthPyramid->pyramidSrv;
                workContext.depthOcclusion.hzbWidth = depthPyramid->width;
                workContext.depthOcclusion.hzbHeight = depthPyramid->height;
                workContext.depthOcclusion.hzbMipCount =
                    std::max(1u, depthPyramid->mipCount);
                workContext.depthOcclusion.hzbViewProj = depthPyramid->viewProj;
                workContext.depthOcclusion.hzbViewProjValid = true;
                workContext.depthOcclusion.depthPyramid = *depthPyramid;
            }
            (void)RENDER3D::GPUDRIVEN::BuildGpuDrivenWork(workContext);

            const RENDER3D::CLUSTER::ClusterGpuCullingPassStats* clusterCullStatsPtr =
                g.clusterGpuDrivenProducer.GetClusterStats();
            const RENDER3D::CLUSTER::ClusterGpuCullingPassStats fallbackClusterCullStats{};
            const RENDER3D::CLUSTER::ClusterGpuCullingPassStats& clusterCullStats =
                clusterCullStatsPtr != nullptr
                    ? *clusterCullStatsPtr
                    : fallbackClusterCullStats;
            g.gpuDrivenLayer.ImportProducerOutput(
                g.clusterGpuDrivenProducer.BuildFrameOutput());
            g.gpuDrivenLayer.BuildCommandBuffers();
            UpdateGpuDrivenCommandStreamDebugStats();
            g.debugStats.clusterGpuCullReady =
                clusterCullStats.initialized &&
                clusterCullStats.psoReady &&
                clusterCullStats.inputBufferReady &&
                clusterCullStats.visibleRangeBufferReady &&
                clusterCullStats.counterBufferReady;
            g.debugStats.clusterGpuCullSourceInstanceCount =
                clusterCullStats.sourceInstanceCount;
            g.debugStats.clusterGpuCullCandidateInstanceCount =
                clusterCullStats.candidateInstanceCount;
            g.debugStats.clusterGpuCullSubmittedInstanceCount =
                clusterCullStats.submittedInstanceCount;
            g.debugStats.clusterGpuCullSourcePageTaskCount =
                clusterCullStats.sourcePageTaskCount;
            g.debugStats.clusterGpuCullSubmittedPageTaskCount =
                clusterCullStats.submittedPageTaskCount;
            g.debugStats.clusterGpuCullDrawSeedCount =
                clusterCullStats.submittedDrawSeedCount;
            g.debugStats.clusterGpuCullOverflowInstanceCount =
                clusterCullStats.overflowInstanceCount;
            g.debugStats.clusterGpuCullCounterReadbackReady =
                clusterCullStats.gpuCounterReadbackReady;
            g.debugStats.clusterGpuCullCounterReadbackValid =
                clusterCullStats.gpuCounterReadbackValid;
            g.debugStats.clusterGpuCullDebugCountersEnabled =
                clusterCullStats.debugCountersEnabled;
            g.debugStats.clusterGpuCullFineCullingOwnedByAmplificationShader =
                clusterCullStats.meshletFineCullingDeferredToAmplificationShader;
            g.debugStats.clusterGpuCullOcclusionHistoryReady =
                clusterCullStats.occlusionHistoryReady;
            g.debugStats.clusterGpuCullGpuInputCount =
                clusterCullStats.gpuInputCount;
            g.debugStats.clusterGpuCullGpuPageTaskCount =
                clusterCullStats.gpuPageTaskCount;
            g.debugStats.clusterGpuCullGpuPageTaskOverflowCount =
                clusterCullStats.gpuPageTaskOverflowCount;
            g.debugStats.clusterGpuCullGpuVisibleRangeCount =
                clusterCullStats.gpuVisibleRangeCount;
            g.debugStats.clusterGpuCullGpuVisibleClusterCount =
                clusterCullStats.gpuVisibleClusterCount;
            g.debugStats.clusterGpuCullGpuOverflowCount =
                clusterCullStats.gpuOverflowCount;
            g.debugStats.clusterGpuCullHzbOcclusionEnabled =
                clusterCullStats.hzbOcclusionEnabled;
            g.debugStats.clusterGpuCullHzbOcclusionWidth =
                clusterCullStats.hzbOcclusionWidth;
            g.debugStats.clusterGpuCullHzbOcclusionHeight =
                clusterCullStats.hzbOcclusionHeight;
            g.debugStats.clusterGpuCullHzbOcclusionMipCount =
                clusterCullStats.hzbOcclusionMipCount;
            g.debugStats.clusterGpuCullGpuInputFrustumCulledCount =
                clusterCullStats.gpuInputFrustumCulledCount;
            g.debugStats.clusterGpuCullGpuPageTestedCount =
                clusterCullStats.gpuPageTestedCount;
            g.debugStats.clusterGpuCullGpuPageFrustumCulledCount =
                clusterCullStats.gpuPageFrustumCulledCount;
            g.debugStats.clusterGpuCullGpuPageOcclusionTestedCount =
                clusterCullStats.gpuPageOcclusionTestedCount;
            g.debugStats.clusterGpuCullGpuPageOcclusionCulledCount =
                clusterCullStats.gpuPageOcclusionCulledCount;
            g.debugStats.clusterGpuCullGpuClusterTestedCount =
                clusterCullStats.gpuClusterTestedCount;
            g.debugStats.clusterGpuCullGpuClusterFrustumCulledCount =
                clusterCullStats.gpuClusterFrustumCulledCount;
            g.debugStats.clusterGpuCullGpuClusterOcclusionTestedCount =
                clusterCullStats.gpuClusterOcclusionTestedCount;
            g.debugStats.clusterGpuCullGpuClusterOcclusionCulledCount =
                clusterCullStats.gpuClusterOcclusionCulledCount;
            g.debugStats.clusterGpuCullGpuHzbPassRejectedCount =
                clusterCullStats.gpuHzbPassRejectedCount;
            g.debugStats.clusterGpuCullGpuHzbAabbRejectedCount =
                clusterCullStats.gpuHzbAabbRejectedCount;
            g.debugStats.clusterGpuCullGpuHzbSphereRejectedCount =
                clusterCullStats.gpuHzbSphereRejectedCount;
            g.debugStats.clusterGpuCullGpuHzbQueryAcceptedCount =
                clusterCullStats.gpuHzbQueryAcceptedCount;
            g.debugStats.clusterGpuCullGpuHzbTryCount =
                clusterCullStats.gpuHzbTryCount;
            g.debugStats.clusterGpuCullGpuHzbAllowedCount =
                clusterCullStats.gpuHzbAllowedCount;
            g.debugStats.clusterGpuCullGpuHzbInvalidRejectedCount =
                clusterCullStats.gpuHzbInvalidRejectedCount;
            g.debugStats.clusterGpuCullGpuHzbNearPlaneRejectedCount =
                clusterCullStats.gpuHzbNearPlaneRejectedCount;
            g.debugStats.clusterGpuCullGpuHzbOffscreenRejectedCount =
                clusterCullStats.gpuHzbOffscreenRejectedCount;
            g.debugStats.clusterGpuCullGpuHzbLargeRectCount =
                clusterCullStats.gpuHzbLargeRectCount;
            g.debugStats.clusterGpuCullGpuHzbAabbAcceptedCount =
                clusterCullStats.gpuHzbAabbAcceptedCount;
            g.debugStats.clusterGpuCullGpuHzbSphereAcceptedCount =
                clusterCullStats.gpuHzbSphereAcceptedCount;
            g.debugStats.clusterGpuCullGpuHzbRawOccludedCount =
                clusterCullStats.gpuHzbRawOccludedCount;
            g.debugStats.clusterGpuCullGpuHzbTemporalPendingCount =
                clusterCullStats.gpuHzbTemporalPendingCount;
            g.debugStats.clusterGpuCullGpuHzbTemporalConfirmedCount =
                clusterCullStats.gpuHzbTemporalConfirmedCount;
            g.debugStats.clusterGpuCullGpuHzbTemporalResetCount =
                clusterCullStats.gpuHzbTemporalResetCount;
            g.debugStats.clusterGpuCullGpuHzbTemporalCollisionCount =
                clusterCullStats.gpuHzbTemporalCollisionCount;
            g.debugStats.clusterGpuCullGpuHzbLargeRectSkippedCount =
                clusterCullStats.gpuHzbLargeRectSkippedCount;
            g.debugStats.clusterGpuCullGpuPageHzbSmallScreenSkippedCount =
                clusterCullStats.gpuPageHzbSmallScreenSkippedCount;
            g.debugStats.clusterGpuCullGpuClusterHzbSmallScreenSkippedCount =
                clusterCullStats.gpuClusterHzbSmallScreenSkippedCount;
            g.debugStats.clusterGpuCullGpuConeSkippedDoubleSidedCount =
                clusterCullStats.gpuConeSkippedDoubleSidedCount;
            g.debugStats.clusterGpuCullGpuConeSkippedMaterialCount =
                clusterCullStats.gpuConeSkippedMaterialCount;
            g.debugStats.clusterGpuCullGpuClusterHzbLargeScreenSkippedCount =
                clusterCullStats.gpuClusterHzbLargeScreenSkippedCount;
            g.debugStats.clusterGpuCullGpuHzbBudgetSkippedCount =
                clusterCullStats.gpuHzbBudgetSkippedCount;
            g.debugStats.clusterGpuCullGpuClusterConeCulledCount =
                clusterCullStats.gpuClusterConeCulledCount;
            g.debugStats.clusterGpuCullGpuClusterConeTestedCount =
                clusterCullStats.gpuClusterConeTestedCount;
            g.debugStats.clusterGpuCullGpuDoubleSidedClusterCount =
                clusterCullStats.gpuDoubleSidedClusterCount;
            g.debugStats.clusterGpuCullGpuDrawCommandCount =
                clusterCullStats.gpuDrawCommandCount;
            g.debugStats.clusterGpuCullGpuBackFaceDrawCommandCount =
                clusterCullStats.gpuBackFaceDrawCommandCount;
            g.debugStats.clusterGpuCullGpuDoubleSidedDrawCommandCount =
                clusterCullStats.gpuDoubleSidedDrawCommandCount;
            g.debugStats.clusterGpuCullGpuDrawCommandOverflowCount =
                clusterCullStats.gpuDrawCommandOverflowCount;
            g.debugStats.clusterGpuCullGpuBackFaceDrawCommandOverflowCount =
                clusterCullStats.gpuBackFaceDrawCommandOverflowCount;
            g.debugStats.clusterGpuCullGpuDoubleSidedDrawCommandOverflowCount =
                clusterCullStats.gpuDoubleSidedDrawCommandOverflowCount;
            g.debugStats.clusterGpuCullGpuMergedGapCount =
                clusterCullStats.gpuMergedGapCount;
            g.debugStats.clusterGpuCullGpuMergedGapIndexCount =
                clusterCullStats.gpuMergedGapIndexCount;
            g.debugStats.clusterGpuCullGpuPacketRangeCount =
                clusterCullStats.gpuPacketRangeCount;
            g.debugStats.clusterGpuCullGpuPacketClusterCount =
                clusterCullStats.gpuPacketClusterCount;
            g.debugStats.clusterGpuCullGpuVisibleClusterListReservedCount =
                clusterCullStats.gpuVisibleClusterListReservedCount;
            g.debugStats.clusterGpuCullGpuVisibleClusterListOverflowCount =
                clusterCullStats.gpuVisibleClusterListOverflowCount;
            g.debugStats.clusterGpuCullGpuLod0SelectedCount =
                clusterCullStats.gpuLod0SelectedCount;
            g.debugStats.clusterGpuCullGpuLod1SelectedCount =
                clusterCullStats.gpuLod1SelectedCount;
            g.debugStats.clusterGpuCullGpuLod2SelectedCount =
                clusterCullStats.gpuLod2SelectedCount;
            g.debugStats.clusterGpuCullGpuLod3PlusSelectedCount =
                clusterCullStats.gpuLod3PlusSelectedCount;
            g.debugStats.clusterGpuCullLodTargetErrorNdc =
                clusterCullStats.lodTargetErrorNdc;
            g.debugStats.clusterGpuCullLodTransitionRelaxPerLevel =
                clusterCullStats.lodTransitionRelaxPerLevel;
            g.debugStats.clusterGpuCullLodErrorRelaxPerLevel =
                clusterCullStats.lodErrorRelaxPerLevel;
            g.debugStats.clusterGpuCullGpuCulledInstanceCount =
                clusterCullStats.gpuInputFrustumCulledCount;
            g.debugStats.clusterGpuCullDispatchCount =
                clusterCullStats.dispatchCount;
            g.debugStats.clusterGpuCullWorkgroupCount =
                clusterCullStats.workgroupCount;
            g.debugStats.clusterGpuCullInputCapacity =
                clusterCullStats.inputCapacity;
            g.debugStats.clusterGpuCullVisibleRangeCapacity =
                clusterCullStats.visibleRangeCapacity;
            g.debugStats.clusterGpuCullCandidateCommandCapacity =
                clusterCullStats.candidateCommandCapacity;
            g.debugStats.clusterGpuCullOcclusionHistoryCapacity =
                clusterCullStats.occlusionHistoryCapacity;
        }
        void UpdateMeshletBackendDebugStats() {
            const RENDER3D::MESHLET::MeshletRenderBackendStats& meshletStats =
                g.meshletRenderBackend.GetStats();
            const RENDER3D::GPUDRIVEN::GpuCommandBuildResult& gpuDrivenCommands =
                g.gpuDrivenLayer.GetFrameContext().commands;
            g.debugStats.meshletBackendInitialized =
                meshletStats.initialized;
            g.debugStats.meshletBackendShaderModel65Supported =
                meshletStats.shaderModel65Supported;
            g.debugStats.meshletBackendMeshShaderSupported =
                meshletStats.meshShaderSupported;
            g.debugStats.meshletBackendPipelineStatsSupported =
                meshletStats.meshShaderPipelineStatsSupported;
            g.debugStats.meshletBackendShaderCompileReady =
                meshletStats.shaderCompileReady;
            g.debugStats.meshletBackendDispatchArgumentBufferReady =
                meshletStats.dispatchArgumentBufferReady ||
                gpuDrivenCommands.meshDispatchArgs != nullptr;
            g.debugStats.meshletBackendDispatchCommandSignatureReady =
                meshletStats.dispatchCommandSignatureReady ||
                gpuDrivenCommands.meshDispatchSignature != nullptr;
            g.debugStats.meshletBackendForwardPipelineReady =
                meshletStats.forwardPipelineReady;
            g.debugStats.meshletBackendGeometryAuxPipelineReady =
                meshletStats.geometryAuxPipelineReady;
            g.debugStats.meshletBackendPipelineReady =
                meshletStats.pipelineReady;
            g.debugStats.meshletBackendMeshShaderTier =
                meshletStats.meshShaderTier;
            g.debugStats.meshletBackendRequestedDispatchCount =
                meshletStats.requestedDispatchCount;
            g.debugStats.meshletBackendSubmittedDispatchCount =
                meshletStats.submittedDispatchCount;
            g.debugStats.meshletBackendSkippedDispatchCount =
                meshletStats.skippedDispatchCount;
            g.debugStats.meshletBackendSubmitCallCount =
                meshletStats.submitCallCount;
            g.debugStats.meshletBackendSkippedBucketCount =
                meshletStats.skippedBucketCount;
            g.debugStats.meshletBackendForwardSubmittedDispatchCount =
                meshletStats.forwardSubmittedDispatchCount;
            g.debugStats.meshletBackendGeometryAuxSubmittedDispatchCount =
                meshletStats.geometryAuxSubmittedDispatchCount;
            g.debugStats.meshletBackendDepthPrepassSubmittedDispatchCount =
                meshletStats.depthPrepassSubmittedDispatchCount;
            g.debugStats.meshletBackendShadowSubmittedDispatchCount =
                meshletStats.shadowSubmittedDispatchCount;
            g.debugStats.meshletBackendBackFaceSubmitCallCount =
                meshletStats.backFaceSubmitCallCount;
            g.debugStats.meshletBackendDoubleSidedSubmitCallCount =
                meshletStats.doubleSidedSubmitCallCount;
            g.debugStats.meshletBackendPipelineCreateRequestCount =
                meshletStats.pipelineCreateRequestCount;
            g.debugStats.meshletBackendPipelineCreateReadyCount =
                meshletStats.pipelineCreateReadyCount;
        }

        void SyncGpuDrivenBackendAvailability() {
            RENDER3D::GPUDRIVEN::GpuDrivenBackendAvailability availability{};
            const RENDER3D::MESHLET::MeshletRenderBackendStats& meshletStats =
                g.meshletRenderBackend.GetStats();

            availability.meshShaderForwardPipelineReady =
                meshletStats.forwardPipelineReady &&
                meshletStats.depthAwarePipelineReady &&
                meshletStats.transparentPipelineReady;
            availability.meshShaderGeometryAuxPipelineReady =
                meshletStats.geometryAuxPipelineReady;
            availability.traditionalIndirectPipelineReady =
                GetStaticRootSignature(g.pipelines) != nullptr &&
                GetSkinnedRootSignature(g.pipelines) != nullptr &&
                g.pipelines.pso != nullptr &&
                g.pipelines.skinnedPso != nullptr &&
                g.pipelines.depthPso != nullptr &&
                g.pipelines.depthSkinnedPso != nullptr;
            g.gpuDrivenLayer.SetBackendAvailability(availability);
        }

        void UpdateGpuDrivenWorkReadyDebugStats() {
            SyncGpuDrivenBackendAvailability();
            const RENDER3D::GPUDRIVEN::GpuDrivenPassExecutionState& forward =
                g.gpuDrivenLayer.GetPassExecutionState(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque);
            const RENDER3D::GPUDRIVEN::GpuDrivenPassExecutionState& geometry =
                g.gpuDrivenLayer.GetPassExecutionState(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::GeometryAux);
            g.debugStats.clusterMainlineReady = forward.gpuBackendReady;
            g.debugStats.clusterMainlineForwardReady = forward.gpuBackendReady;
            g.debugStats.clusterMainlineGeometryAuxReady = geometry.gpuBackendReady;
            g.debugStats.clusterMainlineHasDrawSeeds =
                forward.hasDrawSeeds ||
                geometry.hasDrawSeeds;
            g.debugStats.clusterMainlineOverflowBlocked =
                forward.overflowBlocked ||
                geometry.overflowBlocked;
            UpdateGpuDrivenCommandStreamDebugStats();
        }

        void UpdateGpuDrivenCommandStreamDebugStats() {
            const RENDER3D::GPUDRIVEN::GpuDrivenDrawCommandStream& stream =
                g.gpuDrivenLayer.GetDrawCommandStream();
            g.debugStats.gpuDrivenCommandStreamPassCount =
                stream.CountActivePasses();
            g.debugStats.gpuDrivenCommandStreamRangeCount =
                stream.CountActiveRanges();
            g.debugStats.gpuDrivenCommandStreamGpuCommandCount =
                stream.CountGpuAuthoredCommands();
            g.debugStats.gpuDrivenCommandStreamTraditionalCommandCount =
                stream.CountTraditionalIndirectCommands();
            g.debugStats.gpuDrivenCommandStreamGpuCounterBackedRangeCount =
                stream.CountGpuCounterBackedRanges();
            g.debugStats.gpuDrivenCommandStreamKnownVisibleCommandCount =
                stream.CountKnownGpuVisibleCommands();
            g.debugStats.gpuDrivenCommandStreamKnownVisibleCommandOverflowCount =
                stream.CountKnownGpuVisibleCommandOverflows();
        }

        void ApplyGpuDrivenWorkOwnershipDebugStats(
            const RENDER3D::GPUDRIVEN::GpuDrivenWorkOwnershipStats& stats) {

            g.debugStats.clusterMainlineOwnedCommandCount = stats.ownedCommandCount;
            g.debugStats.clusterMainlineOwnedRecordCount = stats.ownedRecordCount;
            g.debugStats.clusterMainlineGeometryAuxCommandCount = stats.geometryAuxCommandCount;
            g.debugStats.clusterMainlineGeometryAuxRecordCount = stats.geometryAuxRecordCount;
        }

        void UpdateGpuDrivenWorkOwnershipDebugStats() {
            SyncGpuDrivenBackendAvailability();
            RENDER3D::GPUDRIVEN::GpuDrivenWorkOwnershipStats stats{};
            const RENDER3D::GPUDRIVEN::GpuDrivenPassExecutionState& forward =
                g.gpuDrivenLayer.GetPassExecutionState(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque);
            const RENDER3D::GPUDRIVEN::GpuDrivenPassExecutionState& geometry =
                g.gpuDrivenLayer.GetPassExecutionState(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::GeometryAux);
            if (forward.gpuBackendReady) {
                stats.ownedCommandCount = forward.sourceInstanceCount;
                stats.ownedRecordCount = forward.sourceInstanceCount;
                stats.eligibleCommandCount = stats.ownedCommandCount;
            }
            if (geometry.gpuBackendReady) {
                stats.geometryAuxCommandCount = geometry.sourceInstanceCount;
                stats.geometryAuxRecordCount = geometry.sourceInstanceCount;
            }
            ApplyGpuDrivenWorkOwnershipDebugStats(stats);
        }

        bool IsGpuDrivenWorkPreparedForPass(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass) {

            SyncGpuDrivenBackendAvailability();
            return g.gpuDrivenLayer.IsPassGpuReady(pass);
        }

        struct GeometryBackendExecutionResult {
            bool gpuBackendExecuted = false;
            RENDER3D::GPUDRIVEN::GeometryBackendKind executedGpuBackend =
                RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVsPs;
        };
        bool ExecuteMeshletDrawFrame(
            const MeshPassResources& passResources,
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
            MeshDrawPassKind passKind,
            RENDER3D::MESHLET::MeshletPipelineKind pipelineKind) {
            if (!IsGpuDrivenWorkPreparedForPass(gpuPass)) {
                return false;
            }

            const RENDER3D::GPUDRIVEN::GeometryBackendContext backendContext =
                g.gpuDrivenLayer.BuildGeometryBackendContext(
                    SERVICES::gCtx.cmdList,
                    gpuPass,
                    RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader);
            ID3D12Resource* visibleRangeBuffer =
                backendContext.visibility != nullptr
                    ? backendContext.visibility->visibleMeshletRangeBuffer
                    : nullptr;
            ID3D12Resource* visibleClusterListBuffer =
                backendContext.visibility != nullptr
                    ? backendContext.visibility->visibleMeshletClusterListBuffer
                    : nullptr;
            if (visibleRangeBuffer == nullptr ||
                visibleClusterListBuffer == nullptr) {
                return false;
            }

            MeshBindingStateCache bindingCache{};
            const bool depthAwarePhase =
                gpuPass == RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware;
            MeshDrawContext drawCtx = BuildDrawContext(depthAwarePhase, passKind, passResources);
            drawCtx.binding.cache = &bindingCache;
            BindSurfaceRecordFrameResources(drawCtx);
            BindMeshletVisibleRanges(
                drawCtx.binding,
                visibleRangeBuffer->GetGPUVirtualAddress());
            BindMeshletVisibleClusterList(
                drawCtx.binding,
                visibleClusterListBuffer->GetGPUVirtualAddress());
            const MeshRendererFrameResources& frame = ActiveFrameResources();
            BindMeshletDeformationPalettes(
                drawCtx.binding,
                frame.jointPaletteCB != nullptr
                    ? frame.jointPaletteCB->GetGPUVirtualAddress()
                    : 0u);

            RENDER3D::MESHLET::MeshletRenderExecutionContext ctx{};
            ctx.commandList = backendContext.commandList;
            ctx.pass = backendContext.pass;
            ctx.visibility = backendContext.visibility;
            ctx.drawCommandRange = backendContext.drawCommandRange;
            ctx.pipelineKind = pipelineKind;
            const bool executed = g.meshletRenderBackend.Execute(ctx);
            UpdateMeshletBackendDebugStats();
            UpdateGpuDrivenWorkReadyDebugStats();
            return executed;
        }

        ID3D12PipelineState* ResolveTraditionalStaticPso(
            MeshDrawPassKind passKind,
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
            const VFX::VariantKey& bucketVariant) {

            (void)gpuPass;

            if (passKind == MeshDrawPassKind::GeometryAux) {
                return g.pipelines.geometryPso.Get();
            }
            if (passKind == MeshDrawPassKind::DepthPrepass) {
                return g.pipelines.depthPso.Get();
            }
            if (passKind != MeshDrawPassKind::Forward ||
                SERVICES::gCtx.device == nullptr) {
                return g.pipelines.pso.Get();
            }

            return GetOrCreateVariantPso(
                g.pipelines,
                SERVICES::gCtx.device,
                g.debugStats,
                bucketVariant,
                false,
                false);
        }

        ID3D12PipelineState* ResolveTraditionalSkinnedPso(
            MeshDrawPassKind passKind,
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
            const VFX::VariantKey& bucketVariant) {

            (void)gpuPass;

            if (!bucketVariant.vertexShaderId.empty() &&
                bucketVariant.vertexShaderId != "Render3D_StaticVS") {
                return nullptr;
            }
            if (passKind == MeshDrawPassKind::GeometryAux) {
                return g.pipelines.geometrySkinnedPso.Get();
            }
            if (passKind == MeshDrawPassKind::DepthPrepass) {
                return g.pipelines.depthSkinnedPso.Get();
            }
            if (passKind != MeshDrawPassKind::Forward ||
                SERVICES::gCtx.device == nullptr) {
                return g.pipelines.skinnedPso.Get();
            }

            VFX::VariantKey key = bucketVariant;
            key.vertexShaderId.clear();
            return GetOrCreateVariantPso(
                g.pipelines,
                SERVICES::gCtx.device,
                g.debugStats,
                key,
                true,
                false);
        }

        bool ExecuteTraditionalDrawFrame(
            const MeshPassResources& passResources,
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
            MeshDrawPassKind passKind
#if defined(HIKARI_WITH_EDITOR)
            ,
            RENDER3D::GPUDRIVEN::GpuDrivenLayer* layerOverride = nullptr,
            const MeshFrameBindingOverrides* bindingOverrides = nullptr
#endif
        ) {

#if defined(HIKARI_WITH_EDITOR)
            RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer =
                layerOverride != nullptr ? *layerOverride : g.gpuDrivenLayer;
            if (layerOverride == nullptr) {
                SyncGpuDrivenBackendAvailability();
            }
            if (!layer.IsPassGpuReady(gpuPass)) {
                return false;
            }
#else
            if (!IsGpuDrivenWorkPreparedForPass(gpuPass)) {
                return false;
            }
#endif

            const RENDER3D::GPUDRIVEN::GeometryBackendContext backendContext =
#if defined(HIKARI_WITH_EDITOR)
                layer.BuildGeometryBackendContext(
#else
                g.gpuDrivenLayer.BuildGeometryBackendContext(
#endif
                    SERVICES::gCtx.cmdList,
                    gpuPass,
                    RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVsPs);
            const RENDER3D::GPUDRIVEN::GpuDrivenDrawCommandRange* range =
                backendContext.drawCommandRange;
            if (range == nullptr ||
                !range->gpuAuthored ||
                !range->gpuCounterBacked ||
                range->counterBuffer == nullptr ||
                range->commandCount == 0) {
                return false;
            }
            const size_t commandBucketCapacity = range->commandBucketCapacity;
            const size_t commandBucketCount = range->commandBucketCount;
            const bool staticBucketLayoutReady =
                commandBucketCapacity != 0 &&
                commandBucketCount != 0 &&
                range->argumentBucketStride != 0 &&
                range->counterBucketStride != 0;
            const bool skinnedBucketLayoutReady =
                commandBucketCapacity != 0 &&
                commandBucketCount != 0 &&
                range->skinnedArgumentBucketStride != 0 &&
                range->counterBucketStride != 0;
            const bool hasStaticStream =
                range->argumentBuffer != nullptr &&
                range->commandSignature != nullptr &&
                staticBucketLayoutReady &&
                range->staticCommandCount != 0;
            const bool hasSkinnedStream =
                range->skinnedArgumentBuffer != nullptr &&
                range->skinnedCommandSignature != nullptr &&
                skinnedBucketLayoutReady &&
                range->skinnedCommandCount != 0;
            if (!hasStaticStream && !hasSkinnedStream) {
                return false;
            }
            const std::vector<VFX::VariantKey>* bucketVariants =
                range->traditionalIndirect != nullptr
                    ? range->traditionalIndirect->bucketVariants
                    : nullptr;
            if (bucketVariants == nullptr || bucketVariants->empty()) {
                return false;
            }
            const size_t executableBucketCount =
                (std::min)(commandBucketCount, bucketVariants->size());
            if (executableBucketCount == 0) {
                return false;
            }

            MeshBindingStateCache bindingCache{};
            const bool depthAwarePhase =
                gpuPass == RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware;
            MeshDrawContext drawCtx =
                BuildDrawContext(
                    depthAwarePhase,
                    passKind,
                    passResources
#if defined(HIKARI_WITH_EDITOR)
                    ,
                    bindingOverrides);
#else
                );
#endif
            drawCtx.binding.cache = &bindingCache;
            drawCtx.surfaceGpuSceneBaseOffset = range->gpuSceneBaseIndex;
            BindSurfaceRecordFrameResources(drawCtx);
            BindSurfaceGpuSceneBuffer(drawCtx.binding, drawCtx.surfaceGpuSceneSrv);
            BindObjectDataIndex(drawCtx.binding, 0u);
            BindMaterialDataIndex(drawCtx.binding, 0u);
            const size_t uintMaxCommandCount =
                static_cast<size_t>((std::numeric_limits<UINT>::max)());
            const size_t staticCommandLimit =
                (std::min)(range->staticCommandCount, commandBucketCapacity);
            const size_t skinnedCommandLimit =
                (std::min)(range->skinnedCommandCount, commandBucketCapacity);
            const UINT maxStaticCommandCount = static_cast<UINT>(
                (std::min)(staticCommandLimit, uintMaxCommandCount));
            const UINT maxSkinnedCommandCount = static_cast<UINT>(
                (std::min)(skinnedCommandLimit, uintMaxCommandCount));
            bool executed = false;
            if (hasStaticStream) {
                for (size_t bucketIndex = 0; bucketIndex < executableBucketCount; ++bucketIndex) {
                    const VFX::VariantKey& bucketVariant =
                        (*bucketVariants)[bucketIndex];
                    ID3D12PipelineState* bucketPso =
                        ResolveTraditionalStaticPso(
                            passKind,
                            gpuPass,
                            bucketVariant);
                    if (bucketPso == nullptr) {
                        continue;
                    }
                    BindPipelineState(drawCtx.binding, bucketPso);
                    SERVICES::gCtx.cmdList->ExecuteIndirect(
                        range->commandSignature,
                        maxStaticCommandCount,
                        range->argumentBuffer,
                        range->argumentBufferOffset +
                            static_cast<UINT64>(bucketIndex) *
                            range->argumentBucketStride,
                        range->counterBuffer,
                        range->counterBufferOffset +
                            static_cast<UINT64>(bucketIndex) *
                            range->counterBucketStride);
                    executed = true;
                }
            }

            if (hasSkinnedStream &&
                drawCtx.skinnedRootSig != nullptr) {
                BindFrameCommonResources(
                    drawCtx.binding,
                    drawCtx.skinnedRootSig,
                    drawCtx.cameraAddress,
                    drawCtx.cullingCameraAddress,
                    drawCtx.lightAddress,
                    drawCtx.shadowAddress,
                    drawCtx.skyEnvironmentAddress);
                BindObjectDataBuffer(drawCtx.binding, drawCtx.objectDataSrv);
                BindMaterialDataBuffer(drawCtx.binding, drawCtx.materialDataSrv);
                BindSurfaceGpuSceneBuffer(drawCtx.binding, drawCtx.surfaceGpuSceneSrv);
                BindSurfaceGpuSceneControl(drawCtx.binding, 0u, false);
                BindShadowMap(drawCtx.binding);
                if (passKind == MeshDrawPassKind::Forward) {
                    BindSkyCube(drawCtx.binding);
                    BindSceneDepth(drawCtx.binding);
                    BindSceneColor(drawCtx.binding);
                    BindIblResources(drawCtx.binding);
                    BindReflectionProbeResources(drawCtx.binding);
                    BindSsao(drawCtx.binding);
                    BindLightProbeResources(drawCtx.binding);
                }
                BindObjectDataIndex(drawCtx.binding, 0u);
                BindMaterialDataIndex(drawCtx.binding, 0u);
                for (size_t bucketIndex = 0; bucketIndex < executableBucketCount; ++bucketIndex) {
                    const VFX::VariantKey& bucketVariant =
                        (*bucketVariants)[bucketIndex];
                    ID3D12PipelineState* bucketPso =
                        ResolveTraditionalSkinnedPso(
                            passKind,
                            gpuPass,
                            bucketVariant);
                    if (bucketPso == nullptr) {
                        continue;
                    }
                    BindPipelineState(drawCtx.binding, bucketPso);
                    SERVICES::gCtx.cmdList->ExecuteIndirect(
                        range->skinnedCommandSignature,
                        maxSkinnedCommandCount,
                        range->skinnedArgumentBuffer,
                        range->skinnedArgumentBufferOffset +
                            static_cast<UINT64>(bucketIndex) *
                            range->skinnedArgumentBucketStride,
                        range->counterBuffer,
                        range->skinnedCounterBufferOffset +
                            static_cast<UINT64>(bucketIndex) *
                            range->counterBucketStride);
                    executed = true;
                }
            }

            g.debugStats.gpuDrivenSkinnedCommandCount +=
                range->skinnedCommandCount;
            g.debugStats.gpuDrivenSkinnedSourceRecordCount +=
                range->skinnedCommandCount;
            return executed;
        }

        bool ExecuteGeometryBackend(
            RENDER3D::GPUDRIVEN::GeometryBackendKind backend,
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
            const MeshPassResources& passResources,
            MeshDrawPassKind passKind,
            RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind) {

            switch (backend) {
            case RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader:
                return ExecuteMeshletDrawFrame(
                    passResources,
                    gpuPass,
                    passKind,
                    meshletPipelineKind);
            case RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVsPs:
                return ExecuteTraditionalDrawFrame(
                    passResources,
                    gpuPass,
                    passKind);
            default:
                return false;
            }
        }

        GeometryBackendExecutionResult ExecuteGeometryBackendPlan(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass,
            const MeshPassResources& passResources,
            MeshDrawPassKind passKind,
            RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind) {

            GeometryBackendExecutionResult result{};
            SyncGpuDrivenBackendAvailability();
            const RENDER3D::GPUDRIVEN::GeometryBackendExecutionPlan plan =
                g.gpuDrivenLayer.GetPassExecutionPlan(pass);
            for (size_t i = 0; i < plan.gpuBackendCount; ++i) {
                const RENDER3D::GPUDRIVEN::GeometryBackendKind backend =
                    plan.gpuBackends[i];
                if (!ExecuteGeometryBackend(
                    backend,
                    pass,
                    passResources,
                    passKind,
                    meshletPipelineKind)) {
                    continue;
                }
                result.gpuBackendExecuted = true;
                result.executedGpuBackend = backend;
            }
            return result;
        }

        GeometryBackendExecutionResult ExecuteDepthVisibilityBackendPlan(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass,
            const MeshPassResources& passResources) {

            GeometryBackendExecutionResult result{};
            SyncGpuDrivenBackendAvailability();
            const RENDER3D::GPUDRIVEN::GeometryBackendExecutionPlan plan =
                g.gpuDrivenLayer.GetPassExecutionPlan(pass);
            for (size_t i = 0; i < plan.gpuBackendCount; ++i) {
                const RENDER3D::GPUDRIVEN::GeometryBackendKind backend =
                    plan.gpuBackends[i];
                if (!ExecuteGeometryBackend(
                    backend,
                    pass,
                    passResources,
                    MeshDrawPassKind::DepthPrepass,
                    RENDER3D::MESHLET::MeshletPipelineKind::DepthPrepass)) {
                    continue;
                }
                result.gpuBackendExecuted = true;
                result.executedGpuBackend = backend;
            }
            return result;
        }

        bool PrepareMeshFrame(
            const Camera3D& camera,
            const SceneEnvironment& environment,
            RenderDebugView debugView,
            uint32_t overrideScreenWidth = 0,
            uint32_t overrideScreenHeight = 0,
            const MeshFrameCameraOverrides* cameraOverrides = nullptr) {
            MeshRendererFrameResources& frameResources = ActiveFrameResources();
            if (frameResources.cameraMapped == nullptr ||
                frameResources.cullingCameraMapped == nullptr ||
                frameResources.lightMapped == nullptr ||
                frameResources.shadowMapped == nullptr ||
                frameResources.skyEnvironmentMapped == nullptr) {
                return false;
            }

            frameResources.cameraMapped->viewProj =
                cameraOverrides != nullptr && cameraOverrides->HasRenderMatrices()
                    ? *cameraOverrides->renderViewProj
                    : camera.GetViewProj();
            frameResources.cameraMapped->invViewProj =
                cameraOverrides != nullptr && cameraOverrides->HasRenderMatrices()
                    ? *cameraOverrides->renderInvViewProj
                    : MATH::Inverse(frameResources.cameraMapped->viewProj);
            const MATH::Vec3 cameraPos = camera.GetPosition();
            frameResources.cameraMapped->cameraPos = {
                cameraPos.x,
                cameraPos.y,
                cameraPos.z,
                1.0f
            };
            const FrameContext& frame = TIME::GetFrameContext();
            g.elapsedTimeSec += std::max(0.0f, frame.unscaledDt);
            frameResources.cameraMapped->timeParams = {
                g.elapsedTimeSec,
                frame.unscaledDt,
                frame.gameDt,
                static_cast<float>(frame.frameIndex)
            };
            int screenW = static_cast<int>(overrideScreenWidth);
            int screenH = static_cast<int>(overrideScreenHeight);
            if (screenW <= 0 || screenH <= 0) {
                screenW = std::max(1, SERVICES::gCtx.backBufferWidth);
                screenH = std::max(1, SERVICES::gCtx.backBufferHeight);
            }
            frameResources.cameraMapped->screenParams = {
                static_cast<float>(screenW),
                static_cast<float>(screenH),
                1.0f / static_cast<float>(screenW),
                1.0f / static_cast<float>(screenH)
            };
            CameraCB cullingCamera = *frameResources.cameraMapped;
            if (cameraOverrides != nullptr && cameraOverrides->HasCullingMatrices()) {
                cullingCamera.viewProj = *cameraOverrides->cullingViewProj;
                cullingCamera.invViewProj = *cameraOverrides->cullingInvViewProj;
            }
            const uint32_t cullingWidth = static_cast<uint32_t>(screenW);
            const uint32_t cullingHeight = static_cast<uint32_t>(screenH);
            if (g.freezeGpuDrivenCullingCamera) {
                const bool sizeChanged =
                    g.frozenCullingCameraWidth != cullingWidth ||
                    g.frozenCullingCameraHeight != cullingHeight;
                if (!g.frozenCullingCameraValid || sizeChanged) {
                    g.frozenCullingCamera = cullingCamera;
                    g.frozenCullingCameraValid = true;
                    g.frozenCullingCameraWidth = cullingWidth;
                    g.frozenCullingCameraHeight = cullingHeight;
                }
                *frameResources.cullingCameraMapped = g.frozenCullingCamera;
            } else {
                g.frozenCullingCameraValid = false;
                g.frozenCullingCameraWidth = 0;
                g.frozenCullingCameraHeight = 0;
                *frameResources.cullingCameraMapped = cullingCamera;
            }
            g.debugStats.gpuDrivenCullingCameraFrozen =
                g.freezeGpuDrivenCullingCamera && g.frozenCullingCameraValid;

            FillLightCB(
                environment,
                debugView,
                *frameResources.lightMapped,
                g.debugStats);
            FillShadowCB(environment, *frameResources.shadowMapped);
            FillSkyEnvironmentCB(
                environment,
                *frameResources.skyEnvironmentMapped);
            return true;
        }

        MeshDrawContext BuildDrawContext(
            bool depthAwarePhase,
            MeshDrawPassKind passKind,
            const MeshPassResources& passResources
#if defined(HIKARI_WITH_EDITOR)
            , const MeshFrameBindingOverrides* overrides
#endif
        ) {
            MeshDrawContext ctx{};
            MeshRendererFrameResources& frame = ActiveFrameResources();
            ctx.cmd = SERVICES::gCtx.cmdList;
            ctx.staticRootSig = GetStaticRootSignature(g.pipelines);
            ctx.skinnedRootSig = GetSkinnedRootSignature(g.pipelines);
            ctx.objectCB = frame.objectCB.Get();
            ctx.objectDataBuffer = frame.objectDataBuffer.Get();
            ctx.materialDataBuffer = frame.materialDataBuffer.Get();
            ctx.jointPaletteCB = frame.jointPaletteCB.Get();
            ctx.objectMapped = frame.objectMapped;
            ctx.objectDataMapped = frame.objectDataMapped;
            ctx.materialDataMapped = frame.materialDataMapped;
            ctx.jointPaletteMapped = frame.jointPaletteMapped;
            ctx.gpuMaterialRegistry = &g.gpuMaterialRegistry;
            ctx.objectDataSrv = frame.objectDataSrvGpu;
            ctx.materialDataSrv = frame.materialDataSrvGpu;
            ctx.surfaceGpuSceneSrv = g.surfaceGpuSceneBuffer.GetSrv();
            ctx.surfaceGpuSceneFrameBuffer = &g.surfaceGpuSceneBuffer;
#if defined(HIKARI_WITH_EDITOR)
            ctx.traditionalCommandStreamBuffer =
                overrides != nullptr &&
                    overrides->traditionalCommandStreamBuffer != nullptr
                    ? overrides->traditionalCommandStreamBuffer
                    : &g.traditionalCommandStreamBuffer;
            ctx.cameraAddress =
                overrides != nullptr && overrides->cameraAddress != 0
                    ? overrides->cameraAddress
                    : ResolveCameraAddressForPass(passKind);
            ctx.cullingCameraAddress =
                overrides != nullptr && overrides->cullingCameraAddress != 0
                    ? overrides->cullingCameraAddress
                    : ResolveCullingCameraAddress();
            ctx.lightAddress =
                overrides != nullptr && overrides->lightAddress != 0
                    ? overrides->lightAddress
                    : (frame.lightCB
                        ? frame.lightCB->GetGPUVirtualAddress()
                        : 0);
            ctx.shadowAddress =
                overrides != nullptr && overrides->shadowAddress != 0
                    ? overrides->shadowAddress
                    : (frame.shadowCB
                        ? frame.shadowCB->GetGPUVirtualAddress()
                        : 0);
            ctx.skyEnvironmentAddress =
                overrides != nullptr &&
                    overrides->skyEnvironmentAddress != 0
                    ? overrides->skyEnvironmentAddress
                    : (frame.skyEnvironmentCB
                        ? frame.skyEnvironmentCB->GetGPUVirtualAddress()
                        : 0);
#else
            ctx.traditionalCommandStreamBuffer = &g.traditionalCommandStreamBuffer;
            ctx.cameraAddress = ResolveCameraAddressForPass(passKind);
            ctx.cullingCameraAddress = ResolveCullingCameraAddress();
            ctx.lightAddress = frame.lightCB
                ? frame.lightCB->GetGPUVirtualAddress()
                : 0;
            ctx.shadowAddress = frame.shadowCB
                ? frame.shadowCB->GetGPUVirtualAddress()
                : 0;
            ctx.skyEnvironmentAddress = frame.skyEnvironmentCB
                ? frame.skyEnvironmentCB->GetGPUVirtualAddress()
                : 0;
#endif
            ctx.passKind = passKind;
            ctx.binding.cmd = ctx.cmd;
            ctx.binding.depthAwarePhase = depthAwarePhase;
            ctx.binding.fallbackTextureHandle = g.fallbackTextureHandle;
            ctx.binding.fallbackNormalTextureHandle = g.fallbackNormalTextureHandle;
            ctx.binding.fallbackBlackTextureHandle = g.fallbackBlackTextureHandle;
            ctx.binding.fallbackCubeTextureHandle = g.fallbackCubeTextureHandle;
            ctx.binding.passResources = passResources;
            if (ctx.binding.passResources.fallbackAoTextureHandle < 0) {
                ctx.binding.passResources.fallbackAoTextureHandle = g.fallbackTextureHandle;
            }
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

        bool HasGpuDrivenSceneSource() {
            return g.gpuDrivenSceneSource.HasAnyGpuSceneRanges();
        }

        bool RenderGeometryAuxPassInternal(
            RENDER3D::SCREENSPACE::ScreenSpaceGeometryAux& geometryAux,
            D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv) {
            if (!HasGpuDrivenPassSource(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque)) {
                return false;
            }

            const CameraCB* camera = ActiveFrameResources().cameraMapped;
            const uint32_t width = static_cast<uint32_t>(
                std::max(1.0f, camera ? camera->screenParams.x : 1.0f));
            const uint32_t height = static_cast<uint32_t>(
                std::max(1.0f, camera ? camera->screenParams.y : 1.0f));
            if (!geometryAux.EnsureSize(width, height)) {
                return false;
            }

            if (sceneDsv.ptr == 0) {
                return false;
            }

            GFX::PIX::ScopedGpuEvent pixGeometry(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, "ScreenSpaceGeometryAux");
            geometryAux.BeginNormalRoughnessPass(SERVICES::gCtx.cmdList, sceneDsv);
            MeshPassResources passResources{};
            const GeometryBackendExecutionResult backendResult =
                ExecuteGeometryBackendPlan(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::GeometryAux,
                    passResources,
                    MeshDrawPassKind::GeometryAux,
                    RENDER3D::MESHLET::MeshletPipelineKind::GeometryAux);
            geometryAux.EndNormalRoughnessPass(SERVICES::gCtx.cmdList);
            return backendResult.gpuBackendExecuted;
        }

        bool RenderDepthPrepassInternal(D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv) {
            if (!HasGpuDrivenPassSource(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::DepthPrepass)) {
                return false;
            }

            ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
            if (cmd == nullptr || sceneDsv.ptr == 0) {
                return false;
            }

            const CameraCB* camera = ActiveFrameResources().cameraMapped;
            const uint32_t width = static_cast<uint32_t>(
                std::max(1.0f, camera ? camera->screenParams.x : 1.0f));
            const uint32_t height = static_cast<uint32_t>(
                std::max(1.0f, camera ? camera->screenParams.y : 1.0f));
            D3D12_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(width);
            viewport.Height = static_cast<float>(height);
            viewport.MaxDepth = 1.0f;
            D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };

            GFX::PIX::ScopedGpuEvent pixDepth(
                cmd,
                GFX::PIX::kColorRender,
                "GpuDepthVisibility.DepthPrepass");
            cmd->OMSetRenderTargets(0, nullptr, FALSE, &sceneDsv);
            cmd->RSSetViewports(1, &viewport);
            cmd->RSSetScissorRects(1, &scissor);

            const MeshPassResources passResources{};
            const GeometryBackendExecutionResult backendResult =
                ExecuteDepthVisibilityBackendPlan(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::DepthPrepass,
                    passResources);
            return backendResult.gpuBackendExecuted;
        }

        bool BeginFrameInternal(
            const Camera3D& camera,
            const SceneEnvironment& environment,
            uint32_t screenWidth,
            uint32_t screenHeight,
            RenderDebugView debugView,
            const MeshFrameCameraOverrides* cameraOverrides) {

            if (!EnsureInitialized()) {
                return false;
            }

            g.frameResources.Activate(SERVICES::gCtx.frameIndex);
            g.materialResolver.BeginFrame(kMaxMaterialTextureGpuLoadsPerFrame);
            g.gpuMaterialRegistry.BeginFrame();
            // Capture 逕ｨ縺ｮ蝗ｺ螳夊ｧ｣蜒丞ｺｦ繧・camera constants 縺ｫ蜿肴丐縺吶ｋ縲・
            if (!PrepareMeshFrame(
                    camera,
                    environment,
                    debugView,
                    screenWidth,
                    screenHeight,
                    cameraOverrides)) {
                return false;
            }

            ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
            if (cmd == nullptr ||
                !g.frameResources.HasActiveDrawResources()) {
                return false;
            }

            g.frameObjectIndex = 0;
            SyncSurfaceGpuSceneMaterialFrame();
            CommitActiveMaterialDataFrame(cmd);
            if (HasGpuDrivenSceneSource()) {
                UploadMeshShaderJointPalettes();
                g.traditionalIndirectOwner.RefreshForActivePipeline(
                    g.gpuDrivenSceneSource,
                    BuildTraditionalIndirectHydrationContext());
                PrepareGpuDrivenFrameState();
            } else {
                ResetGpuDrivenFrameState();
            }

            cmd->IASetPrimitiveTopology(
                D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D12DescriptorHeap* srvHeap =
                RENDER3D::GetTextureResourceSrvHeap();
            if (srvHeap != nullptr) {
                ID3D12DescriptorHeap* heaps[] = { srvHeap };
                cmd->SetDescriptorHeaps(1, heaps);
            }
            return true;
        }

#if defined(HIKARI_WITH_EDITOR)
        bool RenderEditorInteractiveMainlineStatic(
            const MeshPassResources& passResources,
            const MeshFrameBindingOverrides& overrides) {

            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass =
                g.gpuDrivenSceneSource.GetPass(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque);
            if (pass.instances == nullptr ||
                pass.materialSources == nullptr ||
                pass.instances->empty() ||
                SERVICES::gCtx.cmdList == nullptr) {
                return false;
            }

            const size_t instanceCount = (std::min)(
                pass.instances->size(),
                pass.materialSources->size());
            MeshBindingStateCache bindingCache{};
            MeshDrawContext drawCtx = BuildDrawContext(
                false,
                MeshDrawPassKind::Forward,
                passResources,
                &overrides);
            drawCtx.binding.cache = &bindingCache;
            BindSurfaceRecordFrameResources(drawCtx);
            BindObjectDataIndex(drawCtx.binding, 0u);
            BindMaterialDataIndex(drawCtx.binding, 0u);

            bool rendered = false;
            for (size_t instanceIndex = 0;
                instanceIndex < instanceCount;
                ++instanceIndex) {

                const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance =
                    (*pass.instances)[instanceIndex];
                const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source =
                    (*pass.materialSources)[instanceIndex];
                if (source.model == nullptr ||
                    instance.meshIndex >= source.model->meshes.size()) {
                    continue;
                }
                const MeshAsset& meshAsset =
                    source.model->meshes[instance.meshIndex];
                if (instance.primitiveIndex >= meshAsset.primitives.size()) {
                    continue;
                }
                const MeshPrimitive& primitive =
                    meshAsset.primitives[instance.primitiveIndex];
                if (primitive.layout != VertexLayoutKind::StaticPNTT) {
                    continue;
                }

                Mesh* mesh = g.primitiveCache.GetOrCreateStatic(
                    SERVICES::gCtx.device,
                    primitive,
                    &g.debugStats);
                if (mesh == nullptr || !mesh->IsValid()) {
                    continue;
                }

                DrawItem variantItem{};
                variantItem.asset = source.model;
                variantItem.materialOverride = source.materialOverride;
                variantItem.fxFlags = source.fxFlags;
                for (size_t fxIndex = 0;
                    fxIndex < VFX::kMaterialFxUserCount;
                    ++fxIndex) {
                    variantItem.fxValues[fxIndex] = source.fxUser[fxIndex];
                }
                ResolveDrawVariant(variantItem);
                const MaterialAsset* materialAsset =
                    primitive.materialIndex < source.model->materials.size()
                        ? &source.model->materials[primitive.materialIndex]
                        : nullptr;
                const VFX::VariantKey variant = ResolvePrimitiveVariant(
                    variantItem,
                    source.materialOverride != nullptr
                        ? nullptr
                        : materialAsset,
                    &primitive);
                ID3D12PipelineState* pso = ResolveTraditionalStaticPso(
                    MeshDrawPassKind::Forward,
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque,
                    variant);
                if (pso == nullptr) {
                    continue;
                }

                BindPipelineState(drawCtx.binding, pso);
                BindSurfaceGpuSceneControl(
                    drawCtx.binding,
                    pass.gpuSceneBaseIndex +
                        static_cast<uint32_t>(instanceIndex),
                    true);
                const D3D12_VERTEX_BUFFER_VIEW vertexBuffer =
                    mesh->GetVBView();
                const D3D12_INDEX_BUFFER_VIEW indexBuffer =
                    mesh->GetIBView();
                SERVICES::gCtx.cmdList->IASetVertexBuffers(
                    0,
                    1,
                    &vertexBuffer);
                SERVICES::gCtx.cmdList->IASetIndexBuffer(&indexBuffer);
                SERVICES::gCtx.cmdList->DrawIndexedInstanced(
                    mesh->GetIndexCount(),
                    1u,
                    0u,
                    0,
                    0u);
                rendered = true;
            }
            return rendered;
        }
#endif

    }

    void Reset() {
        g.frameObjectIndex = 0;
        g.gpuMaterialRegistry.Clear();
        g.gpuDrivenSceneSource.Reset();
        g.gpuDrivenSceneSourceIdentity = nullptr;
        g.freezeGpuDrivenCullingCamera = false;
        g.frozenCullingCamera = {};
        g.frozenCullingCameraValid = false;
        g.frozenCullingCameraWidth = 0;
        g.frozenCullingCameraHeight = 0;
        g.debugStats = {};
    }

    void InvalidateMaterialFxPipelineCache() {
        InvalidateMeshPipelineVariants(g.pipelines);
    }

    void SetGpuDrivenSceneSource(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source) {

        if (source != nullptr &&
            g.gpuDrivenSceneSourceIdentity == source &&
            g.gpuDrivenSceneSource.layoutVersion == source->layoutVersion &&
            g.gpuDrivenSceneSource.sourceVersion == source->sourceVersion &&
            g.gpuDrivenSceneSource.sourceInstanceCount == source->sourceInstanceCount &&
            g.gpuDrivenSceneSource.sourceRecordCount == source->sourceRecordCount) {

            g.gpuDrivenSceneSource.dirtyBaseSourceVersion =
                source->dirtyBaseSourceVersion;
            for (size_t passIndex = 0;
                passIndex < RENDER3D::GPUDRIVEN::kGpuDrivenPassCount;
                ++passIndex) {

                g.gpuDrivenSceneSource.passes[passIndex].dirtyRanges =
                    source->passes[passIndex].dirtyRanges;
            }
            return;
        }

        g.gpuDrivenSceneSource.Reset();
        g.traditionalIndirectOwner.Clear();
        g.gpuDrivenSceneSourceIdentity = source;
        if (source == nullptr) {
            g.gpuMaterialRegistry.Clear();
            return;
        }
        g.gpuDrivenSceneSource = *source;
        g.traditionalIndirectOwner.CopyFromSceneSource(
            g.gpuDrivenSceneSource);
        g.traditionalIndirectOwner.AttachToSceneSource(
            g.gpuDrivenSceneSource);
    }

    bool HasSubmittedItems() {
        return HasGpuDrivenSceneSource();
    }

    bool BeginFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        RenderDebugView debugView) {
        return BeginFrameInternal(
            camera,
            environment,
            0u,
            0u,
            debugView,
            nullptr);
    }

    bool BeginFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        uint32_t screenWidth,
        uint32_t screenHeight,
        RenderDebugView debugView,
        const MeshFrameCameraOverrides* cameraOverrides) {
        return BeginFrameInternal(
            camera,
            environment,
            screenWidth,
            screenHeight,
            debugView,
            cameraOverrides);
    }

    void SetGpuDrivenCullingCameraFreezeEnabled(bool enabled) {
        if (g.freezeGpuDrivenCullingCamera == enabled) {
            return;
        }
        g.freezeGpuDrivenCullingCamera = enabled;
        if (!enabled) {
            g.frozenCullingCamera = {};
            g.frozenCullingCameraValid = false;
            g.frozenCullingCameraWidth = 0;
            g.frozenCullingCameraHeight = 0;
            g.debugStats.gpuDrivenCullingCameraFrozen = false;
        }
    }

    bool IsGpuDrivenCullingCameraFrozen() {
        return g.freezeGpuDrivenCullingCamera && g.frozenCullingCameraValid;
    }

    const CameraCB* GetCameraConstants() {
        return ActiveFrameResources().cameraMapped;
    }

    const CameraCB* GetGpuDrivenCullingCameraConstants() {
        const MeshRendererFrameResources& frame = ActiveFrameResources();
        return frame.cullingCameraMapped != nullptr
            ? frame.cullingCameraMapped
            : frame.cameraMapped;
    }

    bool RenderGeometryAuxPass(
        RENDER3D::SCREENSPACE::ScreenSpaceGeometryAux& geometryAux,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv) {
        return RenderGeometryAuxPassInternal(geometryAux, sceneDsv);
    }

    bool HasDepthPrepassWork() {
        return HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::DepthPrepass);
    }

    bool RenderDepthPrepass(D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv) {
        return RenderDepthPrepassInternal(sceneDsv);
    }

    bool FinalizeGpuDrivenVisibilityWithoutDepth() {
        if (!HasGpuDrivenSceneSource()) {
            return false;
        }

        GFX::PIX::ScopedGpuEvent pixFinalize(
            SERVICES::gCtx.cmdList,
            GFX::PIX::kColorUpload,
            "GpuDepthVisibility.FinalizeVisibility.NoHZB");
        BuildGpuDrivenWorkFrame(
            nullptr,
            MakeMainCameraGpuDrivenPassMask(),
            true);
        UpdateGpuDrivenWorkReadyDebugStats();
        UpdateGpuDrivenWorkOwnershipDebugStats();
        BuildStrictGpuDrivenCommandFrame();
        return true;
    }

    bool FinalizeGpuDrivenVisibilityFromDepth(
        const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& depthVisibilityStats) {

        const HIKARI::RENDER3D::DEPTH::DepthPyramidView& depthPyramid =
            depthVisibilityStats.depthPyramid;
        if (!HasGpuDrivenSceneSource() ||
            !depthPyramid.valid ||
            depthPyramid.pyramidSrv.ptr == 0 ||
            depthPyramid.width == 0 ||
            depthPyramid.height == 0 ||
            !depthPyramid.viewProjValid) {
            return false;
        }

        GFX::PIX::ScopedGpuEvent pixFinalize(
            SERVICES::gCtx.cmdList,
            GFX::PIX::kColorUpload,
            "GpuDepthVisibility.FinalizeVisibility");
        BuildGpuDrivenWorkFrame(
            &depthPyramid,
            MakeMainCameraGpuDrivenPassMask(),
            true);
        UpdateGpuDrivenWorkReadyDebugStats();
        UpdateGpuDrivenWorkOwnershipDebugStats();
        BuildStrictGpuDrivenCommandFrame();
        return true;
    }

    bool RenderForwardOpaquePass(
        const MeshPassResources& passResources) {
        if (!HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque)) {
            return true;
        }
        const GeometryBackendExecutionResult backendResult =
            ExecuteGeometryBackendPlan(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque,
                passResources,
                MeshDrawPassKind::Forward,
                RENDER3D::MESHLET::MeshletPipelineKind::ForwardOpaque);
        return backendResult.gpuBackendExecuted;
    }

    bool RenderForwardTransparentPass(
        const MeshPassResources& passResources) {
        if (!HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent)) {
            return true;
        }
        const GeometryBackendExecutionResult backendResult =
            ExecuteGeometryBackendPlan(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent,
                passResources,
                MeshDrawPassKind::Forward,
                RENDER3D::MESHLET::MeshletPipelineKind::ForwardTransparent);
        return backendResult.gpuBackendExecuted;
    }

    bool HasDepthAwarePassWork() {
        return HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware);
    }

    bool HasForwardTransparentPassWork() {
        return HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent);
    }

    bool RenderDepthAwarePass(
        const MeshPassResources& passResources) {
        if (!HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware)) {
            return true;
        }
        const GeometryBackendExecutionResult backendResult =
            ExecuteGeometryBackendPlan(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware,
                passResources,
                MeshDrawPassKind::Forward,
                RENDER3D::MESHLET::MeshletPipelineKind::ForwardDepthAware);
        return backendResult.gpuBackendExecuted;
    }

    void SetAmbientOcclusionRuntimeEnabled(bool enabled) {
        SkyEnvironmentCB* skyEnvironment =
            ActiveFrameResources().skyEnvironmentMapped;
        if (skyEnvironment != nullptr) {
            skyEnvironment->aoParams.x = enabled ? 1.0f : 0.0f;
        }
    }

    void GatherTemporalVelocityDraws(
        std::vector<TemporalVelocityDraw>& outDraws) {

        outDraws.clear();
        const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& opaquePass =
            GetSceneSourcePass(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque);

        // Rigid dynamic surfaces may be owned by the mesh-shader mainline.
        // Expose their triangle source only to the temporal sidecar instead of
        // forcing the complete forward pass through a second geometry pass.
        if (opaquePass.instances != nullptr &&
            opaquePass.materialSources != nullptr) {
            const auto& instances = *opaquePass.instances;
            const auto& materialSources = *opaquePass.materialSources;
            const size_t count = (std::min)(instances.size(), materialSources.size());
            for (size_t index = 0; index < count; ++index) {
                const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance =
                    instances[index];
                const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source =
                    materialSources[index];
                const uint32_t flags = instance.flags;
                if ((flags & static_cast<uint32_t>(
                        RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::PassForwardOpaque)) == 0u ||
                    (flags & static_cast<uint32_t>(
                        RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::StaticGeometry)) != 0u ||
                    source.model == nullptr ||
                    instance.meshIndex >= source.model->meshes.size()) {
                    continue;
                }

                const MeshAsset& meshAsset = source.model->meshes[instance.meshIndex];
                if (instance.primitiveIndex >= meshAsset.primitives.size()) {
                    continue;
                }
                const MeshPrimitive& primitive = meshAsset.primitives[instance.primitiveIndex];
                if (primitive.layout != VertexLayoutKind::StaticPNTT) {
                    continue;
                }
                Mesh* mesh = g.primitiveCache.GetOrCreateStatic(
                    SERVICES::gCtx.device,
                    primitive,
                    &g.debugStats);
                if (mesh == nullptr || !mesh->IsValid()) {
                    continue;
                }

                TemporalVelocityDraw draw{};
                draw.objectId =
                    (static_cast<uint64_t>(instance.objectIdHigh) << 32u) |
                    static_cast<uint64_t>(instance.objectIdLow);
                draw.world = instance.world;
                draw.vertexBuffer = mesh->GetVBView();
                draw.indexBuffer = mesh->GetIBView();
                draw.indexCount = mesh->GetIndexCount();
                draw.doubleSided =
                    (flags & static_cast<uint32_t>(
                        RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::DoubleSided)) != 0u;
                draw.alphaMasked =
                    (flags & static_cast<uint32_t>(
                        RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::AlphaMasked)) != 0u;
                outDraws.push_back(draw);
            }
        }

        // Skinned records carry the current palette in the traditional
        // sidecar. They are appended separately and deduplicated by object and
        // geometry identity.
        const RENDER3D::GPUDRIVEN::GpuDrivenTraditionalIndirectView stream =
            g.traditionalIndirectOwner.GetView(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque);
        if (stream.commands == nullptr ||
            stream.executableRecordIndices == nullptr ||
            stream.records == nullptr ||
            stream.jointPalettes == nullptr) {
            return;
        }
        for (const RENDER3D::RUNTIME::SurfaceDrawCommand& command : *stream.commands) {
            if (!command.drawArgsValid ||
                !command.HasTriangleMeshGpuView() ||
                command.firstExecutableIndex >= stream.executableRecordIndices->size()) {
                continue;
            }
            const uint32_t recordIndex =
                (*stream.executableRecordIndices)[command.firstExecutableIndex];
            if (recordIndex >= stream.records->size() ||
                recordIndex >= stream.jointPalettes->size()) {
                continue;
            }
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record =
                (*stream.records)[recordIndex];
            if (!record.skinned || (*stream.jointPalettes)[recordIndex].empty()) {
                continue;
            }

            TemporalVelocityDraw draw{};
            draw.objectId = record.objectId.value;
            draw.world = record.drawWorldMatrix;
            draw.vertexBuffer = command.triangleMeshView.vertexBuffer;
            draw.indexBuffer = command.triangleMeshView.indexBuffer;
            draw.indexCount = command.drawArgs.indexCountPerInstance;
            draw.startIndex = command.drawArgs.startIndexLocation;
            draw.baseVertex = command.drawArgs.baseVertexLocation;
            draw.skinned = true;
            draw.doubleSided = record.key.doubleSided;
            draw.alphaMasked = record.key.alphaMasked;
            draw.jointPalette = &(*stream.jointPalettes)[recordIndex];
            outDraws.push_back(draw);
        }
    }

    void EndFrame() {
        g.frameObjectIndex = 0;
    }

#if defined(HIKARI_WITH_EDITOR)
    bool RenderEditorInteractiveOpaque(
        const RENDER3D::RenderViewContext& view,
        const SceneEnvironment& environment,
        uint32_t screenWidth,
        uint32_t screenHeight,
        const EditorInteractiveRenderSettings& settings) {

        if (view.cameraFrame == nullptr ||
            !view.cameraFrame->valid ||
            screenWidth == 0 ||
            screenHeight == 0 ||
            SERVICES::gCtx.cmdList == nullptr ||
            !EnsureInitialized() ||
            !g.frameResources.HasActiveDrawResources() ||
            !EnsureEditorInteractiveResources()) {
            return false;
        }

        EditorInteractiveMeshFrameResources& editorFrame =
            g.editorInteractive.frames[
                SERVICES::gCtx.frameIndex % GFX::kFrameResourceCount];
        if (!editorFrame.IsReady()) {
            return false;
        }

        const Camera3D& camera = view.cameraFrame->camera;
        CameraCB& cameraData = *editorFrame.cameraMapped;
        cameraData = {};
        cameraData.viewProj = camera.GetViewProj();
        cameraData.invViewProj = MATH::Inverse(cameraData.viewProj);
        const MATH::Vec3 cameraPosition = camera.GetPosition();
        cameraData.cameraPos = {
            cameraPosition.x,
            cameraPosition.y,
            cameraPosition.z,
            1.0f
        };
        const FrameContext& timeFrame = TIME::GetFrameContext();
        cameraData.timeParams = {
            g.elapsedTimeSec,
            timeFrame.unscaledDt,
            timeFrame.gameDt,
            static_cast<float>(timeFrame.frameIndex)
        };
        cameraData.screenParams = {
            static_cast<float>(screenWidth),
            static_cast<float>(screenHeight),
            1.0f / static_cast<float>(screenWidth),
            1.0f / static_cast<float>(screenHeight)
        };
        FillLightCB(
            environment,
            settings.debugView,
            *editorFrame.lightMapped,
            g.debugStats);
        FillShadowCB(environment, *editorFrame.shadowMapped);
        FillSkyEnvironmentCB(
            environment,
            *editorFrame.skyEnvironmentMapped);
        if (settings.neutralLighting) {
            LightCB& light = *editorFrame.lightMapped;
            light.directionalDir = { 0.35f, -0.82f, 0.45f, 0.0f };
            light.directionalColor = { 1.0f, 0.98f, 0.94f, 1.0f };
            light.directionalIntensity = 0.75f;
            light.ambientColor = { 0.92f, 0.96f, 1.0f, 1.0f };
            light.ambientIntensity = 0.55f;
            light.specularParams.x = 0.12f;
            light.pointLightCount = 0;
            light.fogParams.x = 0.0f;
            editorFrame.shadowMapped->enabled = 0;
        }

        RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer =
            g.editorInteractive.gpuDrivenLayer;
        layer.BeginFrame(&g.gpuDrivenSceneSource);
        RENDER3D::GPUDRIVEN::GpuDrivenBackendAvailability availability{};
        availability.traditionalIndirectPipelineReady =
            GetStaticRootSignature(g.pipelines) != nullptr &&
            GetSkinnedRootSignature(g.pipelines) != nullptr &&
            g.pipelines.pso != nullptr &&
            g.pipelines.skinnedPso != nullptr;
        layer.SetBackendAvailability(availability);

        const MATH::Mat4 cullViewProj = cameraData.viewProj;
        RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDesc{};
        commandFrameDesc.commandList = SERVICES::gCtx.cmdList;
        commandFrameDesc.cullViewProj = &cullViewProj;
        commandFrameDesc.frameIndex = SERVICES::gCtx.frameIndex;
        commandFrameDesc.enableSurfaceFrustumCull = true;
        layer.BuildCommandFrame(commandFrameDesc);

        ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
        if (srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            SERVICES::gCtx.cmdList->SetDescriptorHeaps(1, heaps);
        }
        SERVICES::gCtx.cmdList->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        if (!HasGpuDrivenPassSource(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque)) {
            return true;
        }

        MeshFrameBindingOverrides overrides{};
        overrides.cameraAddress =
            editorFrame.cameraCB->GetGPUVirtualAddress();
        overrides.cullingCameraAddress = overrides.cameraAddress;
        overrides.lightAddress =
            editorFrame.lightCB->GetGPUVirtualAddress();
        overrides.shadowAddress =
            editorFrame.shadowCB->GetGPUVirtualAddress();
        overrides.skyEnvironmentAddress =
            editorFrame.skyEnvironmentCB->GetGPUVirtualAddress();
        overrides.traditionalCommandStreamBuffer =
            &g.editorInteractive.traditionalCommandStreamBuffer;

        MeshPassResources passResources{};
        passResources.fallbackAoTextureHandle = g.fallbackTextureHandle;
        const bool mainlineRendered =
            RenderEditorInteractiveMainlineStatic(
                passResources,
                overrides);
        const bool sidecarRendered = ExecuteTraditionalDrawFrame(
            passResources,
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque,
            MeshDrawPassKind::Forward,
            &layer,
            &overrides);
        return mainlineRendered || sidecarRendered;
    }

    void ShutdownEditorInteractiveResources() {
        ResetEditorInteractiveResources();
    }
#endif

    void RenderAll(
        const RENDER3D::RenderViewContext& view,
        const SceneEnvironment& environment,
        RenderDebugView debugView) {

        if (view.cameraFrame == nullptr || !view.cameraFrame->valid) {
            return;
        }
        (void)RENDER3D::PIPELINE::RenderMeshLightingFrame(view, environment, debugView);
    }

    void RenderAll(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        RenderDebugView debugView) {

        RENDER3D::ResolvedCameraFrame cameraFrame{};
        cameraFrame.camera = camera;
        cameraFrame.valid = true;

        RENDER3D::RenderViewContext view{};
        view.viewId = RENDER3D::kPrimaryRenderViewId;
        view.purpose = RENDER3D::RenderViewPurpose::Game;
        view.cameraFrame = &cameraFrame;
        RenderAll(view, environment, debugView);
    }

    const MeshRendererDebugStats& GetDebugStats() {
        const MaterialFxProfileCacheStats fxCacheStats = MaterialFxProfile::GetCacheStats();
        g.debugStats.materialFxProfileCacheHitCount = fxCacheStats.hitCount;
        g.debugStats.materialFxProfileCacheMissCount = fxCacheStats.missCount;
        g.debugStats.materialFxProfileCacheFailCount = fxCacheStats.failCount;
        return g.debugStats;
    }

    const RENDER3D::MATERIAL::GpuMaterialRegistryStats&
        GetGpuMaterialRegistryStats() {
        return g.gpuMaterialRegistry.GetStats();
    }

} // namespace HIKARI::MESHRENDERER
