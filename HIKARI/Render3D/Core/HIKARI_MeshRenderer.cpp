#include "HIKARI_MeshRenderer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
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

        MATH::Mat4 ResolveGpuDrivenCullingViewProj() {
            return g.cullingCameraMapped != nullptr
                ? g.cullingCameraMapped->viewProj
                : MATH::Mat4::Identity();
        }

        MATH::Vec3 ResolveGpuDrivenCullingCameraPosition() {
            return g.cullingCameraMapped != nullptr
                ? MATH::Vec3{
                    g.cullingCameraMapped->cameraPos.x,
                    g.cullingCameraMapped->cameraPos.y,
                    g.cullingCameraMapped->cameraPos.z
                }
                : MATH::Vec3{};
        }

        D3D12_GPU_VIRTUAL_ADDRESS ResolveCameraAddressForPass(MeshDrawPassKind passKind) {
            if (passKind == MeshDrawPassKind::DepthPrepass) {
                return g.cameraCB != nullptr ? g.cameraCB->GetGPUVirtualAddress() : 0;
            }
            return g.cameraCB != nullptr ? g.cameraCB->GetGPUVirtualAddress() : 0;
        }

        D3D12_GPU_VIRTUAL_ADDRESS ResolveCullingCameraAddress() {
            return g.cullingCameraCB != nullptr
                ? g.cullingCameraCB->GetGPUVirtualAddress()
                : (g.cameraCB != nullptr ? g.cameraCB->GetGPUVirtualAddress() : 0);
        }

        struct OwnedTraditionalIndirectStream {
            std::vector<RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord> records{};
            std::vector<uint32_t> executableRecordIndices{};
            std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand> commands{};
            std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance> instances{};
            std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource> materialSources{};
            std::vector<std::vector<MATH::Mat4>> jointPalettes{};
            std::vector<VFX::VariantKey> bucketVariants{};
            uint32_t gpuSceneBaseIndex = 0;
            uint32_t gpuSceneInstanceCount = 0;
            uint32_t staticCommandCount = 0;
            uint32_t skinnedCommandCount = 0;

            void Clear() {
                records.clear();
                executableRecordIndices.clear();
                commands.clear();
                instances.clear();
                materialSources.clear();
                jointPalettes.clear();
                bucketVariants.clear();
                gpuSceneBaseIndex = 0;
                gpuSceneInstanceCount = 0;
                staticCommandCount = 0;
                skinnedCommandCount = 0;
            }

            bool CopyFrom(
                const RENDER3D::GPUDRIVEN::GpuDrivenTraditionalIndirectView& view) {

                Clear();
                if (!view.HasCommands()) {
                    return false;
                }
                records = *view.records;
                executableRecordIndices = *view.executableRecordIndices;
                commands = *view.commands;
                if (view.instances != nullptr) {
                    instances = *view.instances;
                }
                if (view.materialSources != nullptr) {
                    materialSources = *view.materialSources;
                }
                if (view.jointPalettes != nullptr) {
                    jointPalettes = *view.jointPalettes;
                }
                if (view.bucketVariants != nullptr) {
                    bucketVariants = *view.bucketVariants;
                }
                gpuSceneBaseIndex = view.gpuSceneBaseIndex;
                gpuSceneInstanceCount = view.gpuSceneInstanceCount;
                staticCommandCount = view.staticCommandCount;
                skinnedCommandCount = view.skinnedCommandCount;
                return true;
            }

            void AttachTo(RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass) const {
                pass.traditionalIndirect.Reset();
                if (commands.empty() || records.empty() || executableRecordIndices.empty()) {
                    return;
                }
                pass.traditionalIndirect.records = &records;
                pass.traditionalIndirect.executableRecordIndices =
                    &executableRecordIndices;
                pass.traditionalIndirect.commands = &commands;
                pass.traditionalIndirect.instances =
                    instances.empty() ? nullptr : &instances;
                pass.traditionalIndirect.materialSources =
                    materialSources.empty() ? nullptr : &materialSources;
                pass.traditionalIndirect.jointPalettes =
                    jointPalettes.empty() ? nullptr : &jointPalettes;
                pass.traditionalIndirect.bucketVariants =
                    bucketVariants.empty() ? nullptr : &bucketVariants;
                pass.traditionalIndirect.gpuSceneBaseIndex = gpuSceneBaseIndex;
                pass.traditionalIndirect.gpuSceneInstanceCount =
                    gpuSceneInstanceCount;
                pass.traditionalIndirect.staticCommandCount =
                    staticCommandCount;
                pass.traditionalIndirect.skinnedCommandCount =
                    skinnedCommandCount;
            }
        };

        std::array<
            OwnedTraditionalIndirectStream,
            RENDER3D::GPUDRIVEN::kGpuDrivenPassCount>
            gOwnedTraditionalIndirectStreams{};
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

        const MeshPrimitive* ResolveTraditionalRecordPrimitive(
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record) {

            if (record.model == nullptr ||
                record.meshIndex >= record.model->meshes.size()) {
                return nullptr;
            }

            const MeshAsset& meshAsset = record.model->meshes[record.meshIndex];
            if (record.primitiveIndex >= meshAsset.primitives.size()) {
                return nullptr;
            }

            return &meshAsset.primitives[record.primitiveIndex];
        }

        Mesh* GetOrCreateTraditionalRecordMesh(
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record,
            bool skinned) {

            const MeshPrimitive* primitive = ResolveTraditionalRecordPrimitive(record);
            if (primitive == nullptr) {
                return nullptr;
            }

            return skinned
                ? g.primitiveCache.GetOrCreateSkinned(
                    SERVICES::gCtx.device,
                    *primitive,
                    &g.debugStats)
                : g.primitiveCache.GetOrCreateStatic(
                    SERVICES::gCtx.device,
                    *primitive,
                    &g.debugStats);
        }

        bool FillTraditionalCommandMeshView(
            RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record,
            bool skinned) {

            command.triangleMeshView = {};
            Mesh* mesh = GetOrCreateTraditionalRecordMesh(record, skinned);
            if (mesh == nullptr || !mesh->IsValid()) {
                return false;
            }

            command.triangleMeshView.vertexBuffer = mesh->GetVBView();
            command.triangleMeshView.indexBuffer = mesh->GetIBView();
            return command.HasTriangleMeshGpuView();
        }

        D3D12_GPU_VIRTUAL_ADDRESS ResolveTraditionalJointPaletteAddress(
            size_t objectIndex) {

            if (g.jointPaletteCB == nullptr || objectIndex >= kMaxObjectCount) {
                return 0;
            }

            constexpr UINT kJointPaletteStride =
                AlignConstantBufferSize(sizeof(JointPaletteCB));
            return g.jointPaletteCB->GetGPUVirtualAddress() +
                static_cast<UINT64>(kJointPaletteStride) * objectIndex;
        }

        void HydrateOwnedTraditionalIndirectStream(
            OwnedTraditionalIndirectStream& stream) {

            if (stream.commands.empty() ||
                stream.records.empty() ||
                stream.executableRecordIndices.empty()) {
                return;
            }

            for (RENDER3D::RUNTIME::SurfaceDrawCommand& command :
                stream.commands) {

                command.triangleMeshView = {};
                command.jointPaletteGpuAddress = 0;
                if (command.recordCount == 0 ||
                    command.firstExecutableIndex >=
                        stream.executableRecordIndices.size()) {
                    continue;
                }

                const uint32_t recordIndex =
                    stream.executableRecordIndices[command.firstExecutableIndex];
                if (recordIndex >= stream.records.size()) {
                    continue;
                }

                const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record =
                    stream.records[recordIndex];
                const bool hasJointPalette =
                    command.firstRecordIndex !=
                        RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex &&
                    command.firstRecordIndex < stream.jointPalettes.size() &&
                    !stream.jointPalettes[command.firstRecordIndex].empty();
                const bool skinnedCommand = record.skinned && hasJointPalette;
                if (!FillTraditionalCommandMeshView(
                    command,
                    record,
                    skinnedCommand)) {
                    continue;
                }

                if (!skinnedCommand) {
                    continue;
                }

                const size_t paletteSlot =
                    static_cast<size_t>(stream.gpuSceneBaseIndex) +
                    static_cast<size_t>(command.firstGpuSceneInstanceIndex);
                if (paletteSlot >= kMaxObjectCount ||
                    g.jointPaletteMapped == nullptr ||
                    g.jointPaletteCB == nullptr) {
                    command.jointPaletteGpuAddress = 0;
                    continue;
                }

                (void)UploadJointPalette(
                    g.jointPaletteMapped,
                    paletteSlot,
                    stream.jointPalettes[command.firstRecordIndex]);
                command.jointPaletteGpuAddress =
                    ResolveTraditionalJointPaletteAddress(paletteSlot);
            }
        }

        bool HasGpuDrivenTraditionalIndirectCommands() {
            for (const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass :
                g.gpuDrivenSceneSource.passes) {
                if (pass.traditionalIndirect.HasCommands()) {
                    return true;
                }
            }
            return false;
        }

        void CopyOwnedTraditionalIndirectStreamsFromSceneSource() {
            for (size_t passIndex = 0;
                passIndex < RENDER3D::GPUDRIVEN::kGpuDrivenPassCount;
                ++passIndex) {

                OwnedTraditionalIndirectStream& owned =
                    gOwnedTraditionalIndirectStreams[passIndex];
                RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass =
                    g.gpuDrivenSceneSource.passes[passIndex];
                (void)owned.CopyFrom(pass.traditionalIndirect);
            }
        }

        void AttachOwnedTraditionalIndirectStreamsToSceneSource() {
            for (size_t passIndex = 0;
                passIndex < RENDER3D::GPUDRIVEN::kGpuDrivenPassCount;
                ++passIndex) {

                gOwnedTraditionalIndirectStreams[passIndex].AttachTo(
                    g.gpuDrivenSceneSource.passes[passIndex]);
            }
        }

        void ClearOwnedTraditionalIndirectStreams() {
            for (OwnedTraditionalIndirectStream& stream :
                gOwnedTraditionalIndirectStreams) {
                stream.Clear();
            }
        }

        void RefreshGpuDrivenTraditionalIndirectStreamsForActivePipeline() {
            if (!HasGpuDrivenTraditionalIndirectCommands()) {
                AttachOwnedTraditionalIndirectStreamsToSceneSource();
                return;
            }

            for (size_t passIndex = 0;
                passIndex < RENDER3D::GPUDRIVEN::kGpuDrivenPassCount;
                ++passIndex) {

                OwnedTraditionalIndirectStream& owned =
                    gOwnedTraditionalIndirectStreams[passIndex];
                HydrateOwnedTraditionalIndirectStream(owned);
                owned.AttachTo(g.gpuDrivenSceneSource.passes[passIndex]);
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

        bool CreateMappedUploadBuffer(
            ID3D12Device* device,
            UINT64 byteSize,
            Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
            void** mapped) {

            if (device == nullptr || byteSize == 0 || mapped == nullptr) {
                return false;
            }

            resource.Reset();
            *mapped = nullptr;
            auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto desc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
            if (FAILED(device->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(resource.GetAddressOf())))) {
                return false;
            }

            return SUCCEEDED(resource->Map(0, nullptr, mapped));
        }

        bool CreateDefaultBuffer(
            ID3D12Device* device,
            UINT64 byteSize,
            Microsoft::WRL::ComPtr<ID3D12Resource>& resource) {

            if (device == nullptr || byteSize == 0) {
                return false;
            }

            resource.Reset();
            auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            auto desc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
            return SUCCEEDED(device->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(resource.GetAddressOf())));
        }

        bool CreateGpuResidentMappedBuffer(
            ID3D12Device* device,
            UINT64 byteSize,
            Microsoft::WRL::ComPtr<ID3D12Resource>& uploadResource,
            Microsoft::WRL::ComPtr<ID3D12Resource>& defaultResource,
            void** mapped) {

            if (!CreateMappedUploadBuffer(device, byteSize, uploadResource, mapped) ||
                !CreateDefaultBuffer(device, byteSize, defaultResource)) {
                return false;
            }
            if (mapped != nullptr && *mapped != nullptr) {
                std::memset(*mapped, 0, static_cast<size_t>(byteSize));
            }
            return true;
        }

        void CommitMappedBufferToGpu(
            ID3D12GraphicsCommandList* commandList,
            ID3D12Resource* uploadResource,
            ID3D12Resource* defaultResource,
            D3D12_RESOURCE_STATES& defaultState,
            UINT64 byteCount) {

            if (commandList == nullptr ||
                uploadResource == nullptr ||
                defaultResource == nullptr ||
                byteCount == 0) {
                return;
            }

            if (defaultState != D3D12_RESOURCE_STATE_COPY_DEST) {
                const auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
                    defaultResource,
                    defaultState,
                    D3D12_RESOURCE_STATE_COPY_DEST);
                commandList->ResourceBarrier(1, &toCopyDest);
                defaultState = D3D12_RESOURCE_STATE_COPY_DEST;
            }

            commandList->CopyBufferRegion(
                defaultResource,
                0,
                uploadResource,
                0,
                byteCount);

            const D3D12_RESOURCE_STATES shaderState =
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            const auto toShader = CD3DX12_RESOURCE_BARRIER::Transition(
                defaultResource,
                D3D12_RESOURCE_STATE_COPY_DEST,
                shaderState);
            commandList->ResourceBarrier(1, &toShader);
            defaultState = shaderState;
        }

        void BindActiveFrameResources(uint32_t frameIndex) {
            g.activeFrameResourceIndex = frameIndex % GFX::kFrameResourceCount;
            MeshRendererFrameResources& frame =
                g.frameResources[g.activeFrameResourceIndex];

            g.cameraCB = frame.cameraCB;
            g.cullingCameraCB = frame.cullingCameraCB;
            g.objectCB = frame.objectCB;
            g.objectDataBuffer = frame.objectDataBuffer;
            g.materialDataBuffer = frame.materialDataBuffer;
            g.lightCB = frame.lightCB;
            g.shadowCB = frame.shadowCB;
            g.skyEnvironmentCB = frame.skyEnvironmentCB;
            g.jointPaletteCB = frame.jointPaletteCB;

            g.cameraMapped = frame.cameraMapped;
            g.cullingCameraMapped = frame.cullingCameraMapped;
            g.objectMapped = frame.objectMapped;
            g.objectDataMapped = frame.objectDataMapped;
            g.materialDataMapped = frame.materialDataMapped;
            g.lightMapped = frame.lightMapped;
            g.shadowMapped = frame.shadowMapped;
            g.skyEnvironmentMapped = frame.skyEnvironmentMapped;
            g.jointPaletteMapped = frame.jointPaletteMapped;

            g.objectDataSrvCpu = frame.objectDataSrvCpu;
            g.objectDataSrvGpu = frame.objectDataSrvGpu;
            g.materialDataSrvCpu = frame.materialDataSrvCpu;
            g.materialDataSrvGpu = frame.materialDataSrvGpu;
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

            ID3D12DescriptorHeap* srvHeap = SERVICES::gCtx.srvHeap;
            if (srvHeap == nullptr) {
                return false;
            }
            const UINT descriptorSize =
                device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            const UINT surfaceGpuSceneSrvIndex =
                GFX::DESCRIPTOR::ToFrameIndex(
                    GFX::DESCRIPTOR::SystemSrv::MeshSurfaceGpuSceneFrame0,
                    0u);
            const D3D12_CPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrvCpu =
                GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, surfaceGpuSceneSrvIndex);
            const D3D12_GPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrvGpu =
                GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, surfaceGpuSceneSrvIndex);
            if (!g.surfaceGpuSceneBuffer.Initialize(
                device,
                surfaceGpuSceneSrvCpu,
                surfaceGpuSceneSrvGpu,
                descriptorSize)) {
                DEBUGLOG::PushRenderError("[MeshRenderer][WARN] SurfaceGpuScene buffer initialization failed. GPU-driven mesh pass will be unavailable.");
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC objectDataSrv{};
            objectDataSrv.Format = DXGI_FORMAT_UNKNOWN;
            objectDataSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            objectDataSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            objectDataSrv.Buffer.FirstElement = 0;
            objectDataSrv.Buffer.NumElements = kMaxObjectCount;
            objectDataSrv.Buffer.StructureByteStride = sizeof(ObjectGpuData);
            objectDataSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

            D3D12_SHADER_RESOURCE_VIEW_DESC materialDataSrv{};
            materialDataSrv.Format = DXGI_FORMAT_UNKNOWN;
            materialDataSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            materialDataSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            materialDataSrv.Buffer.FirstElement = 0;
            materialDataSrv.Buffer.NumElements = kMaxMaterialDataCount;
            materialDataSrv.Buffer.StructureByteStride = sizeof(MaterialGpuData);
            materialDataSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

            for (uint32_t frameIndex = 0; frameIndex < GFX::kFrameResourceCount; ++frameIndex) {
                MeshRendererFrameResources& frame = g.frameResources[frameIndex];

                if (!CreateMappedUploadBuffer(
                    device,
                    cameraBytes,
                    frame.cameraCB,
                    reinterpret_cast<void**>(&frame.cameraMapped)) ||
                    !CreateMappedUploadBuffer(
                        device,
                        cameraBytes,
                        frame.cullingCameraCB,
                        reinterpret_cast<void**>(&frame.cullingCameraMapped)) ||
                    !CreateMappedUploadBuffer(
                        device,
                        objectBytes,
                        frame.objectCB,
                        reinterpret_cast<void**>(&frame.objectMapped)) ||
                    !CreateGpuResidentMappedBuffer(
                        device,
                        objectDataBytes,
                        frame.objectDataUploadBuffer,
                        frame.objectDataBuffer,
                        reinterpret_cast<void**>(&frame.objectDataMapped)) ||
                    !CreateGpuResidentMappedBuffer(
                        device,
                        materialDataBytes,
                        frame.materialDataUploadBuffer,
                        frame.materialDataBuffer,
                        reinterpret_cast<void**>(&frame.materialDataMapped)) ||
                    !CreateMappedUploadBuffer(
                        device,
                        lightBytes,
                        frame.lightCB,
                        reinterpret_cast<void**>(&frame.lightMapped)) ||
                    !CreateMappedUploadBuffer(
                        device,
                        shadowBytes,
                        frame.shadowCB,
                        reinterpret_cast<void**>(&frame.shadowMapped)) ||
                    !CreateMappedUploadBuffer(
                        device,
                        skyEnvironmentBytes,
                        frame.skyEnvironmentCB,
                        reinterpret_cast<void**>(&frame.skyEnvironmentMapped)) ||
                    !CreateMappedUploadBuffer(
                        device,
                        jointPaletteBytes,
                        frame.jointPaletteCB,
                        reinterpret_cast<void**>(&frame.jointPaletteMapped))) {
                    return false;
                }
                frame.objectDataState = D3D12_RESOURCE_STATE_COMMON;
                frame.materialDataState = D3D12_RESOURCE_STATE_COMMON;

                const UINT objectDataSrvIndex =
                    GFX::DESCRIPTOR::ToFrameIndex(
                        GFX::DESCRIPTOR::SystemSrv::MeshObjectDataFrame0,
                        frameIndex);
                frame.objectDataSrvCpu =
                    GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, objectDataSrvIndex);
                frame.objectDataSrvGpu =
                    GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, objectDataSrvIndex);
                device->CreateShaderResourceView(
                    frame.objectDataBuffer.Get(),
                    &objectDataSrv,
                    frame.objectDataSrvCpu);

                const UINT materialDataSrvIndex =
                    GFX::DESCRIPTOR::ToFrameIndex(
                        GFX::DESCRIPTOR::SystemSrv::MeshMaterialDataFrame0,
                        frameIndex);
                frame.materialDataSrvCpu =
                    GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, materialDataSrvIndex);
                frame.materialDataSrvGpu =
                    GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, materialDataSrvIndex);
                device->CreateShaderResourceView(
                    frame.materialDataBuffer.Get(),
                    &materialDataSrv,
                    frame.materialDataSrvCpu);
            }

            BindActiveFrameResources(SERVICES::gCtx.frameIndex);
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
            return true;
        }

        MeshDrawContext BuildDrawContext(
            bool depthAwarePhase,
            MeshDrawPassKind passKind,
            const MeshPassResources& passResources);

        void UploadGpuDrivenSceneFrame() {
            g.gpuDrivenLayer.BeginFrame(&g.gpuDrivenSceneSource);

            RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadDesc uploadDesc{};
            uploadDesc.commandList = SERVICES::gCtx.cmdList;
            uploadDesc.residency = &g.gpuDrivenSceneResidency;
            uploadDesc.frameIndex = SERVICES::gCtx.frameIndex;
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
            g.debugStats.surfaceGpuSceneMaterialPatchChangedCount =
                gpuSceneStats.materialPatchChangedCount;
            g.debugStats.surfaceGpuSceneMaterialPatchUnchangedCount =
                gpuSceneStats.materialPatchUnchangedCount;
            g.debugStats.surfaceGpuSceneSrvValid = gpuSceneStats.srv.ptr != 0;
            g.debugStats.surfaceGpuSceneBufferReady = gpuSceneStats.initialized;
        }

        void CommitActiveMaterialDataFrame(ID3D12GraphicsCommandList* commandList) {
            MeshRendererFrameResources& frame =
                g.frameResources[g.activeFrameResourceIndex % GFX::kFrameResourceCount];
            const UINT64 materialBytes =
                static_cast<UINT64>(sizeof(MaterialGpuData)) *
                static_cast<UINT64>(
                    (std::min)(
                        static_cast<size_t>(g.materialDataFrameTable.count),
                        static_cast<size_t>(kMaxMaterialDataCount)));
            if (materialBytes == 0u) {
                return;
            }

            CommitMappedBufferToGpu(
                commandList,
                frame.materialDataUploadBuffer.Get(),
                frame.materialDataBuffer.Get(),
                frame.materialDataState,
                materialBytes);
            g.debugStats.materialDataGpuUploadBytes += static_cast<size_t>(materialBytes);
            ++g.debugStats.materialDataGpuUploadCallCount;
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

            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& depthPrepass =
                GetSceneSourcePass(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::DepthPrepass);
            prepareMaterialSources(depthPrepass.gpuSceneBaseIndex, depthPrepass.materialSources);

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
            CommitActiveMaterialDataFrame(SERVICES::gCtx.cmdList);
            g.gpuDrivenLayer.CommitSurfaceGpuSceneMaterialFrame(SERVICES::gCtx.cmdList);
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
            return
                MakeGpuDrivenPassMask(GpuDrivenPassKind::Shadow) |
                MakeGpuDrivenPassMask(GpuDrivenPassKind::DepthPrepass);
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
            PrepareSurfaceGpuSceneMaterialFrame();
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
            workContext.emitTraditionalDrawArgs =
                HasGpuDrivenTraditionalIndirectCommands();
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
            g.debugStats.clusterGpuCullDrawArgsReady =
                clusterCullStats.traditionalDrawArgsEmitted &&
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
            g.debugStats.clusterGpuCullDrawArgumentCapacity =
                clusterCullStats.drawArgumentCapacity;
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
            MeshDrawPassKind passKind) {

            if (!IsGpuDrivenWorkPreparedForPass(gpuPass)) {
                return false;
            }

            const RENDER3D::GPUDRIVEN::GeometryBackendContext backendContext =
                g.gpuDrivenLayer.BuildGeometryBackendContext(
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
                BuildDrawContext(depthAwarePhase, passKind, passResources);
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
            if (g.cameraMapped == nullptr || g.cullingCameraMapped == nullptr || g.lightMapped == nullptr || g.shadowMapped == nullptr || g.skyEnvironmentMapped == nullptr) {
                return false;
            }

            g.cameraMapped->viewProj =
                cameraOverrides != nullptr && cameraOverrides->HasRenderMatrices()
                    ? *cameraOverrides->renderViewProj
                    : camera.GetViewProj();
            g.cameraMapped->invViewProj =
                cameraOverrides != nullptr && cameraOverrides->HasRenderMatrices()
                    ? *cameraOverrides->renderInvViewProj
                    : MATH::Inverse(g.cameraMapped->viewProj);
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
            CameraCB cullingCamera = *g.cameraMapped;
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
                *g.cullingCameraMapped = g.frozenCullingCamera;
            } else {
                g.frozenCullingCameraValid = false;
                g.frozenCullingCameraWidth = 0;
                g.frozenCullingCameraHeight = 0;
                *g.cullingCameraMapped = cullingCamera;
            }
            g.debugStats.gpuDrivenCullingCameraFrozen =
                g.freezeGpuDrivenCullingCamera && g.frozenCullingCameraValid;

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
            ctx.traditionalCommandStreamBuffer = &g.traditionalCommandStreamBuffer;
            ctx.cameraAddress = ResolveCameraAddressForPass(passKind);
            ctx.cullingCameraAddress = ResolveCullingCameraAddress();
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
            RENDER3D::SCREENSPACE::ScreenSpaceGeometryAux& geometryAux,
            D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv) {
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

            const uint32_t width = static_cast<uint32_t>(
                std::max(1.0f, g.cameraMapped ? g.cameraMapped->screenParams.x : 1.0f));
            const uint32_t height = static_cast<uint32_t>(
                std::max(1.0f, g.cameraMapped ? g.cameraMapped->screenParams.y : 1.0f));
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

    }

    void Reset() {
        g.frameObjectIndex = 0;
        g.materialDataFrameTable.Clear();
        g.gpuDrivenSceneSource.Reset();
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

        g.gpuDrivenSceneSource.Reset();
        ClearOwnedTraditionalIndirectStreams();
        if (source == nullptr) {
            return;
        }
        g.gpuDrivenSceneSource = *source;
        CopyOwnedTraditionalIndirectStreamsFromSceneSource();
        AttachOwnedTraditionalIndirectStreamsToSceneSource();
    }

    bool HasSubmittedItems() {
        return HasGpuDrivenSceneSource();
    }

    bool BeginFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        RenderDebugView debugView) {
        if (!EnsureInitialized()) {
            return false;
        }
        BindActiveFrameResources(SERVICES::gCtx.frameIndex);
        g.materialResolver.BeginFrame(kMaxMaterialTextureGpuLoadsPerFrame);
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
            RefreshGpuDrivenTraditionalIndirectStreamsForActivePipeline();
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
        RenderDebugView debugView,
        const MeshFrameCameraOverrides* cameraOverrides) {

        if (!EnsureInitialized()) {
            return false;
        }
        BindActiveFrameResources(SERVICES::gCtx.frameIndex);
        g.materialResolver.BeginFrame(kMaxMaterialTextureGpuLoadsPerFrame);
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
            RefreshGpuDrivenTraditionalIndirectStreamsForActivePipeline();
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
        return g.cameraMapped;
    }

    const CameraCB* GetGpuDrivenCullingCameraConstants() {
        return g.cullingCameraMapped != nullptr
            ? g.cullingCameraMapped
            : g.cameraMapped;
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
        if (g.skyEnvironmentMapped != nullptr) {
            g.skyEnvironmentMapped->aoParams.x = enabled ? 1.0f : 0.0f;
        }
    }

    void EndFrame() {
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
