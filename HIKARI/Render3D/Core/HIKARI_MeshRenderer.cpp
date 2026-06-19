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
#include "Render3D/GpuDriven/HIKARI_GeometryBackendPolicy.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenDrawCommandStream.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenWorkBuilder.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Render3D/Pipeline/HIKARI_CpuRenderQueue.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpaceGeometryAux.h"
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

        void UpdateClusterDrawDebugStats();
        void UpdateMeshletBackendDebugStats();
        void SyncGpuDrivenBackendAvailability();
        void UpdateGpuDrivenWorkReadyDebugStats();
        void UpdateGpuDrivenCommandStreamDebugStats();
        void BuildGpuDrivenFrameState();
        void UpdateGpuDrivenWorkOwnershipDebugStats();
        void BuildGpuDrivenWorkFrame();

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
            const UINT surfaceGpuSceneSrvIndex =
                GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::MeshSurfaceGpuScene);
            const D3D12_CPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrvCpu =
                GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, surfaceGpuSceneSrvIndex);
            const D3D12_GPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrvGpu =
                GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, surfaceGpuSceneSrvIndex);
            if (!g.surfaceGpuSceneBuffer.Initialize(
                device,
                surfaceGpuSceneSrvCpu,
                surfaceGpuSceneSrvGpu)) {
                DEBUGLOG::PushRenderError("[MeshRenderer][WARN] SurfaceGpuScene buffer initialization failed. ObjectData fallback will be used.");
            }

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
            if (!g.surfaceIndirectDrawBuffer.Initialize(
                device,
                GetStaticRootSignature(g.pipelines),
                ROOT_PARAM::SurfaceGpuSceneControl,
                4u)) {
                DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Surface indirect draw buffer initialization failed. CPU-authored indirect draws stay disabled.");
            }
            if (!g.clusterGpuCullingPass.Initialize(
                device,
                GetStaticRootSignature(g.pipelines),
                ROOT_PARAM::SurfaceGpuSceneControl,
                4u)) {
                DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Cluster GPU culling pass initialization failed. Cluster draw seeds will be disabled.");
            }
            if (!g.clusterDrawExecutor.Initialize(
                device,
                GetStaticRootSignature(g.pipelines),
                RENDER3D::CLUSTER::ClusterDrawPipelineMask::MainRenderer)) {
                DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Cluster draw executor initialization failed. Cluster VS backend will be unavailable.");
            }
            if (!g.meshletRenderBackend.Initialize(
                device,
                GetStaticRootSignature(g.pipelines),
                RENDER3D::MESHLET::MeshletPipelineMask::MainRenderer)) {
                DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Meshlet render backend is not ready. Cluster draw backend remains active.");
            }
            g.clusterGpuDrivenProducer.Attach(&g.clusterGpuCullingPass);
            g.gpuDrivenLayer.Attach(
                &g.surfaceGpuSceneBuffer,
                &g.surfaceIndirectDrawBuffer,
                &g.clusterGpuDrivenProducer);
            if (!g.gpuDrivenLayer.Initialize(
                device,
                GetStaticRootSignature(g.pipelines),
                ROOT_PARAM::SurfaceGpuSceneControl,
                4u)) {
                DEBUGLOG::PushRenderError("[MeshRenderer][WARN] GPU-driven layer initialization failed. GPU-driven draws will be unavailable.");
            }
            UpdateMeshletBackendDebugStats();

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

        MeshDrawContext BuildDrawContext(
            bool depthAwarePhase,
            MeshDrawPassKind passKind,
            const MeshPassResources& passResources);

        void UploadGpuDrivenSceneFrame() {
            g.gpuDrivenLayer.BeginFrame(&g.gpuDrivenSceneSource);

            RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadDesc uploadDesc{};
            uploadDesc.residency = &g.gpuDrivenSceneResidency;
            const RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadStats& uploadStats =
                g.gpuDrivenLayer.UploadSceneFrame(uploadDesc);

            g.debugStats.surfaceGpuSceneOpaqueInstanceCount =
                uploadStats.passInstanceCounts[
                    RENDER3D::GPUDRIVEN::ToPassIndex(
                        RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque)];
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
            g.debugStats.surfaceGpuSceneOverflowInstanceCount = gpuSceneStats.overflowInstanceCount;
            g.debugStats.surfaceGpuSceneUploadCallCount = gpuSceneStats.uploadCallCount;
            g.debugStats.surfaceGpuSceneSrvValid = gpuSceneStats.srv.ptr != 0;
            g.debugStats.surfaceGpuSceneBufferReady = gpuSceneStats.initialized;
        }

        void PrepareSurfaceGpuSceneMaterialFrame() {
            MeshDrawContext drawCtx = BuildDrawContext(false, MeshDrawPassKind::Forward, {});
            const auto prepareMaterialSources =
                [&](uint32_t baseIndex,
                    const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>* sources) {
                drawCtx.surfaceGpuSceneBaseOffset = baseIndex;
                if (sources != nullptr && !sources->empty()) {
                    PrepareSurfaceGpuSceneMaterialSources(
                        drawCtx,
                        sources->data(),
                        sources->size());
                }
            };

            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& opaque =
                GetSceneSourcePass(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque);
            prepareMaterialSources(opaque.gpuSceneBaseIndex, opaque.materialSources);
            prepareMaterialSources(
                opaque.traditionalIndirect.gpuSceneBaseIndex,
                opaque.traditionalIndirect.materialSources);

            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& depthAware =
                GetSceneSourcePass(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware);
            prepareMaterialSources(depthAware.gpuSceneBaseIndex, depthAware.materialSources);
            prepareMaterialSources(
                depthAware.traditionalIndirect.gpuSceneBaseIndex,
                depthAware.traditionalIndirect.materialSources);

            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& transparent =
                GetSceneSourcePass(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent);
            prepareMaterialSources(transparent.gpuSceneBaseIndex, transparent.materialSources);
            prepareMaterialSources(
                transparent.traditionalIndirect.gpuSceneBaseIndex,
                transparent.traditionalIndirect.materialSources);

            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& shadow =
                GetSceneSourcePass(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
            prepareMaterialSources(shadow.gpuSceneBaseIndex, shadow.materialSources);
            prepareMaterialSources(
                shadow.traditionalIndirect.gpuSceneBaseIndex,
                shadow.traditionalIndirect.materialSources);
            g.gpuDrivenLayer.PrepareSurfaceGpuSceneMaterialFrame();
        }

        void UpdateSurfaceIndirectDrawStats() {
            const RENDER3D::GPUDRIVEN::SurfaceIndirectDrawBufferStats& indirectStats =
                g.gpuDrivenLayer.GetCommandFrameStats().surfaceIndirectStats;
            g.debugStats.surfaceIndirectCommandCapacity = indirectStats.capacity;
            g.debugStats.surfaceIndirectRequestedCommandCount = indirectStats.requestedCommandCount;
            g.debugStats.surfaceIndirectUploadedCommandCount = indirectStats.uploadedCommandCount;
            g.debugStats.surfaceIndirectOverflowCommandCount = indirectStats.overflowCommandCount;
            g.debugStats.surfaceIndirectFilteredCommandCount = indirectStats.filteredCommandCount;
            g.debugStats.surfaceIndirectCpuDirectCommandCount = indirectStats.cpuDirectCommandCount;
            g.debugStats.surfaceIndirectMissingDrawArgsCommandCount = indirectStats.missingDrawArgsCommandCount;
            g.debugStats.surfaceIndirectDrawBindingPatchCount = indirectStats.drawBindingPatchCount;
            g.debugStats.surfaceIndirectUploadCallCount = indirectStats.uploadCallCount;
            g.debugStats.surfaceIndirectCommandStride = indirectStats.commandStride;
            g.debugStats.surfaceIndirectArgumentBufferReady = indirectStats.initialized;
            g.debugStats.surfaceIndirectCommandSignatureReady = indirectStats.commandSignatureReady;
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

        void BuildStrictGpuDrivenCommandFrame() {
            RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDesc{};
            commandFrameDesc.commandList = SERVICES::gCtx.cmdList;
            g.gpuDrivenLayer.BuildCommandFrame(commandFrameDesc);
            UpdateSurfaceIndirectDrawStats();
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
            g.clusterDrawExecutor.ResetFrame();
            UpdateClusterDrawDebugStats();
            g.meshletRenderBackend.ResetFrame();
            UpdateMeshletBackendDebugStats();
            UpdateGpuDrivenWorkReadyDebugStats();
            UpdateGpuDrivenWorkOwnershipDebugStats();
            g.gpuDrivenLayer.BuildCommandFrame({ SERVICES::gCtx.cmdList });
            UpdateSurfaceIndirectDrawStats();
            UpdateGpuDrivenCommandStreamDebugStats();
        }

        void PrepareGpuDrivenFrameState() {
            UploadGpuDrivenSceneFrame();
            if (!g.gpuDrivenSceneResidency.resident) {
                g.gpuDrivenFrame.Reset();
                UpdateGpuDrivenWorklistDebugStats();
                g.clusterGpuDrivenProducer.BeginFrame(false);
                g.gpuDrivenLayer.ImportProducerOutput(
                    g.clusterGpuDrivenProducer.BuildFrameOutput());
                g.clusterDrawExecutor.ResetFrame();
                UpdateClusterDrawDebugStats();
                g.meshletRenderBackend.ResetFrame();
                UpdateMeshletBackendDebugStats();
                UpdateGpuDrivenWorkReadyDebugStats();
                UpdateGpuDrivenWorkOwnershipDebugStats();
                g.gpuDrivenLayer.BuildCommandFrame({ SERVICES::gCtx.cmdList });
                UpdateSurfaceIndirectDrawStats();
                UpdateGpuDrivenCommandStreamDebugStats();
                return;
            }
            PrepareSurfaceGpuSceneMaterialFrame();
            BuildGpuDrivenFrameState();
            BuildGpuDrivenWorkFrame();
            g.clusterDrawExecutor.ResetFrame();
            UpdateClusterDrawDebugStats();
            g.meshletRenderBackend.ResetFrame();
            UpdateMeshletBackendDebugStats();
            UpdateGpuDrivenWorkReadyDebugStats();
            UpdateGpuDrivenWorkOwnershipDebugStats();
            BuildStrictGpuDrivenCommandFrame();
        }

        void BuildGpuDrivenWorkFrame() {
            const MATH::Mat4 viewProj =
                g.cameraMapped != nullptr ? g.cameraMapped->viewProj : MATH::Mat4::Identity();
            const MATH::Vec3 cameraPosition =
                g.cameraMapped != nullptr
                    ? MATH::Vec3{
                        g.cameraMapped->cameraPos.x,
                        g.cameraMapped->cameraPos.y,
                        g.cameraMapped->cameraPos.z
                    }
                    : MATH::Vec3{};
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
                clusterCullStats.drawArgumentBufferReady &&
                clusterCullStats.counterBufferReady;
            g.debugStats.clusterGpuCullDrawArgsReady =
                clusterCullStats.drawArgumentBufferReady;
            g.debugStats.clusterGpuCullCommandSignatureReady =
                clusterCullStats.drawCommandSignatureReady;
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
            g.debugStats.clusterGpuCullGpuInputFrustumCulledCount =
                clusterCullStats.gpuInputFrustumCulledCount;
            g.debugStats.clusterGpuCullGpuPageTestedCount =
                clusterCullStats.gpuPageTestedCount;
            g.debugStats.clusterGpuCullGpuPageFrustumCulledCount =
                clusterCullStats.gpuPageFrustumCulledCount;
            g.debugStats.clusterGpuCullGpuClusterTestedCount =
                clusterCullStats.gpuClusterTestedCount;
            g.debugStats.clusterGpuCullGpuClusterFrustumCulledCount =
                clusterCullStats.gpuClusterFrustumCulledCount;
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
            g.debugStats.clusterGpuCullGpuLod0SelectedCount =
                clusterCullStats.gpuLod0SelectedCount;
            g.debugStats.clusterGpuCullGpuLod1SelectedCount =
                clusterCullStats.gpuLod1SelectedCount;
            g.debugStats.clusterGpuCullGpuLod2SelectedCount =
                clusterCullStats.gpuLod2SelectedCount;
            g.debugStats.clusterGpuCullGpuLod3PlusSelectedCount =
                clusterCullStats.gpuLod3PlusSelectedCount;
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
            g.debugStats.clusterGpuCullDrawArgumentCapacity =
                clusterCullStats.drawArgumentCapacity;
        }

        void UpdateClusterDrawDebugStats() {
            const RENDER3D::CLUSTER::ClusterDrawExecutorStats& clusterDrawStats =
                g.clusterDrawExecutor.GetStats();
            g.debugStats.clusterDrawRequestedCount =
                clusterDrawStats.requestedDrawCount;
            g.debugStats.clusterDrawSubmittedCount =
                clusterDrawStats.submittedDrawCount;
            g.debugStats.clusterDrawSkippedCount =
                clusterDrawStats.skippedDrawCount;
            g.debugStats.clusterDrawSkippedBucketCount =
                clusterDrawStats.skippedBucketCount;
            g.debugStats.clusterDrawSubmitCallCount =
                clusterDrawStats.submitCallCount;
            g.debugStats.clusterDrawForwardSubmittedCount =
                clusterDrawStats.forwardSubmittedDrawCount;
            g.debugStats.clusterDrawGeometryAuxSubmittedCount =
                clusterDrawStats.geometryAuxSubmittedDrawCount;
            g.debugStats.clusterDrawForwardSubmitCallCount =
                clusterDrawStats.forwardSubmitCallCount;
            g.debugStats.clusterDrawGeometryAuxSubmitCallCount =
                clusterDrawStats.geometryAuxSubmitCallCount;
            g.debugStats.clusterDrawBackFaceSubmitCallCount =
                clusterDrawStats.backFaceSubmitCallCount;
            g.debugStats.clusterDrawDoubleSidedSubmitCallCount =
                clusterDrawStats.doubleSidedSubmitCallCount;
            g.debugStats.clusterDrawPipelineReady =
                clusterDrawStats.drawPipelineReady;
            g.debugStats.clusterDrawForwardPipelineReady =
                clusterDrawStats.forwardPipelineReady;
            g.debugStats.clusterDrawGeometryAuxPipelineReady =
                clusterDrawStats.geometryAuxPipelineReady;
            g.debugStats.clusterDrawArgumentBufferReady =
                clusterDrawStats.drawArgumentBufferReady;
            g.debugStats.clusterDrawCommandSignatureReady =
                clusterDrawStats.drawCommandSignatureReady;
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
            const RENDER3D::CLUSTER::ClusterDrawExecutorStats& clusterStats =
                g.clusterDrawExecutor.GetStats();

            availability.meshShaderForwardPipelineReady =
                meshletStats.forwardPipelineReady &&
                meshletStats.depthAwarePipelineReady &&
                meshletStats.transparentPipelineReady;
            availability.meshShaderGeometryAuxPipelineReady =
                meshletStats.geometryAuxPipelineReady;
            availability.clusterVsForwardPipelineReady =
                clusterStats.forwardPipelineReady &&
                clusterStats.depthAwarePipelineReady &&
                clusterStats.transparentPipelineReady;
            availability.clusterVsGeometryAuxPipelineReady =
                clusterStats.geometryAuxPipelineReady;
            availability.traditionalIndirectPipelineReady =
                GetStaticRootSignature(g.pipelines) != nullptr &&
                GetSkinnedRootSignature(g.pipelines) != nullptr;
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
            g.debugStats.gpuDrivenCommandStreamCpuCommandCount =
                stream.CountCpuAuthoredCommands();
            g.debugStats.gpuDrivenCommandStreamGpuCommandCount =
                stream.CountGpuAuthoredCommands();
            g.debugStats.gpuDrivenCommandStreamTraditionalCommandCount =
                stream.CountTraditionalIndirectCommands();
        }

        void ApplyGpuDrivenWorkOwnershipDebugStats(
            const RENDER3D::GPUDRIVEN::GpuDrivenWorkOwnershipStats& stats) {

            g.debugStats.clusterMainlineOwnedCommandCount = stats.ownedCommandCount;
            g.debugStats.clusterMainlineOwnedPacketCount = stats.ownedPacketCount;
            g.debugStats.clusterMainlineGeometryAuxCommandCount = stats.geometryAuxCommandCount;
            g.debugStats.clusterMainlineGeometryAuxPacketCount = stats.geometryAuxPacketCount;
            g.debugStats.clusterMainlineLegacyCommandCount = stats.legacyCommandCount;
            g.debugStats.clusterMainlineLegacyPacketCount = stats.legacyPacketCount;
            g.debugStats.clusterDrawBypassedLegacyCommandCount = stats.bypassedLegacyCommandCount;
            g.debugStats.clusterDrawBypassedLegacyPacketCount = stats.bypassedLegacyPacketCount;

            g.debugStats.clusterDrawEligibleCommandCount = stats.eligibleCommandCount;
            g.debugStats.clusterDrawRejectContextCommandCount = stats.rejectContextCommandCount;
            g.debugStats.clusterDrawRejectMainlineCommandCount = stats.rejectMainlineCommandCount;
            g.debugStats.clusterDrawRejectBackendCommandCount = stats.rejectBackendCommandCount;
            g.debugStats.clusterDrawRejectTransparentCommandCount = stats.rejectTransparentCommandCount;
            g.debugStats.clusterDrawRejectMaterialFxCommandCount = stats.rejectMaterialFxCommandCount;
            g.debugStats.clusterDrawRejectRangeCommandCount = stats.rejectRangeCommandCount;
            g.debugStats.clusterDrawRejectInstanceResourceCommandCount = stats.rejectInstanceResourceCommandCount;
            g.debugStats.clusterDrawRejectInstanceFlagCommandCount = stats.rejectInstanceFlagCommandCount;
            g.debugStats.clusterDrawRejectMaterialPatchCommandCount = stats.rejectMaterialPatchCommandCount;
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
                // GPU 主導ルートでは CPU 側で per-surface 所有 mask を作らない。
                // ここではレガシー抑止の概算だけを表示し、実際の可視性/LOD は GPU counter に任せる。
                stats.ownedCommandCount = forward.sourceInstanceCount;
                stats.ownedPacketCount = forward.sourceInstanceCount;
                stats.eligibleCommandCount = stats.ownedCommandCount;
                stats.bypassedLegacyCommandCount = stats.ownedCommandCount;
                stats.bypassedLegacyPacketCount = stats.ownedPacketCount;
            }
            if (geometry.gpuBackendReady) {
                stats.geometryAuxCommandCount = geometry.sourceInstanceCount;
                stats.geometryAuxPacketCount = geometry.sourceInstanceCount;
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
                RENDER3D::GPUDRIVEN::GeometryBackendKind::CpuDirect;
        };

        bool ExecuteClusterDrawFrame(
            const MeshPassResources& passResources,
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
            MeshDrawPassKind passKind,
            RENDER3D::CLUSTER::ClusterDrawPipelineKind pipelineKind) {
            if (!IsGpuDrivenWorkPreparedForPass(gpuPass)) {
                return false;
            }

            MeshBindingStateCache bindingCache{};
            const bool depthAwarePhase =
                gpuPass == RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware;
            MeshDrawContext drawCtx = BuildDrawContext(depthAwarePhase, passKind, passResources);
            drawCtx.binding.cache = &bindingCache;
            BindSurfacePacketFrameResources(drawCtx);

            const RENDER3D::GPUDRIVEN::GeometryBackendContext backendContext =
                g.gpuDrivenLayer.BuildGeometryBackendContext(
                    SERVICES::gCtx.cmdList,
                    gpuPass,
                    RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenClusterVS);
            RENDER3D::CLUSTER::ClusterDrawExecutionContext ctx{};
            ctx.commandList = backendContext.commandList;
            ctx.pass = backendContext.pass;
            ctx.visibility = backendContext.visibility;
            ctx.drawCommandRange = backendContext.drawCommandRange;
            ctx.pipelineKind = pipelineKind;
            const bool executed = g.clusterDrawExecutor.Execute(ctx);
            UpdateClusterDrawDebugStats();
            UpdateGpuDrivenWorkReadyDebugStats();
            return executed;
        }

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
            if (visibleRangeBuffer == nullptr) {
                return false;
            }

            MeshBindingStateCache bindingCache{};
            const bool depthAwarePhase =
                gpuPass == RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware;
            MeshDrawContext drawCtx = BuildDrawContext(depthAwarePhase, passKind, passResources);
            drawCtx.binding.cache = &bindingCache;
            BindSurfacePacketFrameResources(drawCtx);
            BindMeshletVisibleRanges(
                drawCtx.binding,
                visibleRangeBuffer->GetGPUVirtualAddress());

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

        bool ExecuteTraditionalDrawFrame(
            const MeshPassResources& passResources,
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
            MeshDrawPassKind passKind) {

            if (!IsGpuDrivenWorkPreparedForPass(gpuPass)) {
                return false;
            }

            const RENDER3D::GPUDRIVEN::GeometryBackendContext backendContext =
                g.gpuDrivenLayer.BuildGeometryBackendContext(
                    SERVICES::gCtx.cmdList,
                    gpuPass,
                    RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVS);
            const RENDER3D::GPUDRIVEN::GpuDrivenTraditionalIndirectView* view =
                backendContext.traditionalIndirect;
            if (view == nullptr ||
                view->packets == nullptr ||
                view->executablePacketIndices == nullptr ||
                view->commands == nullptr ||
                view->jointPalettes == nullptr ||
                view->commands->empty()) {
                return false;
            }

            MeshBindingStateCache bindingCache{};
            const bool depthAwarePhase =
                gpuPass == RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware;
            MeshDrawContext drawCtx =
                BuildDrawContext(depthAwarePhase, passKind, passResources);
            drawCtx.binding.cache = &bindingCache;
            drawCtx.surfaceGpuSceneBaseOffset = view->gpuSceneBaseIndex;
            BindSurfacePacketFrameResources(drawCtx);

            size_t objectIndex = 0;
            size_t submitted = 0;
            size_t skipped = 0;
            for (uint32_t packetIndex : *view->executablePacketIndices) {
                if (packetIndex >= view->packets->size() ||
                    packetIndex >= view->jointPalettes->size()) {
                    ++skipped;
                    continue;
                }

                const RENDER3D::RUNTIME::SurfaceDrawPacket& packet =
                    (*view->packets)[packetIndex];
                const std::vector<MATH::Mat4>& jointPalette =
                    (*view->jointPalettes)[packetIndex];
                if (packet.model == nullptr || jointPalette.empty()) {
                    ++skipped;
                    continue;
                }

                DrawItem item{};
                item.asset = packet.model;
                item.materialOverride = packet.materialOverride;
                item.transform = packet.objectWorldTransform;
                item.jointPalette = jointPalette;
                item.materialFxProfileId = packet.materialFxProfileId;
                item.postGroupMask = packet.postGroupMask;
                for (size_t i = 0; i < item.materialFxParamValues.size(); ++i) {
                    item.materialFxParamValues[i] = packet.materialFxParamValues[i];
                }
                item.materialFxValuesInitialized = packet.materialFxValuesInitialized;
                item.usePrimitiveFilter = true;
                item.meshIndexFilter = packet.meshIndex;
                item.primitiveIndexFilter = packet.primitiveIndex;
                item.receiveShadow = packet.receiveShadow;
                item.renderDebugMode = MeshRenderDebugMode::Normal;
                ResolveDrawVariant(item);

                if (DrawMeshItem(drawCtx, item, objectIndex)) {
                    ++submitted;
                } else {
                    ++skipped;
                }
            }

            g.debugStats.gpuDrivenSkinnedCommandCount += view->commands->size();
            g.debugStats.gpuDrivenSkinnedSourcePacketCount +=
                view->executablePacketIndices->size();
            g.debugStats.gpuDrivenSkinnedSubmittedPacketCount += submitted;
            g.debugStats.gpuDrivenSkinnedSkippedPacketCount += skipped;
            return submitted != 0;
        }

        bool ExecuteGeometryBackend(
            RENDER3D::GPUDRIVEN::GeometryBackendKind backend,
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
            const MeshPassResources& passResources,
            MeshDrawPassKind passKind,
            RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind,
            RENDER3D::CLUSTER::ClusterDrawPipelineKind clusterPipelineKind) {

            switch (backend) {
            case RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader:
                return ExecuteMeshletDrawFrame(
                    passResources,
                    gpuPass,
                    passKind,
                    meshletPipelineKind);
            case RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenClusterVS:
                return ExecuteClusterDrawFrame(
                    passResources,
                    gpuPass,
                    passKind,
                    clusterPipelineKind);
            case RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVS:
                return ExecuteTraditionalDrawFrame(
                    passResources,
                    gpuPass,
                    passKind);
            case RENDER3D::GPUDRIVEN::GeometryBackendKind::CpuDirect:
            default:
                return false;
            }
        }

        GeometryBackendExecutionResult ExecuteGeometryBackendPlan(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass,
            const MeshPassResources& passResources,
            MeshDrawPassKind passKind,
            RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind,
            RENDER3D::CLUSTER::ClusterDrawPipelineKind clusterPipelineKind) {

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
                    meshletPipelineKind,
                    clusterPipelineKind)) {
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
                screenW = std::max(1, SERVICES::gCtx.backBufferWidth);
                screenH = std::max(1, SERVICES::gCtx.backBufferHeight);
            }
            g.cameraMapped->screenParams = {
                static_cast<float>(screenW),
                static_cast<float>(screenH),
                1.0f / static_cast<float>(screenW),
                1.0f / static_cast<float>(screenH)
            };

            FillLightCB(environment, debugView, *g.lightMapped, g.debugStats);
            FillShadowCB(environment, *g.shadowMapped);
            FillSkyEnvironmentCB(environment, *g.skyEnvironmentMapped);
            return true;
        }

        MeshDrawContext BuildDrawContext(
            bool depthAwarePhase,
            MeshDrawPassKind passKind,
            const MeshPassResources& passResources) {
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
            ctx.surfaceGpuSceneSrv = g.surfaceGpuSceneBuffer.GetSrv();
            ctx.surfaceGpuSceneFrameBuffer = &g.surfaceGpuSceneBuffer;
            ctx.surfaceIndirectDrawBuffer = &g.surfaceIndirectDrawBuffer;
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
            const RENDER3D::CpuRenderQueue& queue,
            RENDER3D::SCREENSPACE::ScreenSpaceGeometryAux& geometryAux,
            D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv) {
            (void)queue;
            if (!HasGpuDrivenPassSource(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque)) {
                return false;
            }

            const uint32_t width = static_cast<uint32_t>(std::max(1.0f, g.cameraMapped ? g.cameraMapped->screenParams.x : 1.0f));
            const uint32_t height = static_cast<uint32_t>(std::max(1.0f, g.cameraMapped ? g.cameraMapped->screenParams.y : 1.0f));
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
                    RENDER3D::MESHLET::MeshletPipelineKind::GeometryAux,
                    RENDER3D::CLUSTER::ClusterDrawPipelineKind::GeometryAux);
            geometryAux.EndNormalRoughnessPass(SERVICES::gCtx.cmdList);
            return backendResult.gpuBackendExecuted;
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
        g.cpuRenderQueue.Clear();
        g.frameObjectIndex = 0;
        g.materialDataFrameTable.Clear();
        g.gpuDrivenSceneSource.Reset();
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

    void SubmitSkinnedSubmesh(const ModelAsset& asset, const Transform3D& transform, const std::vector<MATH::Mat4>& jointPalette, uint32_t meshIndex, uint32_t primitiveIndex, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4(&materialFxParamValues)[VFX::kMaterialFxUserCount], bool materialFxValuesInitialized, bool receiveShadow, MeshRenderDebugMode renderDebugMode, const Material* materialOverride) {
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
        item.usePrimitiveFilter = true;
        item.meshIndexFilter = meshIndex;
        item.primitiveIndexFilter = primitiveIndex;
        item.receiveShadow = receiveShadow;
        item.renderDebugMode = renderDebugMode;
        ResolveDrawVariant(item);
        ++g.debugStats.skinnedDrawItemCount;
        if (renderDebugMode != MeshRenderDebugMode::Normal) {
            ++g.debugStats.wireDrawItemCount;
        }
        g.drawItems.push_back(std::move(item));
    }

    void SetGpuDrivenSceneSource(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source) {

        g.gpuDrivenSceneSource.Reset();
        if (source == nullptr) {
            return;
        }
        g.gpuDrivenSceneSource = *source;
    }

    bool HasSubmittedItems() {
        return !g.drawItems.empty() || HasGpuDrivenSceneSource();
    }

    bool BeginFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        RenderDebugView debugView) {
        if (!EnsureInitialized()) {
            return false;
        }
        if (!PrepareMeshFrame(camera, environment, debugView)) {
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
        if (HasGpuDrivenSceneSource()) {
            PrepareGpuDrivenFrameState();
        } else {
            ResetGpuDrivenFrameState();
        }
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
        uint32_t screenHeight,
        RenderDebugView debugView) {

        if (!EnsureInitialized()) {
            return false;
        }
        // Capture 用の固定解像度を camera constants に反映する。
        if (!PrepareMeshFrame(camera, environment, debugView, screenWidth, screenHeight)) {
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
        if (HasGpuDrivenSceneSource()) {
            PrepareGpuDrivenFrameState();
        } else {
            ResetGpuDrivenFrameState();
        }
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
        if (srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            cmd->SetDescriptorHeaps(1, heaps);
        }

        return true;
    }

    const RENDER3D::CpuRenderQueue& BuildCpuRenderQueue() {
        g.cpuRenderQueue.Clear();
        g.cpuRenderQueue.Build(g.drawItems);
        return g.cpuRenderQueue;
    }

    const CameraCB* GetCameraConstants() {
        return g.cameraMapped;
    }

    bool RenderGeometryAuxPass(
            const RENDER3D::CpuRenderQueue& queue,
        RENDER3D::SCREENSPACE::ScreenSpaceGeometryAux& geometryAux,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv) {
        return RenderGeometryAuxPassInternal(queue, geometryAux, sceneDsv);
    }

    bool RenderForwardOpaquePass(
        const RENDER3D::CpuRenderQueue& queue,
        const MeshPassResources& passResources) {
        (void)queue;
        if (!HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque)) {
            return true;
        }
        const GeometryBackendExecutionResult backendResult =
            ExecuteGeometryBackendPlan(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque,
                passResources,
                MeshDrawPassKind::Forward,
                RENDER3D::MESHLET::MeshletPipelineKind::ForwardOpaque,
                RENDER3D::CLUSTER::ClusterDrawPipelineKind::ForwardOpaque);
        return backendResult.gpuBackendExecuted;
    }

    bool RenderForwardTransparentPass(
        const RENDER3D::CpuRenderQueue& queue,
        const MeshPassResources& passResources) {
        (void)queue;
        if (!HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent)) {
            return true;
        }
        const GeometryBackendExecutionResult backendResult =
            ExecuteGeometryBackendPlan(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent,
                passResources,
                MeshDrawPassKind::Forward,
                RENDER3D::MESHLET::MeshletPipelineKind::ForwardTransparent,
                RENDER3D::CLUSTER::ClusterDrawPipelineKind::ForwardTransparent);
        return backendResult.gpuBackendExecuted;
    }

    bool HasDepthAwarePassWork(const RENDER3D::CpuRenderQueue& queue) {
        (void)queue;
        return HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware);
    }

    bool RenderDepthAwarePass(
        const RENDER3D::CpuRenderQueue& queue,
        const MeshPassResources& passResources) {
        (void)queue;
        if (!HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware)) {
            return true;
        }
        const GeometryBackendExecutionResult backendResult =
            ExecuteGeometryBackendPlan(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware,
                passResources,
                MeshDrawPassKind::Forward,
                RENDER3D::MESHLET::MeshletPipelineKind::ForwardDepthAware,
                RENDER3D::CLUSTER::ClusterDrawPipelineKind::ForwardDepthAware);
        return backendResult.gpuBackendExecuted;
    }

    void SetAmbientOcclusionRuntimeEnabled(bool enabled) {
        if (g.skyEnvironmentMapped != nullptr) {
            g.skyEnvironmentMapped->aoParams.x = enabled ? 1.0f : 0.0f;
        }
    }

    void EndFrame() {
        g.drawItems.clear();
        g.cpuRenderQueue.Clear();
        g.frameObjectIndex = 0;
        g.gpuDrivenSceneSource.Reset();
    }

    void RenderAll(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        RenderDebugView debugView) {
        (void)RENDER3D::PIPELINE::RenderMeshLightingFrame(camera, environment, debugView);
    }

    const MeshRendererDebugStats& GetDebugStats() {
        const MaterialFxProfileCacheStats fxCacheStats = MaterialFxProfile::GetCacheStats();
        g.debugStats.materialFxProfileCacheHitCount = fxCacheStats.hitCount;
        g.debugStats.materialFxProfileCacheMissCount = fxCacheStats.missCount;
        g.debugStats.materialFxProfileCacheFailCount = fxCacheStats.failCount;
        return g.debugStats;
    }

} // namespace HIKARI::MESHRENDERER
