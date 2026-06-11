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
#include "Render3D/Cluster/HIKARI_ClusterMainline.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Render3D/Pipeline/HIKARI_RenderQueue.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPacket.h"
#include "Render3D/ScreenSpace/HIKARI_SceneGeometryBuffer.h"
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

        RENDER3D::CLUSTER::ClusterMainlinePass ToClusterMainlinePass(
            MeshDrawPassKind passKind) {

            return passKind == MeshDrawPassKind::GeometryBuffer
                ? RENDER3D::CLUSTER::ClusterMainlinePass::GeometryAux
                : RENDER3D::CLUSTER::ClusterMainlinePass::ForwardOpaque;
        }

        RENDER3D::CLUSTER::ClusterMainlinePolicy ResolveOpaqueMainlinePolicy();

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
                DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Surface indirect draw buffer initialization failed. Direct draw path will be used.");
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
                GetStaticRootSignature(g.pipelines))) {
                DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Cluster draw executor initialization failed. Cluster geometry will keep using SurfacePacket fallback.");
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

        MeshDrawContext BuildDrawContext(
            bool depthAwarePhase,
            MeshDrawPassKind passKind,
            const MeshPassResources& passResources);

        void UploadSurfaceGpuSceneFrame() {
            g.surfaceGpuSceneBuffer.ResetFrame();

            g.debugStats.surfaceGpuSceneOpaqueInstanceCount =
                g.surfacePacketOpaqueGpuSceneInstances != nullptr
                    ? g.surfacePacketOpaqueGpuSceneInstances->size()
                    : 0;
            g.debugStats.surfaceGpuSceneDepthAwareInstanceCount =
                g.surfacePacketDepthAwareGpuSceneInstances != nullptr
                    ? g.surfacePacketDepthAwareGpuSceneInstances->size()
                    : 0;
            g.debugStats.surfaceGpuSceneTransparentInstanceCount =
                g.surfacePacketTransparentGpuSceneInstances != nullptr
                    ? g.surfacePacketTransparentGpuSceneInstances->size()
                    : 0;

            if (g.surfacePacketOpaqueGpuSceneInstances != nullptr) {
                g.surfaceGpuSceneBuffer.Upload(*g.surfacePacketOpaqueGpuSceneInstances);
            }
            if (g.surfacePacketDepthAwareGpuSceneInstances != nullptr) {
                g.surfaceGpuSceneBuffer.Upload(*g.surfacePacketDepthAwareGpuSceneInstances);
            }
            if (g.surfacePacketTransparentGpuSceneInstances != nullptr) {
                g.surfaceGpuSceneBuffer.Upload(*g.surfacePacketTransparentGpuSceneInstances);
            }

            const RENDER3D::CORE::SurfaceGpuSceneFrameBufferStats& gpuSceneStats =
                g.surfaceGpuSceneBuffer.GetStats();
            g.debugStats.surfaceGpuSceneCapacity = gpuSceneStats.capacity;
            g.debugStats.surfaceGpuSceneRequestedInstanceCount = gpuSceneStats.requestedInstanceCount;
            g.debugStats.surfaceGpuSceneUploadedInstanceCount = gpuSceneStats.uploadedInstanceCount;
            g.debugStats.surfaceGpuSceneOverflowInstanceCount = gpuSceneStats.overflowInstanceCount;
            g.debugStats.surfaceGpuSceneUploadCallCount = gpuSceneStats.uploadCallCount;
            g.debugStats.surfaceGpuSceneSrvValid = gpuSceneStats.srv.ptr != 0;
            g.debugStats.surfaceGpuSceneBufferReady = gpuSceneStats.initialized;
        }

        void PrepareSurfaceGpuSceneMaterialFrame() {
            if (g.surfacePacketBuilder == nullptr) {
                return;
            }

            const std::vector<RENDER3D::RUNTIME::SurfaceDrawPacket>& packets =
                g.surfacePacketBuilder->GetPackets();
            MeshDrawContext drawCtx = BuildDrawContext(false, MeshDrawPassKind::Forward, {});
            drawCtx.surfaceGpuSceneBaseOffset = 0;
            if (g.surfacePacketOpaqueExecutionIndices != nullptr &&
                g.surfacePacketOpaqueExecutionCommands != nullptr) {
                PrepareSurfacePacketGpuSceneMaterials(
                    drawCtx,
                    packets.data(),
                    packets.size(),
                    g.surfacePacketOpaqueExecutionIndices->data(),
                    g.surfacePacketOpaqueExecutionIndices->size(),
                    g.surfacePacketOpaqueExecutionCommands->data(),
                    g.surfacePacketOpaqueExecutionCommands->size());
            }

            drawCtx.surfaceGpuSceneBaseOffset = g.debugStats.surfaceGpuSceneOpaqueInstanceCount;
            if (g.surfacePacketDepthAwareExecutionIndices != nullptr &&
                g.surfacePacketDepthAwareExecutionCommands != nullptr) {
                PrepareSurfacePacketGpuSceneMaterials(
                    drawCtx,
                    packets.data(),
                    packets.size(),
                    g.surfacePacketDepthAwareExecutionIndices->data(),
                    g.surfacePacketDepthAwareExecutionIndices->size(),
                    g.surfacePacketDepthAwareExecutionCommands->data(),
                    g.surfacePacketDepthAwareExecutionCommands->size());
            }

            drawCtx.surfaceGpuSceneBaseOffset =
                g.debugStats.surfaceGpuSceneOpaqueInstanceCount +
                g.debugStats.surfaceGpuSceneDepthAwareInstanceCount;
            if (g.surfacePacketTransparentExecutionIndices != nullptr &&
                g.surfacePacketTransparentExecutionCommands != nullptr) {
                PrepareSurfacePacketGpuSceneMaterials(
                    drawCtx,
                    packets.data(),
                    packets.size(),
                    g.surfacePacketTransparentExecutionIndices->data(),
                    g.surfacePacketTransparentExecutionIndices->size(),
                    g.surfacePacketTransparentExecutionCommands->data(),
                    g.surfacePacketTransparentExecutionCommands->size());
            }
        }

        void UploadSurfaceIndirectDrawFrame() {
            g.surfaceIndirectDrawBuffer.ResetFrame();

            if (g.surfacePacketOpaqueExecutionCommands != nullptr) {
                const RENDER3D::CLUSTER::ClusterMainlineCommandContext clusterFilter{
                    ResolveOpaqueMainlinePolicy(),
                    &g.staticOpaqueClusterMainlineFrame,
                    true,
                    ToClusterMainlinePass(MeshDrawPassKind::Forward),
                    g.surfacePacketOpaqueGpuSceneInstances,
                    &g.surfaceGpuSceneBuffer,
                    0u
                };
                g.surfaceIndirectDrawBuffer.UploadSurfaceCommands(
                    *g.surfacePacketOpaqueExecutionCommands,
                    0u,
                    RENDER3D::CLUSTER::ShouldUploadSurfaceIndirectCommand,
                    &clusterFilter);
            }
            if (g.surfacePacketDepthAwareExecutionCommands != nullptr) {
                g.surfaceIndirectDrawBuffer.UploadSurfaceCommands(
                    *g.surfacePacketDepthAwareExecutionCommands,
                    static_cast<uint32_t>(g.debugStats.surfaceGpuSceneOpaqueInstanceCount));
            }
            if (g.surfacePacketTransparentExecutionCommands != nullptr) {
                g.surfaceIndirectDrawBuffer.UploadSurfaceCommands(
                    *g.surfacePacketTransparentExecutionCommands,
                    static_cast<uint32_t>(
                        g.debugStats.surfaceGpuSceneOpaqueInstanceCount +
                        g.debugStats.surfaceGpuSceneDepthAwareInstanceCount));
            }
            const RENDER3D::CORE::SurfaceIndirectDrawBufferStats& indirectStats =
                g.surfaceIndirectDrawBuffer.GetStats();
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

        void DispatchClusterGpuCullingFrame() {
            std::vector<RENDER3D::CLUSTER::ClusterGpuCullingSourceRange> ranges{};

            if (g.surfacePacketOpaqueGpuSceneInstances != nullptr &&
                !g.surfacePacketOpaqueGpuSceneInstances->empty()) {
                const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>& instances =
                    *g.surfacePacketOpaqueGpuSceneInstances;
                uint32_t singleSidedInstanceCount = 0;
                uint32_t doubleSidedInstanceCount = 0;
                constexpr uint32_t doubleSidedFlag =
                    static_cast<uint32_t>(
                        RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::DoubleSided);
                for (const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance : instances) {
                    if ((instance.flags & doubleSidedFlag) != 0u) {
                        ++doubleSidedInstanceCount;
                    }
                    else {
                        ++singleSidedInstanceCount;
                    }
                }
                // Cluster cull は CPU command ではなく、opaque GPU scene の instance range を入口にする。
                ranges.push_back({
                    0u,
                    static_cast<uint32_t>(instances.size()),
                    RENDER3D::CLUSTER::ClusterGpuCullingPassKind::ForwardOpaque,
                    singleSidedInstanceCount,
                    doubleSidedInstanceCount
                });
            }

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
            g.clusterGpuCullingPass.Dispatch(
                SERVICES::gCtx.cmdList,
                viewProj,
                cameraPosition,
                ResolveClusterGeometryPoolSrv(),
                g.surfaceGpuSceneBuffer.GetGpuVirtualAddress(),
                ranges.empty() ? nullptr : ranges.data(),
                ranges.size());

            const RENDER3D::CLUSTER::ClusterGpuCullingPassStats& clusterCullStats =
                g.clusterGpuCullingPass.GetStats();
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
            g.debugStats.clusterGpuCullSourceSingleSidedInstanceCount =
                clusterCullStats.sourceSingleSidedInstanceCount;
            g.debugStats.clusterGpuCullSourceDoubleSidedInstanceCount =
                clusterCullStats.sourceDoubleSidedInstanceCount;
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
            g.debugStats.clusterDrawGeometryBufferSubmittedCount =
                clusterDrawStats.geometryBufferSubmittedDrawCount;
            g.debugStats.clusterDrawForwardSubmitCallCount =
                clusterDrawStats.forwardSubmitCallCount;
            g.debugStats.clusterDrawGeometryBufferSubmitCallCount =
                clusterDrawStats.geometryBufferSubmitCallCount;
            g.debugStats.clusterDrawBackFaceSubmitCallCount =
                clusterDrawStats.backFaceSubmitCallCount;
            g.debugStats.clusterDrawDoubleSidedSubmitCallCount =
                clusterDrawStats.doubleSidedSubmitCallCount;
            g.debugStats.clusterDrawPipelineReady =
                clusterDrawStats.drawPipelineReady;
            g.debugStats.clusterDrawForwardPipelineReady =
                clusterDrawStats.forwardPipelineReady;
            g.debugStats.clusterDrawGeometryBufferPipelineReady =
                clusterDrawStats.geometryBufferPipelineReady;
            g.debugStats.clusterDrawArgumentBufferReady =
                clusterDrawStats.drawArgumentBufferReady;
            g.debugStats.clusterDrawCommandSignatureReady =
                clusterDrawStats.drawCommandSignatureReady;
        }

        RENDER3D::CLUSTER::ClusterMainlineSignals BuildClusterMainlineSignals() {
            RENDER3D::CLUSTER::ClusterMainlineSignals signals{};
            signals.gpuCullReady = g.debugStats.clusterGpuCullReady;
            signals.drawArgsReady = g.debugStats.clusterGpuCullDrawArgsReady;
            signals.commandSignatureReady = g.debugStats.clusterGpuCullCommandSignatureReady;
            signals.forwardPipelineReady = g.debugStats.clusterDrawForwardPipelineReady;
            signals.geometryAuxPipelineReady = g.debugStats.clusterDrawGeometryBufferPipelineReady;
            signals.candidateInstanceCount = g.debugStats.clusterGpuCullCandidateInstanceCount;
            signals.drawSeedCount = g.debugStats.clusterGpuCullDrawSeedCount;
            signals.overflowInstanceCount = g.debugStats.clusterGpuCullOverflowInstanceCount;
            return signals;
        }

        RENDER3D::CLUSTER::ClusterMainlinePolicy ResolveOpaqueMainlinePolicy() {
            return RENDER3D::CLUSTER::ResolveClusterMainlinePolicy(
                BuildClusterMainlineSignals());
        }

        void UpdateClusterMainlineDebugStats() {
            const RENDER3D::CLUSTER::ClusterMainlineState state =
                RENDER3D::CLUSTER::ResolveClusterMainlineState(
                    BuildClusterMainlineSignals());
            g.debugStats.clusterMainlineReady = state.forwardReady;
            g.debugStats.clusterMainlineForwardReady = state.forwardReady;
            g.debugStats.clusterMainlineGeometryBufferReady = state.geometryAuxReady;
            g.debugStats.clusterMainlineHasDrawSeeds = state.hasDrawSeeds;
            g.debugStats.clusterMainlineOverflowBlocked = state.overflowBlocked;
        }

        void BuildStaticOpaqueClusterMainlineFrame() {
            RENDER3D::CLUSTER::BuildStaticOpaqueFrame(
                ResolveOpaqueMainlinePolicy(),
                g.surfacePacketOpaqueGpuSceneInstances,
                &g.surfaceGpuSceneBuffer,
                0u,
                g.staticOpaqueClusterMainlineFrame);
        }

        void ApplyClusterMainlineOwnershipDebugStats(
            const RENDER3D::CLUSTER::ClusterMainlineOwnershipStats& stats) {

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

        void UpdateOpaqueMainlineOwnershipDebugStats() {
            const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* commands =
                g.surfacePacketOpaqueExecutionCommands;
            const RENDER3D::CLUSTER::ClusterMainlineOwnershipStats stats =
                RENDER3D::CLUSTER::BuildOpaqueOwnershipStats(
                    g.staticOpaqueClusterMainlineFrame,
                    commands != nullptr ? commands->data() : nullptr,
                    commands != nullptr ? commands->size() : 0u);
            ApplyClusterMainlineOwnershipDebugStats(stats);
        }

        bool IsClusterMainlinePreparedForPass(MeshDrawPassKind passKind) {
            return ResolveOpaqueMainlinePolicy().OwnsPass(
                ToClusterMainlinePass(passKind));
        }

        bool ExecuteClusterDrawFrame(
            const MeshPassResources& passResources,
            MeshDrawPassKind passKind,
            RENDER3D::CLUSTER::ClusterDrawPipelineKind pipelineKind) {
            if (!IsClusterMainlinePreparedForPass(passKind)) {
                return false;
            }

            MeshBindingStateCache bindingCache{};
            MeshDrawContext drawCtx = BuildDrawContext(false, passKind, passResources);
            drawCtx.binding.cache = &bindingCache;
            BindSurfacePacketFrameResources(drawCtx);

            RENDER3D::CLUSTER::ClusterDrawExecutionContext ctx{};
            ctx.commandList = SERVICES::gCtx.cmdList;
            ctx.cullingPass = &g.clusterGpuCullingPass;
            ctx.pipelineKind = pipelineKind;
            const bool executed = g.clusterDrawExecutor.Execute(ctx);
            UpdateClusterDrawDebugStats();
            UpdateClusterMainlineDebugStats();
            return executed;
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

        enum class SurfacePacketExecutionKind {
            Opaque,
            DepthAware,
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

        bool HasSurfacePacketDepthAwareExecutionPlan() {
            return HasSurfacePacketExecutionPlan(
                g.surfacePacketDepthAwareExecutionIndices,
                g.surfacePacketDepthAwareExecutionCommands);
        }

        bool HasAnySurfacePacketExecutionPlan() {
            return HasSurfacePacketOpaqueExecutionPlan() ||
                HasSurfacePacketDepthAwareExecutionPlan() ||
                HasSurfacePacketTransparentExecutionPlan();
        }

        void RecordSurfacePacketExecutorCommandStats(
            SurfacePacketExecutionKind executionKind,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command) {

            ++g.debugStats.surfacePacketExecutorCommandCount;
            if (executionKind == SurfacePacketExecutionKind::DepthAware) {
                ++g.debugStats.surfacePacketExecutorDepthAwareCommandCount;
            } else if (executionKind == SurfacePacketExecutionKind::Transparent) {
                ++g.debugStats.surfacePacketExecutorTransparentCommandCount;
            } else {
                ++g.debugStats.surfacePacketExecutorOpaqueCommandCount;
            }
            if (command.singlePacket) {
                ++g.debugStats.surfacePacketExecutorSinglePacketCommandCount;
            } else {
                ++g.debugStats.surfacePacketExecutorMergedCommandCount;
                if (command.packetCount > 1) {
                    g.debugStats.surfacePacketExecutorSavedCommandCount +=
                        static_cast<size_t>(command.packetCount - 1);
                }
            }
            if (command.drawArgsValid) {
                ++g.debugStats.surfacePacketExecutorIndirectReadyCommandCount;
            } else {
                ++g.debugStats.surfacePacketExecutorMissingDrawArgsCommandCount;
            }
            g.debugStats.surfacePacketExecutorMaxCommandPacketCount =
                (std::max)(
                    g.debugStats.surfacePacketExecutorMaxCommandPacketCount,
                    static_cast<size_t>(command.packetCount));
        }

        void RecordSurfacePacketExecutorDrawStats(
            SurfacePacketExecutionKind executionKind,
            MeshDrawPassKind passKind,
            const SurfacePacketCommandDrawResult& result) {

            g.debugStats.surfacePacketExecutorPacketCount += result.submittedPacketCount;
            g.debugStats.surfacePacketExecutorSkippedPacketCount += result.skippedPacketCount;
            if (passKind == MeshDrawPassKind::GeometryBuffer) {
                g.debugStats.surfacePacketExecutorGeometryDrawCount += result.drawCallCount;
            } else {
                g.debugStats.surfacePacketExecutorForwardDrawCount += result.drawCallCount;
                if (executionKind == SurfacePacketExecutionKind::DepthAware) {
                    g.debugStats.surfacePacketExecutorDepthAwareDrawCount += result.drawCallCount;
                } else if (executionKind == SurfacePacketExecutionKind::Transparent) {
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

        bool RenderSurfacePacketPlan(
            SurfacePacketExecutionKind executionKind,
            MeshDrawPassKind passKind,
            size_t& objectIndex,
            const MeshPassResources& passResources) {
            const std::vector<uint32_t>* executableIndices = g.surfacePacketOpaqueExecutionIndices;
            const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* executableCommands =
                g.surfacePacketOpaqueExecutionCommands;
            if (executionKind == SurfacePacketExecutionKind::DepthAware) {
                executableIndices = g.surfacePacketDepthAwareExecutionIndices;
                executableCommands = g.surfacePacketDepthAwareExecutionCommands;
            } else if (executionKind == SurfacePacketExecutionKind::Transparent) {
                executableIndices = g.surfacePacketTransparentExecutionIndices;
                executableCommands = g.surfacePacketTransparentExecutionCommands;
            }
            if (!HasSurfacePacketExecutionPlan(executableIndices, executableCommands)) {
                return true;
            }

            const char* eventName = "SurfacePacketExecutor.ForwardOpaque";
            if (passKind == MeshDrawPassKind::GeometryBuffer) {
                eventName = "SurfacePacketExecutor.GeometryBuffer";
            } else if (executionKind == SurfacePacketExecutionKind::DepthAware) {
                eventName = "SurfacePacketExecutor.ForwardDepthAware";
            } else if (executionKind == SurfacePacketExecutionKind::Transparent) {
                eventName = "SurfacePacketExecutor.ForwardTransparent";
            }
            GFX::PIX::ScopedGpuEvent pixPhase(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, eventName);

            MeshBindingStateCache bindingCache{};
            const bool depthAwarePhase = executionKind == SurfacePacketExecutionKind::DepthAware;
            MeshDrawContext drawCtx = BuildDrawContext(depthAwarePhase, passKind, passResources);
            drawCtx.binding.cache = &bindingCache;
            // GPU Scene buffer は Opaque -> DepthAware -> Transparent の順で連続配置する。
            drawCtx.surfaceGpuSceneBaseOffset = 0;
            if (executionKind == SurfacePacketExecutionKind::DepthAware) {
                drawCtx.surfaceGpuSceneBaseOffset = g.debugStats.surfaceGpuSceneOpaqueInstanceCount;
            } else if (executionKind == SurfacePacketExecutionKind::Transparent) {
                drawCtx.surfaceGpuSceneBaseOffset =
                    g.debugStats.surfaceGpuSceneOpaqueInstanceCount +
                    g.debugStats.surfaceGpuSceneDepthAwareInstanceCount;
            }
            BindSurfacePacketFrameResources(drawCtx);
            const std::vector<RENDER3D::RUNTIME::SurfaceDrawPacket>& packets =
                g.surfacePacketBuilder->GetPackets();
            const RENDER3D::CLUSTER::ClusterMainlineCommandContext clusterFilter{
                ResolveOpaqueMainlinePolicy(),
                &g.staticOpaqueClusterMainlineFrame,
                executionKind == SurfacePacketExecutionKind::Opaque,
                ToClusterMainlinePass(passKind),
                g.surfacePacketOpaqueGpuSceneInstances,
                &g.surfaceGpuSceneBuffer,
                drawCtx.surfaceGpuSceneBaseOffset
            };
            drawCtx.surfaceIndirectCommandFilter =
                RENDER3D::CLUSTER::ShouldUploadSurfaceIndirectCommand;
            drawCtx.surfaceIndirectCommandFilterUserData = &clusterFilter;
            if ((passKind == MeshDrawPassKind::Forward ||
                passKind == MeshDrawPassKind::GeometryBuffer) &&
                PrepareSurfacePacketIndirectDrawBindings(
                    drawCtx,
                    packets.data(),
                    packets.size(),
                    executableIndices->data(),
                    executableIndices->size(),
                    executableCommands->data(),
                    executableCommands->size())) {
                g.surfaceIndirectDrawBuffer.FlushToGpu(SERVICES::gCtx.cmdList);
                const RENDER3D::CORE::SurfaceIndirectDrawBufferStats& indirectStats =
                    g.surfaceIndirectDrawBuffer.GetStats();
                g.debugStats.surfaceIndirectDrawBindingPatchCount =
                    indirectStats.drawBindingPatchCount;
            }

            const bool useIndirectCommandRange =
                executionKind != SurfacePacketExecutionKind::Transparent &&
                (passKind == MeshDrawPassKind::Forward ||
                    passKind == MeshDrawPassKind::GeometryBuffer);
            size_t commandIndex = 0;
            while (commandIndex < executableCommands->size()) {
                const RENDER3D::RUNTIME::SurfaceDrawCommand& command =
                    (*executableCommands)[commandIndex];
                if (passKind == MeshDrawPassKind::GeometryBuffer && command.transparent) {
                    ++commandIndex;
                    continue;
                }
                if (RENDER3D::CLUSTER::ShouldOwnSurfaceCommand(
                    clusterFilter,
                    command)) {
                    ++commandIndex;
                    continue;
                }

                const size_t firstCommandIndex = commandIndex;
                SurfacePacketCommandDrawResult result{};
                if (useIndirectCommandRange) {
                    result = DrawSurfacePacketCommandRange(
                        drawCtx,
                        packets.data(),
                        packets.size(),
                        executableIndices->data(),
                        executableIndices->size(),
                        executableCommands->data(),
                        executableCommands->size(),
                        commandIndex,
                        objectIndex);
                } else {
                    result = DrawSurfacePacketCommand(
                        drawCtx,
                        packets.data(),
                        packets.size(),
                        executableIndices->data(),
                        executableIndices->size(),
                        command,
                        objectIndex);
                    ++commandIndex;
                }

                if (commandIndex <= firstCommandIndex) {
                    commandIndex = firstCommandIndex + 1;
                }

                for (size_t statsCommandIndex = firstCommandIndex;
                    statsCommandIndex < commandIndex && statsCommandIndex < executableCommands->size();
                    ++statsCommandIndex) {
                    RecordSurfacePacketExecutorCommandStats(
                        executionKind,
                        (*executableCommands)[statsCommandIndex]);
                }
                RecordSurfacePacketExecutorDrawStats(executionKind, passKind, result);
            }
            return true;
        }

        bool RenderMeshPhase(
            const RENDER3D::RenderQueue& queue,
            RENDER3D::RenderPhase phase,
            MeshDrawPassKind passKind,
            size_t& objectIndex,
            const MeshPassResources& passResources) {
            const bool depthAwarePhase = phase == RENDER3D::RenderPhase::DepthAware;
            const bool transparentPhase = phase == RENDER3D::RenderPhase::Transparent;
            const char* eventName = passKind == MeshDrawPassKind::GeometryBuffer
                ? "MeshRenderer.GeometryBuffer"
                : (depthAwarePhase ? "MeshRenderer.DepthAware" :
                    (transparentPhase ? "MeshRenderer.Transparent" : "MeshRenderer.Opaque"));
            GFX::PIX::ScopedGpuEvent pixPhase(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, eventName);
            MeshBindingStateCache bindingCache{};
            MeshDrawContext drawCtx = BuildDrawContext(depthAwarePhase, passKind, passResources);
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
            RENDER3D::SCREENSPACE::SceneGeometryBuffer& geometryBuffer,
            D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv) {
            if (!queue.HasPhase(RENDER3D::RenderPhase::Opaque) && !HasSurfacePacketOpaqueExecutionPlan()) {
                return false;
            }

            const uint32_t width = static_cast<uint32_t>(std::max(1.0f, g.cameraMapped ? g.cameraMapped->screenParams.x : 1.0f));
            const uint32_t height = static_cast<uint32_t>(std::max(1.0f, g.cameraMapped ? g.cameraMapped->screenParams.y : 1.0f));
            if (!geometryBuffer.EnsureSize(width, height)) {
                return false;
            }

            if (sceneDsv.ptr == 0) {
                return false;
            }

            GFX::PIX::ScopedGpuEvent pixGeometry(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, "SceneGeometryBuffer");
            geometryBuffer.BeginNormalRoughnessPass(SERVICES::gCtx.cmdList, sceneDsv);
            size_t geometryObjectIndex = 0;
            MeshPassResources passResources{};
            ExecuteClusterDrawFrame(
                passResources,
                MeshDrawPassKind::GeometryBuffer,
                RENDER3D::CLUSTER::ClusterDrawPipelineKind::GeometryBuffer);
            const bool packetOk = RenderSurfacePacketPlan(
                SurfacePacketExecutionKind::Opaque,
                MeshDrawPassKind::GeometryBuffer,
                geometryObjectIndex,
                passResources);
            const bool queueOk = packetOk && RenderMeshPhase(
                queue,
                RENDER3D::RenderPhase::Opaque,
                MeshDrawPassKind::GeometryBuffer,
                geometryObjectIndex,
                passResources);
            geometryBuffer.EndNormalRoughnessPass(SERVICES::gCtx.cmdList);
            return packetOk && queueOk;
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
        g.surfacePacketOpaqueGpuSceneInstances = nullptr;
        g.surfacePacketDepthAwareExecutionIndices = nullptr;
        g.surfacePacketDepthAwareExecutionCommands = nullptr;
        g.surfacePacketDepthAwareGpuSceneInstances = nullptr;
        g.surfacePacketTransparentExecutionIndices = nullptr;
        g.surfacePacketTransparentExecutionCommands = nullptr;
        g.surfacePacketTransparentGpuSceneInstances = nullptr;
        g.staticOpaqueClusterMainlineFrame.Reset();
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
        const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>* opaqueGpuSceneInstances,
        const std::vector<uint32_t>* depthAwareExecutablePacketIndices,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* depthAwareExecutableCommands,
        const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>* depthAwareGpuSceneInstances,
        const std::vector<uint32_t>* transparentExecutablePacketIndices,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* transparentExecutableCommands,
        const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>* transparentGpuSceneInstances) {
        g.surfacePacketBuilder = builder;
        g.surfacePacketOpaqueExecutionIndices = opaqueExecutablePacketIndices;
        g.surfacePacketOpaqueExecutionCommands = opaqueExecutableCommands;
        g.surfacePacketOpaqueGpuSceneInstances = opaqueGpuSceneInstances;
        g.surfacePacketDepthAwareExecutionIndices = depthAwareExecutablePacketIndices;
        g.surfacePacketDepthAwareExecutionCommands = depthAwareExecutableCommands;
        g.surfacePacketDepthAwareGpuSceneInstances = depthAwareGpuSceneInstances;
        g.surfacePacketTransparentExecutionIndices = transparentExecutablePacketIndices;
        g.surfacePacketTransparentExecutionCommands = transparentExecutableCommands;
        g.surfacePacketTransparentGpuSceneInstances = transparentGpuSceneInstances;
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
        UploadSurfaceGpuSceneFrame();
        PrepareSurfaceGpuSceneMaterialFrame();
        DispatchClusterGpuCullingFrame();
        g.clusterDrawExecutor.ResetFrame();
        UpdateClusterDrawDebugStats();
        UpdateClusterMainlineDebugStats();
        BuildStaticOpaqueClusterMainlineFrame();
        UpdateOpaqueMainlineOwnershipDebugStats();
        UploadSurfaceIndirectDrawFrame();
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
        // Capture 用の固定解像度を camera constants に反映する。
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
        UploadSurfaceGpuSceneFrame();
        PrepareSurfaceGpuSceneMaterialFrame();
        DispatchClusterGpuCullingFrame();
        g.clusterDrawExecutor.ResetFrame();
        UpdateClusterDrawDebugStats();
        UpdateClusterMainlineDebugStats();
        BuildStaticOpaqueClusterMainlineFrame();
        UpdateOpaqueMainlineOwnershipDebugStats();
        UploadSurfaceIndirectDrawFrame();
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
        RENDER3D::SCREENSPACE::SceneGeometryBuffer& geometryBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv) {
        return RenderGeometryBufferPassInternal(queue, geometryBuffer, sceneDsv);
    }

    bool RenderForwardOpaquePass(
        const RENDER3D::RenderQueue& queue,
        const MeshPassResources& passResources) {
        ExecuteClusterDrawFrame(
            passResources,
            MeshDrawPassKind::Forward,
            RENDER3D::CLUSTER::ClusterDrawPipelineKind::ForwardOpaque);
        // SurfacePacket は queue を経由せず、先に opaque plan を直接実行する。
        const bool packetOk = RenderSurfacePacketPlan(
            SurfacePacketExecutionKind::Opaque,
            MeshDrawPassKind::Forward,
            g.frameObjectIndex,
            passResources);
        const bool queueOk = packetOk && RenderMeshPhase(
            queue,
            RENDER3D::RenderPhase::Opaque,
            MeshDrawPassKind::Forward,
            g.frameObjectIndex,
            passResources);
        return packetOk && queueOk;
    }

    bool RenderForwardTransparentPass(
        const RENDER3D::RenderQueue& queue,
        const MeshPassResources& passResources) {
        // Transparent は独立 plan として、opaque/depth-aware の後に実行する。
        const bool packetOk = RenderSurfacePacketPlan(
            SurfacePacketExecutionKind::Transparent,
            MeshDrawPassKind::Forward,
            g.frameObjectIndex,
            passResources);
        const bool queueOk = packetOk && RenderMeshPhase(
            queue,
            RENDER3D::RenderPhase::Transparent,
            MeshDrawPassKind::Forward,
            g.frameObjectIndex,
            passResources);
        return packetOk && queueOk;
    }

    bool HasDepthAwarePassWork(const RENDER3D::RenderQueue& queue) {
        return queue.HasPhase(RENDER3D::RenderPhase::DepthAware) ||
            HasSurfacePacketDepthAwareExecutionPlan();
    }

    bool RenderDepthAwarePass(
        const RENDER3D::RenderQueue& queue,
        const MeshPassResources& passResources) {
        const bool packetOk = RenderSurfacePacketPlan(
            SurfacePacketExecutionKind::DepthAware,
            MeshDrawPassKind::Forward,
            g.frameObjectIndex,
            passResources);
        const bool queueOk = packetOk && RenderMeshPhase(
            queue,
            RENDER3D::RenderPhase::DepthAware,
            MeshDrawPassKind::Forward,
            g.frameObjectIndex,
            passResources);
        return packetOk && queueOk;
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
        g.surfacePacketOpaqueGpuSceneInstances = nullptr;
        g.surfacePacketDepthAwareExecutionIndices = nullptr;
        g.surfacePacketDepthAwareExecutionCommands = nullptr;
        g.surfacePacketDepthAwareGpuSceneInstances = nullptr;
        g.surfacePacketTransparentExecutionIndices = nullptr;
        g.surfacePacketTransparentExecutionCommands = nullptr;
        g.surfacePacketTransparentGpuSceneInstances = nullptr;
        g.staticOpaqueClusterMainlineFrame.Reset();
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
