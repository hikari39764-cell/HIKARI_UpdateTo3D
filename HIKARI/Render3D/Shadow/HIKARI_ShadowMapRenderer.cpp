#include "HIKARI_ShadowMapRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <wrl/client.h>

#include "HIKARI_Services.h"
#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/Cluster/HIKARI_ClusterGpuCullingPass.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/GpuDriven/HIKARI_ClusterGpuDrivenProducerAdapter.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenDrawCommandStream.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenFrame.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenLayer.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenWorkBuilder.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/CommandStream/HIKARI_GpuTraditionalCommandStreamBuffer.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/HIKARI_Mesh.h"
#include "Render3D/Meshlet/HIKARI_MeshletRenderBackend.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Shadow/HIKARI_ShadowCachePolicy.h"
#include "Render3D/Shadow/HIKARI_ShadowLightFrame.h"
#include "Render3D/Shadow/HIKARI_ShadowRecordExecutor.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::SHADOW {

    using Microsoft::WRL::ComPtr;

    namespace {
        constexpr UINT kMaxCasterObjects = 2048u;
        constexpr size_t kMaxJointPaletteMatrices = 128u;
        constexpr D3D12_RESOURCE_STATES kShadowShaderReadState =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        struct ShadowCameraCB {
            MATH::Mat4 lightViewProj{};
            MATH::Mat4 invLightViewProj{};
            MATH::Vec4 lightPosition{};
            MATH::Vec4 timeParams{};
            MATH::Vec4 screenParams{};
        };

        struct ShadowObjectCB {
            MATH::Mat4 world{};
            uint32_t materialFlags = 0;
            float alphaCutoff = 0.5f;
            float padding[2]{};
        };

        struct JointPaletteCB {
            MATH::Mat4 jointMatrices[kMaxJointPaletteMatrices]{};
        };

        struct ShadowFrameResources {
            ComPtr<ID3D12Resource> cameraCB;
            ComPtr<ID3D12Resource> objectCB;
            ComPtr<ID3D12Resource> materialDataUploadBuffer;
            ComPtr<ID3D12Resource> materialDataBuffer;
            ComPtr<ID3D12Resource> jointPaletteCB;
            ShadowCameraCB* cameraMapped = nullptr;
            ShadowObjectCB* objectMapped = nullptr;
            MESHRENDERER::MaterialGpuData* materialDataMapped = nullptr;
            JointPaletteCB* jointPaletteMapped = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE materialDataSrvCpu{};
            D3D12_GPU_DESCRIPTOR_HANDLE materialDataSrvGpu{};
            D3D12_RESOURCE_STATES materialDataState = D3D12_RESOURCE_STATE_COMMON;
        };

        struct ShadowMaterialFrameTable {
            std::unordered_map<uint64_t, uint32_t> indexByKey{};
            std::unordered_set<uint32_t> textureDescriptorIndices{};
            uint32_t count = 0;

            void Clear() {
                indexByKey.clear();
                textureDescriptorIndices.clear();
                count = 0;
            }
        };

        struct State {
            bool initialized = false;
            bool frameEnabled = false;
            bool frameHasShadowWork = false;
            bool frameHasStaticShadowWork = false;
            bool frameHasDynamicShadowWork = false;
            uint32_t resolution = 0;
            D3D12_RESOURCE_STATES shadowState = kShadowShaderReadState;
            D3D12_RESOURCE_STATES staticShadowState = D3D12_RESOURCE_STATE_COMMON;
            MATH::Mat4 lightViewProj = MATH::Mat4::Identity();
            MATH::Vec3 lightCullPosition{};
            MATH::Vec3 lightAnchor{};
            float lightAnchorGrid = 0.0f;
            float elapsedTimeSec = 0.0f;

            ComPtr<ID3D12Resource> shadowMap;
            ComPtr<ID3D12Resource> staticShadowMap;
            ComPtr<ID3D12DescriptorHeap> dsvHeap;
            D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
            RENDER3D::TextureResourceHandle shadowSrvResource{};
            RENDER3D::TextureResourceHandle fallbackTextureResource{};
            int shadowSrvHandle = -1;
            int fallbackTextureHandle = -1;

            ComPtr<ID3D12RootSignature> rootSig;
            ComPtr<ID3D12RootSignature> skinnedRootSig;
            ComPtr<ID3D12PipelineState> staticPso;
            ComPtr<ID3D12PipelineState> skinnedPso;
            std::array<ShadowFrameResources, GFX::kFrameResourceCount> frameResources{};
            uint32_t activeFrameResourceIndex = 0;
            ComPtr<ID3D12Resource> cameraCB;
            ComPtr<ID3D12Resource> objectCB;
            ComPtr<ID3D12Resource> materialDataUploadBuffer;
            ComPtr<ID3D12Resource> materialDataBuffer;
            ComPtr<ID3D12Resource> jointPaletteCB;
            ShadowCameraCB* cameraMapped = nullptr;
            ShadowObjectCB* objectMapped = nullptr;
            MESHRENDERER::MaterialGpuData* materialDataMapped = nullptr;
            JointPaletteCB* jointPaletteMapped = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE materialDataSrvCpu{};
            D3D12_GPU_DESCRIPTOR_HANDLE materialDataSrvGpu{};
            ShadowMaterialFrameTable materialDataFrameTable{};

            const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* gpuDrivenSceneSource = nullptr;
            RENDER3D::GPUDRIVEN::GpuDrivenSceneSource shadowSceneSource{};
            RENDER3D::GPUDRIVEN::GpuDrivenSceneSource staticShadowSceneSource{};
            RENDER3D::GPUDRIVEN::GpuDrivenSceneSource dynamicShadowSceneSource{};
            RENDER3D::GPUDRIVEN::GpuDrivenSceneSource activeShadowSceneSource{};
            std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance> staticShadowPrimaryInstances{};
            std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource> staticShadowPrimaryMaterialSources{};
            std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance> dynamicShadowPrimaryInstances{};
            std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource> dynamicShadowPrimaryMaterialSources{};
            ShadowSourceCacheState shadowSourceCache{};
            RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBuffer surfaceGpuSceneBuffer{};
            RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamBuffer traditionalCommandStreamBuffer{};
            RENDER3D::GPUDRIVEN::GpuDrivenFrame gpuDrivenFrame{};
            RENDER3D::GPUDRIVEN::GpuDrivenLayer gpuDrivenLayer{};
            RENDER3D::CLUSTER::ClusterGpuCullingPass clusterGpuCullingPass{};
            RENDER3D::GPUDRIVEN::ClusterGpuDrivenProducerAdapter clusterGpuDrivenProducer{};
            RENDER3D::MESHLET::MeshletRenderBackend meshletRenderBackend{};
            std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>> primitiveMeshCache;
            std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>> primitiveSkinnedMeshCache;
            std::unordered_map<std::string, RENDER3D::TextureResourceHandle> materialTextureCache;
            ShadowMapDebugStats debugStats;
            size_t shadowMapRecreateCount = 0;
            ShadowCachePolicy shadowCache{};
        };

        State g;

        void InvalidateShadowSourceCache() {
            g.shadowSourceCache.Invalidate();
        }

        bool CanReuseShadowSourceCache() {
            return g.shadowSourceCache.Matches(g.gpuDrivenSceneSource);
        }

        void PublishShadowCacheStats() {
            g.shadowCache.PublishStats(g.debugStats);
        }

        void InvalidateShadowCache() {
            g.shadowCache.Invalidate();
            PublishShadowCacheStats();
        }

        bool HasStaticShadowDirtyRanges() {
            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& shadowPass =
                g.staticShadowSceneSource.GetPass(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
            if (shadowPass.instances == nullptr ||
                shadowPass.instances->empty() ||
                shadowPass.dirtyRanges.empty()) {
                return false;
            }

            for (const RENDER3D::GPUDRIVEN::GpuSceneDirtyRange& range :
                shadowPass.dirtyRanges) {
                const size_t first = static_cast<size_t>(range.firstInstance);
                const size_t last = (std::min)(
                    first + static_cast<size_t>(range.instanceCount),
                    shadowPass.instances->size());
                for (size_t instanceIndex = first; instanceIndex < last; ++instanceIndex) {
                    const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance =
                        (*shadowPass.instances)[instanceIndex];
                    const uint32_t staticFlag = static_cast<uint32_t>(
                        RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::StaticGeometry);
                    if ((instance.flags & staticFlag) != 0u) {
                        return true;
                    }
                }
            }

            return false;
        }

        bool CanReuseShadowCache(uint32_t resolution) {
            ShadowCacheReuseInput input{};
            input.key.layoutVersion = g.staticShadowSceneSource.layoutVersion;
            input.key.sourceVersion = g.staticShadowSceneSource.sourceVersion;
            input.key.sourceInstanceCount =
                g.staticShadowSceneSource.sourceInstanceCount;
            input.key.resolution = resolution;
            input.key.lightViewProj = g.lightViewProj;
            input.hasStaticWork = g.frameHasStaticShadowWork;
            input.resourcesReady =
                g.staticShadowMap != nullptr &&
                g.shadowMap != nullptr &&
                RENDER3D::IsTextureResourceValid(g.shadowSrvResource);
            input.stateReusable =
                g.staticShadowState == D3D12_RESOURCE_STATE_COPY_SOURCE ||
                g.staticShadowState == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            input.hasStaticDirtyRanges = HasStaticShadowDirtyRanges();
            return g.shadowCache.EvaluateReuse(input);
        }

        size_t StatDelta(size_t after, size_t before) {
            return after >= before ? after - before : after;
        }

        void AccumulateShadowMeshletStatsDelta(
            const RENDER3D::MESHLET::MeshletRenderBackendStats& before,
            const RENDER3D::MESHLET::MeshletRenderBackendStats& after) {

            g.debugStats.shadowMeshletRequestedDispatchCount +=
                StatDelta(after.requestedDispatchCount, before.requestedDispatchCount);
            g.debugStats.shadowMeshletSubmittedDispatchCount +=
                StatDelta(after.shadowSubmittedDispatchCount, before.shadowSubmittedDispatchCount);
            g.debugStats.shadowMeshletSkippedDispatchCount +=
                StatDelta(after.skippedDispatchCount, before.skippedDispatchCount);
            g.debugStats.shadowMeshletSubmitCallCount +=
                StatDelta(after.submitCallCount, before.submitCallCount);
            g.debugStats.shadowMeshletSkippedBucketCount +=
                StatDelta(after.skippedBucketCount, before.skippedBucketCount);
            g.debugStats.shadowMeshletBackFaceSubmitCallCount +=
                StatDelta(after.backFaceSubmitCallCount, before.backFaceSubmitCallCount);
            g.debugStats.shadowMeshletDoubleSidedSubmitCallCount +=
                StatDelta(after.doubleSidedSubmitCallCount, before.doubleSidedSubmitCallCount);
            g.debugStats.shadowMeshletPipelineReady = after.shadowPipelineReady;
            g.debugStats.shadowMeshletDispatchArgumentBufferReady =
                after.dispatchArgumentBufferReady;
            g.debugStats.shadowMeshletDispatchCommandSignatureReady =
                after.dispatchCommandSignatureReady;
        }

        void MarkShadowCacheHit() {
            g.shadowCache.MarkHit();
            PublishShadowCacheStats();
        }

        void MarkShadowCacheMiss() {
            g.shadowCache.MarkMiss();
            PublishShadowCacheStats();
        }

        void MarkShadowCacheValidAfterRender() {
            ShadowCacheKey key{};
            key.layoutVersion = g.staticShadowSceneSource.layoutVersion;
            key.sourceVersion = g.staticShadowSceneSource.sourceVersion;
            key.sourceInstanceCount =
                g.staticShadowSceneSource.sourceInstanceCount;
            key.resolution = g.resolution;
            key.lightViewProj = g.lightViewProj;
            g.shadowCache.MarkValid(key);
            PublishShadowCacheStats();
        }

        struct OwnedShadowTraditionalIndirectStream {
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

            bool CopyRangeFrom(
                const RENDER3D::GPUDRIVEN::GpuDrivenTraditionalIndirectView& view,
                size_t firstCommand,
                size_t commandCount,
                bool skinnedRange) {

                Clear();
                if (!view.HasCommands() ||
                    view.records == nullptr ||
                    view.executableRecordIndices == nullptr ||
                    view.commands == nullptr ||
                    view.instances == nullptr ||
                    view.materialSources == nullptr ||
                    view.jointPalettes == nullptr ||
                    firstCommand >= view.commands->size()) {
                    return false;
                }

                const size_t endCommand =
                    (std::min)(view.commands->size(), firstCommand + commandCount);
                if (endCommand <= firstCommand) {
                    return false;
                }

                records.reserve(endCommand - firstCommand);
                executableRecordIndices.reserve(endCommand - firstCommand);
                commands.reserve(endCommand - firstCommand);
                instances.reserve(endCommand - firstCommand);
                materialSources.reserve(endCommand - firstCommand);
                jointPalettes.reserve(endCommand - firstCommand);
                if (view.bucketVariants != nullptr) {
                    bucketVariants = *view.bucketVariants;
                }

                for (size_t srcIndex = firstCommand; srcIndex < endCommand; ++srcIndex) {
                    const uint32_t localIndex = static_cast<uint32_t>(records.size());
                    records.push_back((*view.records)[srcIndex]);
                    executableRecordIndices.push_back(localIndex);

                    RENDER3D::RUNTIME::SurfaceGpuSceneInstance instance =
                        (*view.instances)[srcIndex];
                    instance.sourceRecordIndex = localIndex;
                    instances.push_back(instance);

                    RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource material =
                        (*view.materialSources)[srcIndex];
                    material.localGpuSceneInstanceIndex = localIndex;
                    material.sourceRecordIndex = localIndex;
                    materialSources.push_back(material);

                    jointPalettes.push_back((*view.jointPalettes)[srcIndex]);

                    RENDER3D::RUNTIME::SurfaceDrawCommand command =
                        (*view.commands)[srcIndex];
                    command.firstExecutableIndex = localIndex;
                    command.firstRecordIndex = localIndex;
                    command.firstGpuSceneInstanceIndex = localIndex;
                    commands.push_back(command);
                }

                gpuSceneBaseIndex = 0;
                gpuSceneInstanceCount = static_cast<uint32_t>(instances.size());
                if (skinnedRange) {
                    skinnedCommandCount = static_cast<uint32_t>(commands.size());
                } else {
                    staticCommandCount = static_cast<uint32_t>(commands.size());
                }
                return true;
            }

            void AttachTo(RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass) const {
                pass.traditionalIndirect.Reset();
                if (records.empty() || executableRecordIndices.empty() || commands.empty()) {
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

        OwnedShadowTraditionalIndirectStream gShadowStaticTraditionalIndirectStream{};
        OwnedShadowTraditionalIndirectStream gShadowDynamicTraditionalIndirectStream{};

        const MaterialAsset* GetPrimitiveMaterial(const ModelAsset& asset, uint32_t materialIndex) {
            if (materialIndex >= asset.materials.size()) {
                return nullptr;
            }
            return &asset.materials[static_cast<size_t>(materialIndex)];
        }

        Mesh* GetOrCreatePrimitiveMesh(const MeshPrimitive& primitive) {
            auto found = g.primitiveMeshCache.find(&primitive);
            if (found != g.primitiveMeshCache.end()) {
                return found->second.get();
            }

            if (primitive.staticVertices.empty() || primitive.indices.empty()) {
                return nullptr;
            }

            std::vector<VertexStatic3D> vertices;
            vertices.reserve(primitive.staticVertices.size());
            for (const Vertex3D& src : primitive.staticVertices) {
                VertexStatic3D dst{};
                dst.position = src.position;
                dst.normal = src.normal;
                dst.tangent = src.tangent;
                if (MATH::Length({ dst.tangent.x, dst.tangent.y, dst.tangent.z }) <= 1e-6f) {
                    dst.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
                }
                dst.u = src.uv0.x;
                dst.v = src.uv0.y;
                dst.uv1 = src.uv1;
                vertices.push_back(dst);
            }

            auto mesh = std::make_unique<Mesh>();
            if (!mesh->CreateStatic(SERVICES::gCtx.device, vertices, primitive.indices)) {
                return nullptr;
            }
            Mesh* raw = mesh.get();
            g.primitiveMeshCache.emplace(&primitive, std::move(mesh));
            return raw;
        }

        Mesh* GetOrCreateSkinnedPrimitiveMesh(const MeshPrimitive& primitive) {
            auto found = g.primitiveSkinnedMeshCache.find(&primitive);
            if (found != g.primitiveSkinnedMeshCache.end()) {
                return found->second.get();
            }

            if (primitive.skinnedVertices.empty() || primitive.indices.empty()) {
                return nullptr;
            }

            std::vector<VertexSkinnedGpu3D> vertices;
            vertices.reserve(primitive.skinnedVertices.size());
            for (const SkinnedVertex3D& src : primitive.skinnedVertices) {
                VertexSkinnedGpu3D dst{};
                dst.position = src.position;
                dst.normal = src.normal;
                dst.tangent = src.tangent;
                dst.uv0 = src.uv0;
                dst.uv1 = src.uv1;
                dst.color0 = src.color0;
                for (size_t i = 0; i < 4; ++i) {
                    dst.joints[i] = src.joints[i];
                    dst.weights[i] = src.weights[i];
                }
                vertices.push_back(dst);
            }

            auto mesh = std::make_unique<Mesh>();
            if (!mesh->CreateSkinned(SERVICES::gCtx.device, vertices, primitive.indices)) {
                return nullptr;
            }
            Mesh* raw = mesh.get();
            g.primitiveSkinnedMeshCache.emplace(&primitive, std::move(mesh));
            return raw;
        }

        const MeshPrimitive* ResolveShadowTraditionalRecordPrimitive(
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

        D3D12_GPU_VIRTUAL_ADDRESS ResolveShadowJointPaletteAddress(size_t objectIndex) {
            if (g.jointPaletteCB == nullptr || objectIndex >= kMaxCasterObjects) {
                return 0;
            }

            return g.jointPaletteCB->GetGPUVirtualAddress() +
                static_cast<UINT64>(GFX::AlignD3D12ConstantBufferByteSize(sizeof(JointPaletteCB))) *
                objectIndex;
        }

        size_t UploadShadowIndirectJointPalette(
            size_t objectIndex,
            const std::vector<MATH::Mat4>& palette) {

            if (g.jointPaletteMapped == nullptr || objectIndex >= kMaxCasterObjects) {
                return 0;
            }

            JointPaletteCB cb{};
            for (MATH::Mat4& matrix : cb.jointMatrices) {
                matrix = MATH::Mat4::Identity();
            }

            const size_t uploadCount =
                std::min(palette.size(), kMaxJointPaletteMatrices);
            for (size_t i = 0; i < uploadCount; ++i) {
                cb.jointMatrices[i] = palette[i];
            }

            uint8_t* dst = reinterpret_cast<uint8_t*>(g.jointPaletteMapped) +
                static_cast<size_t>(GFX::AlignD3D12ConstantBufferByteSize(sizeof(JointPaletteCB))) *
                    objectIndex;
            std::memcpy(dst, &cb, sizeof(cb));
            return uploadCount;
        }

        void UploadShadowMeshShaderJointPalettes() {
            if (g.gpuDrivenSceneSource == nullptr ||
                g.gpuDrivenSceneSource->meshShaderJointPalettes == nullptr) {
                return;
            }
            const auto& palettes =
                *g.gpuDrivenSceneSource->meshShaderJointPalettes;
            const size_t paletteCount =
                (std::min)(palettes.size(), static_cast<size_t>(kMaxCasterObjects));
            for (size_t paletteIndex = 0; paletteIndex < paletteCount; ++paletteIndex) {
                if (!palettes[paletteIndex].empty()) {
                    (void)UploadShadowIndirectJointPalette(
                        paletteIndex,
                        palettes[paletteIndex]);
                }
            }
        }

        void HydrateShadowTraditionalIndirectStream(
            OwnedShadowTraditionalIndirectStream& stream) {

            if (stream.records.empty() ||
                stream.executableRecordIndices.empty() ||
                stream.commands.empty()) {
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
                const MeshPrimitive* primitive =
                    ResolveShadowTraditionalRecordPrimitive(record);
                if (primitive == nullptr) {
                    continue;
                }

                const bool hasJointPalette =
                    command.firstRecordIndex !=
                        RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex &&
                    command.firstRecordIndex < stream.jointPalettes.size() &&
                    !stream.jointPalettes[command.firstRecordIndex].empty();
                const bool skinnedCommand = record.skinned && hasJointPalette;
                Mesh* mesh = skinnedCommand
                    ? GetOrCreateSkinnedPrimitiveMesh(*primitive)
                    : GetOrCreatePrimitiveMesh(*primitive);
                if (mesh == nullptr || !mesh->IsValid()) {
                    continue;
                }

                command.triangleMeshView.vertexBuffer = mesh->GetVBView();
                command.triangleMeshView.indexBuffer = mesh->GetIBView();
                if (!command.HasTriangleMeshGpuView() || !skinnedCommand) {
                    continue;
                }

                const size_t paletteSlot =
                    static_cast<size_t>(stream.gpuSceneBaseIndex) +
                    static_cast<size_t>(command.firstGpuSceneInstanceIndex);
                if (paletteSlot >= kMaxCasterObjects ||
                    g.jointPaletteMapped == nullptr ||
                    g.jointPaletteCB == nullptr) {
                    continue;
                }

                (void)UploadShadowIndirectJointPalette(
                    paletteSlot,
                    stream.jointPalettes[command.firstRecordIndex]);
                command.jointPaletteGpuAddress =
                    ResolveShadowJointPaletteAddress(paletteSlot);
            }
        }

        RENDER3D::TextureResourceHandle ResolvePrimitiveTextureResource(const ModelAsset& asset, const MaterialAsset* materialAsset) {
            if (materialAsset == nullptr ||
                materialAsset->baseColorTexture.textureIndex < 0 ||
                materialAsset->baseColorTexture.textureIndex >= static_cast<int>(asset.textures.size())) {
                return g.fallbackTextureResource;
            }

            const TextureAsset3D& texture = asset.textures[static_cast<size_t>(materialAsset->baseColorTexture.textureIndex)];
            if (texture.sourcePath.empty()) {
                return g.fallbackTextureResource;
            }

            const std::string cacheKey = "shadow:base:" + texture.sourcePath;
            auto found = g.materialTextureCache.find(cacheKey);
            if (found != g.materialTextureCache.end() &&
                RENDER3D::IsTextureResourceValid(found->second)) {
                return found->second;
            }

            RENDER3D::TextureResourceHandle resource =
                RENDER3D::LoadTextureResourceSrgb(cacheKey, texture.sourcePath);
            g.materialTextureCache[cacheKey] = resource;
            return RENDER3D::IsTextureResourceValid(resource) ? resource : g.fallbackTextureResource;
        }

        uint32_t ResolveTextureDescriptorIndex(int textureHandle) {
            const UINT descriptorIndex =
                RENDER3D::GetTextureResourceSrvDescriptorIndexFromBackendHandle(textureHandle);
            return descriptorIndex == UINT32_MAX
                ? MESHRENDERER::kInvalidTextureDescriptorIndex
                : static_cast<uint32_t>(descriptorIndex);
        }

        void BindShadowGpuDrivenFrameResources(
            ID3D12GraphicsCommandList* cmd,
            ID3D12Resource* meshletVisibleRangeBuffer,
            ID3D12Resource* meshletVisibleClusterListBuffer = nullptr,
            bool skinnedRoot = false) {

            ID3D12RootSignature* rootSig =
                skinnedRoot ? g.skinnedRootSig.Get() : g.rootSig.Get();
            if (cmd == nullptr || rootSig == nullptr) {
                return;
            }

            cmd->SetGraphicsRootSignature(rootSig);
            cmd->SetGraphicsRootConstantBufferView(
                RECORD::kShadowStaticRootParamCamera,
                g.cameraCB != nullptr ? g.cameraCB->GetGPUVirtualAddress() : 0u);
            cmd->SetGraphicsRootConstantBufferView(
                RECORD::kShadowStaticRootParamCullingCamera,
                g.cameraCB != nullptr ? g.cameraCB->GetGPUVirtualAddress() : 0u);
            if (g.materialDataSrvGpu.ptr != 0) {
                cmd->SetGraphicsRootDescriptorTable(
                    RECORD::kShadowStaticRootParamMaterialData,
                    g.materialDataSrvGpu);
            }
            if (g.surfaceGpuSceneBuffer.GetSrv().ptr != 0) {
                cmd->SetGraphicsRootDescriptorTable(
                    RECORD::kShadowStaticRootParamSurfaceGpuScene,
                    g.surfaceGpuSceneBuffer.GetSrv());
            }
            const D3D12_GPU_DESCRIPTOR_HANDLE texturePoolSrv =
                RENDER3D::GetMaterialTexturePoolSrvGpuHandle(SERVICES::gCtx);
            if (texturePoolSrv.ptr != 0) {
                cmd->SetGraphicsRootDescriptorTable(
                    RECORD::kShadowStaticRootParamTexturePool,
                    texturePoolSrv);
            }
            const D3D12_GPU_DESCRIPTOR_HANDLE clusterPoolSrv =
                RENDER3D::GetClusterGeometryPoolSrvGpuHandle(SERVICES::gCtx);
            if (clusterPoolSrv.ptr != 0) {
                cmd->SetGraphicsRootDescriptorTable(
                    RECORD::kShadowStaticRootParamClusterGeometryPool,
                    clusterPoolSrv);
            }
            if (meshletVisibleRangeBuffer != nullptr) {
                cmd->SetGraphicsRootShaderResourceView(
                    RECORD::kShadowStaticRootParamMeshletVisibleRanges,
                    meshletVisibleRangeBuffer->GetGPUVirtualAddress());
            }
            if (meshletVisibleClusterListBuffer != nullptr) {
                cmd->SetGraphicsRootShaderResourceView(
                    RECORD::kShadowStaticRootParamMeshletVisibleClusterList,
                    meshletVisibleClusterListBuffer->GetGPUVirtualAddress());
            }
            if (g.jointPaletteCB != nullptr) {
                cmd->SetGraphicsRootShaderResourceView(
                    RECORD::kShadowStaticRootParamDeformationPalettes,
                    g.jointPaletteCB->GetGPUVirtualAddress());
            }
            cmd->SetGraphicsRoot32BitConstant(
                RECORD::kShadowStaticRootParamMaterialIndex,
                0u,
                0);
        }

        uint32_t UploadShadowMaterialData(
            uint64_t key,
            const MESHRENDERER::MaterialGpuData& data) {

            if (g.materialDataMapped == nullptr) {
                return MESHRENDERER::kInvalidMaterialDataIndex;
            }

            auto found = g.materialDataFrameTable.indexByKey.find(key);
            if (found != g.materialDataFrameTable.indexByKey.end()) {
                return found->second;
            }

            if (g.materialDataFrameTable.count >= MESHRENDERER::kMaxMaterialDataCount) {
                return MESHRENDERER::kInvalidMaterialDataIndex;
            }

            const uint32_t index = g.materialDataFrameTable.count++;
            g.materialDataMapped[index] = data;
            g.materialDataFrameTable.indexByKey.emplace(key, index);
            if (data.baseColorTextureDescriptorIndex != MESHRENDERER::kInvalidTextureDescriptorIndex) {
                g.materialDataFrameTable.textureDescriptorIndices.insert(data.baseColorTextureDescriptorIndex);
            }
            return index;
        }

        uint64_t HashAppend(uint64_t seed, uint64_t value) {
            constexpr uint64_t kMul = 1099511628211ull;
            seed ^= value;
            seed *= kMul;
            return seed;
        }

        uint64_t HashBytes(uint64_t seed, const void* data, size_t size) {
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i) {
                seed = HashAppend(seed, static_cast<uint64_t>(bytes[i]));
            }
            return seed;
        }

        uint64_t HashString(uint64_t seed, const std::string& value) {
            seed = HashAppend(seed, static_cast<uint64_t>(value.size()));
            return value.empty()
                ? seed
                : HashBytes(seed, value.data(), value.size());
        }

        uint64_t BuildShadowMaterialDataKey(
            uint64_t stableMaterialKey,
            const MESHRENDERER::MaterialGpuData& data) {

            uint64_t seed = HashAppend(1469598103934665603ull, stableMaterialKey);
            return HashBytes(seed, &data, sizeof(data));
        }

        bool HasSurfaceGpuSceneFlag(
            const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance,
            RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags flag) {

            return (instance.flags & static_cast<uint32_t>(flag)) != 0u;
        }

        bool IsPrimaryShadowCaster(
            const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance) {

            using RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags;
            return
                HasSurfaceGpuSceneFlag(instance, SurfaceGpuSceneInstanceFlags::PassShadow) &&
                HasSurfaceGpuSceneFlag(instance, SurfaceGpuSceneInstanceFlags::CastShadow);
        }

        bool IsStaticPrimaryShadowCaster(
            const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance) {

            return IsPrimaryShadowCaster(instance) &&
                HasSurfaceGpuSceneFlag(
                    instance,
                    RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::StaticGeometry);
        }

        uint64_t HashSurfaceGpuSceneInstanceForShadow(
            uint64_t seed,
            const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance) {

            seed = HashBytes(seed, &instance.world, sizeof(instance.world));
            seed = HashBytes(seed, &instance.normalMatrix, sizeof(instance.normalMatrix));
            seed = HashBytes(seed, &instance.clusterWorld, sizeof(instance.clusterWorld));
            seed = HashBytes(
                seed,
                &instance.clusterNormalMatrix,
                sizeof(instance.clusterNormalMatrix));
            seed = HashBytes(seed, &instance.boundsCenterRadius, sizeof(instance.boundsCenterRadius));
            seed = HashAppend(seed, instance.sourceRecordIndex);
            seed = HashAppend(seed, instance.sourceSurfaceInstanceIndex);
            seed = HashAppend(seed, instance.objectIdLow);
            seed = HashAppend(seed, instance.objectIdHigh);
            seed = HashAppend(seed, instance.meshIndex);
            seed = HashAppend(seed, instance.primitiveIndex);
            seed = HashAppend(seed, instance.sourceMaterialIndex);
            seed = HashAppend(seed, instance.nodeIndex);
            seed = HashAppend(seed, instance.flags);
            seed = HashAppend(seed, instance.clusterRangeIndex);
            seed = HashAppend(seed, instance.clusterRangeCount);
            seed = HashAppend(seed, instance.meshResourceIndex);
            seed = HashAppend(seed, instance.meshResourceGeneration);
            seed = HashAppend(seed, instance.materialResourceIndex);
            seed = HashAppend(seed, instance.materialResourceGeneration);
            seed = HashAppend(seed, instance.clusterGeometryResourceIndex);
            seed = HashAppend(seed, instance.clusterGeometryMetadataSrvDescriptorIndex);
            seed = HashAppend(seed, instance.resourceFlags);
            seed = HashAppend(seed, instance.geometryBackend);
            seed = HashAppend(seed, instance.fxFlags);
            seed = HashAppend(seed, instance.clusterGeometrySrvDescriptorIndex);
            seed = HashAppend(seed, instance.clusterSurfaceIndex);
            seed = HashAppend(seed, instance.clusterIndexCount);
            seed = HashAppend(seed, instance.clusterLodRangeIndex);
            seed = HashAppend(seed, instance.clusterLodRangeCount);
            seed = HashAppend(seed, instance.clusterSelectedLodIndex);
            seed = HashAppend(seed, instance.clusterLodFlags);
            for (const MATH::Vec4& value : instance.fxUser) {
                seed = HashBytes(seed, &value, sizeof(value));
            }
            return seed;
        }

        uint64_t HashSurfaceGpuSceneMaterialSourceForShadow(
            uint64_t seed,
            const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source) {

            seed = HashAppend(seed, source.localGpuSceneInstanceIndex);
            seed = HashAppend(seed, source.sourceRecordIndex);
            seed = HashAppend(seed, source.sourceSurfaceInstanceIndex);
            seed = HashAppend(seed, source.materialIndex);
            seed = HashAppend(seed, source.materialKey);
            seed = HashBytes(seed, &source.world, sizeof(source.world));
            seed = HashBytes(seed, &source.normalMatrix, sizeof(source.normalMatrix));
            seed = HashAppend(seed, source.receiveShadow ? 1u : 0u);
            seed = HashAppend(seed, source.fxFlags);
            for (const MATH::Vec4& value : source.fxUser) {
                seed = HashBytes(seed, &value, sizeof(value));
            }
            return seed;
        }

        uint64_t HashSurfaceResourceIdsForShadow(
            uint64_t seed,
            const RENDER3D::RUNTIME::SurfaceResourceIds& resources) {

            seed = HashAppend(seed, resources.mesh.index);
            seed = HashAppend(seed, resources.mesh.generation);
            seed = HashAppend(seed, resources.material.index);
            seed = HashAppend(seed, resources.material.generation);
            seed = HashAppend(seed, resources.clusterGeometry.index);
            seed = HashAppend(seed, resources.clusterGeometry.generation);
            seed = HashAppend(seed, static_cast<uint32_t>(resources.geometryBackend));
            seed = HashAppend(seed, resources.modelKey);
            seed = HashAppend(seed, resources.geometryKey);
            seed = HashAppend(seed, resources.clusterGeometryKey);
            seed = HashAppend(seed, resources.materialKey);
            seed = HashAppend(seed, resources.textureSetKey);
            seed = HashAppend(seed, resources.shaderKey);
            seed = HashAppend(seed, resources.pipelineKey);
            seed = HashAppend(seed, resources.meshIndex);
            seed = HashAppend(seed, resources.primitiveIndex);
            seed = HashAppend(seed, resources.materialIndex);
            return seed;
        }

        uint64_t HashSurfaceDrawBatchKeyForShadow(
            uint64_t seed,
            const RENDER3D::RUNTIME::SurfaceDrawBatchKey& key) {

            seed = HashAppend(seed, static_cast<uint32_t>(key.pass));
            seed = HashAppend(seed, static_cast<uint32_t>(key.geometryBackend));
            seed = HashAppend(seed, static_cast<uint32_t>(key.backendRoute));
            seed = HashAppend(seed, key.psoKey);
            seed = HashAppend(seed, key.geometryKey);
            seed = HashAppend(seed, key.materialKey);
            seed = HashAppend(seed, key.textureSetKey);
            seed = HashAppend(seed, key.transparent ? 1u : 0u);
            seed = HashAppend(seed, key.alphaMasked ? 1u : 0u);
            seed = HashAppend(seed, key.doubleSided ? 1u : 0u);
            seed = HashAppend(seed, key.materialFx ? 1u : 0u);
            seed = HashAppend(seed, key.waterMaterialFx ? 1u : 0u);
            seed = HashAppend(seed, key.materialFxUsesCustomVertexShader ? 1u : 0u);
            seed = HashAppend(seed, key.depthAware ? 1u : 0u);
            seed = HashAppend(seed, key.clusterMainlineEligible ? 1u : 0u);
            return seed;
        }

        uint64_t HashSurfaceDrawArgsForShadow(
            uint64_t seed,
            const RENDER3D::RUNTIME::SurfaceDrawIndexedArgs& args) {

            seed = HashAppend(seed, args.indexCountPerInstance);
            seed = HashAppend(seed, args.instanceCount);
            seed = HashAppend(seed, args.startIndexLocation);
            seed = HashAppend(seed, static_cast<uint32_t>(args.baseVertexLocation));
            seed = HashAppend(seed, args.startInstanceLocation);
            return seed;
        }

        uint64_t HashSurfaceDrawCommandForShadow(
            uint64_t seed,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command) {

            seed = HashAppend(seed, static_cast<uint32_t>(command.pass));
            seed = HashAppend(seed, command.recordCount);
            seed = HashAppend(seed, command.firstRecordIndex);
            seed = HashAppend(seed, command.gpuSceneInstanceCount);
            seed = HashAppend(seed, command.clusterRangeCount);
            seed = HashSurfaceDrawBatchKeyForShadow(seed, command.batchKey);
            seed = HashSurfaceResourceIdsForShadow(seed, command.resources);
            seed = HashSurfaceDrawArgsForShadow(seed, command.drawArgs);
            seed = HashAppend(seed, command.psoKey);
            seed = HashAppend(seed, command.geometryKey);
            seed = HashAppend(seed, static_cast<uint32_t>(command.geometryBackend));
            seed = HashAppend(seed, static_cast<uint32_t>(command.backendRoute));
            seed = HashAppend(seed, command.materialKey);
            seed = HashAppend(seed, command.textureSetKey);
            seed = HashAppend(seed, command.modelKey);
            seed = HashString(seed, command.traditionalVariant.shaderId);
            seed = HashString(seed, command.traditionalVariant.vertexShaderId);
            seed = HashString(seed, command.traditionalVariant.pixelShaderId);
            seed = HashAppend(seed, command.traditionalVariant.featureBits);
            seed = HashAppend(seed, static_cast<uint32_t>(command.traditionalVariant.composite));
            seed = HashAppend(seed, command.traditionalVariant.depthTest ? 1u : 0u);
            seed = HashAppend(seed, command.traditionalVariant.depthWrite ? 1u : 0u);
            seed = HashAppend(seed, command.traditionalVariant.doubleSided ? 1u : 0u);
            seed = HashAppend(seed, command.singleRecord ? 1u : 0u);
            seed = HashAppend(seed, command.transparent ? 1u : 0u);
            seed = HashAppend(seed, command.alphaMasked ? 1u : 0u);
            seed = HashAppend(seed, command.doubleSided ? 1u : 0u);
            seed = HashAppend(seed, command.materialFx ? 1u : 0u);
            seed = HashAppend(seed, command.waterMaterialFx ? 1u : 0u);
            seed = HashAppend(seed, command.materialFxUsesCustomVertexShader ? 1u : 0u);
            seed = HashAppend(seed, command.clusterMainlineEligible ? 1u : 0u);
            seed = HashAppend(seed, command.drawArgsValid ? 1u : 0u);
            return seed;
        }

        void AppendShadowPrimaryInstance(
            const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& sourceInstance,
            const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* sourceMaterial,
            std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>& instances,
            std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>& materialSources) {

            const uint32_t localIndex =
                static_cast<uint32_t>((std::min)(
                    instances.size(),
                    static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
            RENDER3D::RUNTIME::SurfaceGpuSceneInstance instance = sourceInstance;
            instance.materialDataIndex =
                RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex;
            instances.push_back(instance);

            RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource material{};
            if (sourceMaterial != nullptr) {
                material = *sourceMaterial;
            }
            material.localGpuSceneInstanceIndex = localIndex;
            material.sourceRecordIndex = sourceInstance.sourceRecordIndex;
            material.sourceSurfaceInstanceIndex =
                sourceInstance.sourceSurfaceInstanceIndex;
            materialSources.push_back(material);
        }

        uint64_t BuildShadowSourceLayoutHash(
            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass) {

            uint64_t seed = HashAppend(1469598103934665603ull, 0x53484c4fu);
            seed = HashAppend(seed, pass.gpuSceneInstanceCount);
            seed = HashAppend(seed, pass.traditionalIndirect.gpuSceneInstanceCount);
            seed = HashAppend(seed, pass.traditionalIndirect.staticCommandCount);
            seed = HashAppend(seed, pass.traditionalIndirect.skinnedCommandCount);
            if (pass.instances != nullptr) {
                for (const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance :
                    *pass.instances) {
                    seed = HashAppend(seed, instance.sourceRecordIndex);
                    seed = HashAppend(seed, instance.sourceSurfaceInstanceIndex);
                    seed = HashAppend(seed, instance.meshIndex);
                    seed = HashAppend(seed, instance.primitiveIndex);
                    seed = HashAppend(seed, instance.nodeIndex);
                    seed = HashAppend(seed, instance.meshResourceIndex);
                    seed = HashAppend(seed, instance.meshResourceGeneration);
                    seed = HashAppend(seed, instance.materialResourceIndex);
                    seed = HashAppend(seed, instance.materialResourceGeneration);
                    seed = HashAppend(seed, instance.clusterGeometryResourceIndex);
                    seed = HashAppend(seed, instance.geometryBackend);
                }
            }
            if (pass.traditionalIndirect.commands != nullptr) {
                for (const RENDER3D::RUNTIME::SurfaceDrawCommand& command :
                    *pass.traditionalIndirect.commands) {
                    seed = HashAppend(seed, command.firstRecordIndex);
                    seed = HashAppend(seed, command.recordCount);
                    seed = HashAppend(seed, command.geometryKey);
                    seed = HashAppend(seed, command.materialKey);
                    seed = HashAppend(seed, command.textureSetKey);
                    seed = HashAppend(seed, command.modelKey);
                }
            }
            return seed;
        }

        uint64_t BuildShadowSourceContentHash(
            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass) {

            uint64_t seed = HashAppend(1469598103934665603ull, 0x5348434fu);
            if (pass.instances != nullptr) {
                for (const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance :
                    *pass.instances) {
                    seed = HashSurfaceGpuSceneInstanceForShadow(seed, instance);
                }
            }
            if (pass.materialSources != nullptr) {
                for (const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source :
                    *pass.materialSources) {
                    seed = HashSurfaceGpuSceneMaterialSourceForShadow(seed, source);
                }
            }
            if (pass.traditionalIndirect.instances != nullptr) {
                for (const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance :
                    *pass.traditionalIndirect.instances) {
                    seed = HashSurfaceGpuSceneInstanceForShadow(seed, instance);
                }
            }
            if (pass.traditionalIndirect.materialSources != nullptr) {
                for (const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source :
                    *pass.traditionalIndirect.materialSources) {
                    seed = HashSurfaceGpuSceneMaterialSourceForShadow(seed, source);
                }
            }
            if (pass.traditionalIndirect.commands != nullptr) {
                for (const RENDER3D::RUNTIME::SurfaceDrawCommand& command :
                    *pass.traditionalIndirect.commands) {
                    seed = HashSurfaceDrawCommandForShadow(seed, command);
                }
            }
            return seed;
        }

        void ResetShadowMaterialFrame() {
            g.materialDataFrameTable.Clear();
            if (g.materialDataMapped == nullptr) {
                return;
            }

            MESHRENDERER::MaterialGpuData defaultData{};
            defaultData.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
            defaultData.pbrParams = { 0.0f, 1.0f, 1.0f, 0.5f };
            defaultData.normalScale = 1.0f;
            defaultData.baseColorTextureHandle = g.fallbackTextureHandle;
            defaultData.normalTextureHandle = -1;
            defaultData.emissiveTextureHandle = -1;
            defaultData.metallicRoughnessTextureHandle = -1;
            defaultData.occlusionTextureHandle = -1;
            defaultData.baseColorTextureDescriptorIndex =
                ResolveTextureDescriptorIndex(g.fallbackTextureHandle);
            defaultData.normalTextureDescriptorIndex = MESHRENDERER::kInvalidTextureDescriptorIndex;
            defaultData.emissiveTextureDescriptorIndex = MESHRENDERER::kInvalidTextureDescriptorIndex;
            defaultData.metallicRoughnessTextureDescriptorIndex = MESHRENDERER::kInvalidTextureDescriptorIndex;
            defaultData.occlusionTextureDescriptorIndex = MESHRENDERER::kInvalidTextureDescriptorIndex;
            (void)UploadShadowMaterialData(0u, defaultData);
        }

        MESHRENDERER::MaterialGpuData BuildShadowMaterialGpuData(
            const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source) {

            const MaterialAsset* materialAsset =
                source.model != nullptr
                    ? GetPrimitiveMaterial(*source.model, source.materialIndex)
                    : nullptr;
            const RENDER3D::TextureResourceHandle baseColorTexture =
                source.model != nullptr
                    ? ResolvePrimitiveTextureResource(*source.model, materialAsset)
                    : g.fallbackTextureResource;

            MESHRENDERER::MaterialGpuData data{};
            data.baseColor = materialAsset != nullptr
                ? materialAsset->baseColorFactor
                : MATH::Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
            data.emissiveFactor = {
                materialAsset != nullptr ? materialAsset->emissiveFactor.x : 0.0f,
                materialAsset != nullptr ? materialAsset->emissiveFactor.y : 0.0f,
                materialAsset != nullptr ? materialAsset->emissiveFactor.z : 0.0f,
                materialAsset != nullptr ? materialAsset->emissiveStrength : 1.0f
            };
            data.pbrParams = {
                materialAsset != nullptr ? materialAsset->metallicFactor : 0.0f,
                materialAsset != nullptr ? materialAsset->roughnessFactor : 1.0f,
                materialAsset != nullptr ? materialAsset->occlusionTexture.strength : 1.0f,
                materialAsset != nullptr ? materialAsset->alphaCutoff : 0.5f
            };
            data.materialFlags =
                materialAsset != nullptr ? materialAsset->featureBits : 0u;
            if (materialAsset != nullptr && materialAsset->alphaMode == AlphaMode::Mask) {
                data.materialFlags |= MATERIAL_FEATURES::AlphaMask;
            }

            int baseColorHandle =
                RENDER3D::GetTextureResourceBackendHandle(baseColorTexture);
            if (source.materialOverride != nullptr) {
                data.baseColor = source.materialOverride->GetBaseColor();
                data.materialFlags = source.materialOverride->GetFeatureBits();
                if (source.materialOverride->HasBaseColorTexture()) {
                    baseColorHandle =
                        source.materialOverride->GetBaseColorTextureHandle();
                }
            }
            if (baseColorHandle < 0) {
                baseColorHandle = g.fallbackTextureHandle;
            }

            data.hasBaseColorTexture =
                baseColorHandle >= 0 && baseColorHandle != g.fallbackTextureHandle ? 1u : 0u;
            data.hasNormalTexture = 0u;
            data.hasEmissiveTexture = 0u;
            data.hasMetallicRoughnessTexture = 0u;
            data.hasOcclusionTexture = 0u;
            data.normalScale = 1.0f;
            data.baseColorTextureHandle = baseColorHandle;
            data.normalTextureHandle = -1;
            data.emissiveTextureHandle = -1;
            data.metallicRoughnessTextureHandle = -1;
            data.occlusionTextureHandle = -1;
            data.baseColorTextureDescriptorIndex =
                ResolveTextureDescriptorIndex(baseColorHandle);
            data.normalTextureDescriptorIndex =
                MESHRENDERER::kInvalidTextureDescriptorIndex;
            data.emissiveTextureDescriptorIndex =
                MESHRENDERER::kInvalidTextureDescriptorIndex;
            data.metallicRoughnessTextureDescriptorIndex =
                MESHRENDERER::kInvalidTextureDescriptorIndex;
            data.occlusionTextureDescriptorIndex =
                MESHRENDERER::kInvalidTextureDescriptorIndex;
            return data;
        }

        void PrepareShadowSurfaceGpuSceneMaterialSources(
            uint32_t baseIndex,
            const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>* sources) {

            if (sources == nullptr || sources->empty()) {
                return;
            }

            for (size_t sourceIndex = 0; sourceIndex < sources->size(); ++sourceIndex) {
                const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source =
                    (*sources)[sourceIndex];
                if (source.model == nullptr) {
                    continue;
                }

                const MESHRENDERER::MaterialGpuData data =
                    BuildShadowMaterialGpuData(source);
                const uint32_t materialDataIndex = UploadShadowMaterialData(
                    BuildShadowMaterialDataKey(source.materialKey, data),
                    data);
                g.surfaceGpuSceneBuffer.PatchMaterialDataIndex(
                    static_cast<size_t>(baseIndex) + sourceIndex,
                    materialDataIndex == MESHRENDERER::kInvalidMaterialDataIndex
                        ? 0u
                        : materialDataIndex);
            }
        }

        void PrepareShadowSurfaceGpuSceneMaterialFrame() {
            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& shadow =
                g.activeShadowSceneSource.GetPass(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
            PrepareShadowSurfaceGpuSceneMaterialSources(
                shadow.gpuSceneBaseIndex,
                shadow.materialSources);
            PrepareShadowSurfaceGpuSceneMaterialSources(
                shadow.traditionalIndirect.gpuSceneBaseIndex,
                shadow.traditionalIndirect.materialSources);
        }

        const RENDER3D::GPUDRIVEN::GpuDrivenPassSource* GetSourceShadowPass() {
            if (g.gpuDrivenSceneSource == nullptr) {
                return nullptr;
            }
            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass =
                g.gpuDrivenSceneSource->GetPass(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
            return pass.HasGpuSceneInstances()
                ? &pass
                : nullptr;
        }

        bool BuildShadowGpuDrivenSceneSources() {
            CPU_PROFILE::ScopedCpuTimer cpuTimer(
                CPU_PROFILE::Pass::ShadowSourceSync);

            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource* sourcePass =
                GetSourceShadowPass();
            if (sourcePass == nullptr ||
                !sourcePass->HasGpuSceneInstances()) {

                g.shadowSceneSource.Reset();
                g.staticShadowSceneSource.Reset();
                g.dynamicShadowSceneSource.Reset();
                g.activeShadowSceneSource.Reset();
                g.staticShadowPrimaryInstances.clear();
                g.staticShadowPrimaryMaterialSources.clear();
                g.dynamicShadowPrimaryInstances.clear();
                g.dynamicShadowPrimaryMaterialSources.clear();
                gShadowStaticTraditionalIndirectStream.Clear();
                gShadowDynamicTraditionalIndirectStream.Clear();
                InvalidateShadowSourceCache();
                return false;
            }

            if (CanReuseShadowSourceCache()) {
                g.frameHasStaticShadowWork =
                    g.staticShadowSceneSource.sourceInstanceCount != 0u;
                g.frameHasDynamicShadowWork =
                    g.dynamicShadowSceneSource.sourceInstanceCount != 0u;
                g.debugStats.shadowStaticSourceInstanceCount =
                    g.staticShadowSceneSource.sourceInstanceCount;
                g.debugStats.shadowDynamicSourceInstanceCount =
                    g.dynamicShadowSceneSource.sourceInstanceCount;
                return g.shadowSceneSource.sourceInstanceCount != 0u;
            }

            g.shadowSceneSource.Reset();
            g.staticShadowSceneSource.Reset();
            g.dynamicShadowSceneSource.Reset();
            g.activeShadowSceneSource.Reset();
            g.shadowSceneSource.meshShaderJointPalettes =
                g.gpuDrivenSceneSource->meshShaderJointPalettes;
            g.staticShadowSceneSource.meshShaderJointPalettes =
                g.gpuDrivenSceneSource->meshShaderJointPalettes;
            g.dynamicShadowSceneSource.meshShaderJointPalettes =
                g.gpuDrivenSceneSource->meshShaderJointPalettes;
            g.staticShadowPrimaryInstances.clear();
            g.staticShadowPrimaryMaterialSources.clear();
            g.dynamicShadowPrimaryInstances.clear();
            g.dynamicShadowPrimaryMaterialSources.clear();
            gShadowStaticTraditionalIndirectStream.Clear();
            gShadowDynamicTraditionalIndirectStream.Clear();

            RENDER3D::GPUDRIVEN::GpuDrivenPassSource& staticPass =
                g.staticShadowSceneSource.GetPass(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
            staticPass.Reset();
            staticPass.gpuSceneBaseIndex = 0;
            staticPass.preferredBackend =
                RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::MeshShader;
            staticPass.clusterEligible = sourcePass->clusterEligible;

            RENDER3D::GPUDRIVEN::GpuDrivenPassSource& dynamicPass =
                g.dynamicShadowSceneSource.GetPass(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
            dynamicPass.Reset();
            dynamicPass.gpuSceneBaseIndex = 0;
            dynamicPass.preferredBackend =
                RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::MeshShader;
            dynamicPass.clusterEligible = sourcePass->clusterEligible;

            if (sourcePass->instances != nullptr) {
                const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>* sourceMaterials =
                    sourcePass->materialSources;
                for (size_t sourceIndex = 0;
                    sourceIndex < sourcePass->instances->size();
                    ++sourceIndex) {

                    const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance =
                        (*sourcePass->instances)[sourceIndex];
                    if (!IsPrimaryShadowCaster(instance)) {
                        continue;
                    }

                    const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* material =
                        sourceMaterials != nullptr &&
                            sourceIndex < sourceMaterials->size()
                        ? &(*sourceMaterials)[sourceIndex]
                        : nullptr;
                    if (IsStaticPrimaryShadowCaster(instance)) {
                        AppendShadowPrimaryInstance(
                            instance,
                            material,
                            g.staticShadowPrimaryInstances,
                            g.staticShadowPrimaryMaterialSources);
                    } else {
                        AppendShadowPrimaryInstance(
                            instance,
                            material,
                            g.dynamicShadowPrimaryInstances,
                            g.dynamicShadowPrimaryMaterialSources);
                    }
                }
            }

            if (!g.staticShadowPrimaryInstances.empty()) {
                staticPass.instances = &g.staticShadowPrimaryInstances;
                staticPass.materialSources = &g.staticShadowPrimaryMaterialSources;
                staticPass.gpuSceneInstanceCount =
                    static_cast<uint32_t>((std::min)(
                        g.staticShadowPrimaryInstances.size(),
                        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
            }

            if (!g.dynamicShadowPrimaryInstances.empty()) {
                dynamicPass.instances = &g.dynamicShadowPrimaryInstances;
                dynamicPass.materialSources = &g.dynamicShadowPrimaryMaterialSources;
                dynamicPass.gpuSceneInstanceCount =
                    static_cast<uint32_t>((std::min)(
                        g.dynamicShadowPrimaryInstances.size(),
                        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
            }

            const size_t staticTraditionalCount =
                static_cast<size_t>(sourcePass->traditionalIndirect.staticCommandCount);
            if (staticTraditionalCount != 0u &&
                gShadowStaticTraditionalIndirectStream.CopyRangeFrom(
                    sourcePass->traditionalIndirect,
                    0u,
                    staticTraditionalCount,
                    false)) {
                HydrateShadowTraditionalIndirectStream(
                    gShadowStaticTraditionalIndirectStream);
                gShadowStaticTraditionalIndirectStream.gpuSceneBaseIndex =
                    staticPass.gpuSceneInstanceCount;
                gShadowStaticTraditionalIndirectStream.AttachTo(staticPass);
            }
            if (staticPass.gpuSceneInstanceCount == 0u &&
                staticPass.traditionalIndirect.HasCommands()) {
                staticPass.preferredBackend =
                    RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::TraditionalIndirect;
            }

            g.staticShadowSceneSource.sourceInstanceCount =
                staticPass.gpuSceneInstanceCount +
                staticPass.traditionalIndirect.gpuSceneInstanceCount;
            g.staticShadowSceneSource.layoutVersion =
                BuildShadowSourceLayoutHash(staticPass);
            g.staticShadowSceneSource.sourceVersion =
                BuildShadowSourceContentHash(staticPass);
            g.staticShadowSceneSource.dirtyBaseSourceVersion = 0u;

            const size_t skinnedTraditionalCount =
                static_cast<size_t>(sourcePass->traditionalIndirect.skinnedCommandCount);
            if (skinnedTraditionalCount != 0u &&
                gShadowDynamicTraditionalIndirectStream.CopyRangeFrom(
                    sourcePass->traditionalIndirect,
                    staticTraditionalCount,
                    skinnedTraditionalCount,
                    true)) {
                HydrateShadowTraditionalIndirectStream(
                    gShadowDynamicTraditionalIndirectStream);
                gShadowDynamicTraditionalIndirectStream.gpuSceneBaseIndex =
                    dynamicPass.gpuSceneInstanceCount;
                gShadowDynamicTraditionalIndirectStream.AttachTo(dynamicPass);
            }
            if (dynamicPass.gpuSceneInstanceCount == 0u &&
                dynamicPass.traditionalIndirect.HasCommands()) {
                dynamicPass.preferredBackend =
                    RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::TraditionalIndirect;
            }

            g.dynamicShadowSceneSource.sourceInstanceCount =
                dynamicPass.gpuSceneInstanceCount +
                dynamicPass.traditionalIndirect.gpuSceneInstanceCount;
            g.dynamicShadowSceneSource.layoutVersion =
                BuildShadowSourceLayoutHash(dynamicPass);
            g.dynamicShadowSceneSource.sourceVersion =
                BuildShadowSourceContentHash(dynamicPass);
            g.dynamicShadowSceneSource.dirtyBaseSourceVersion = 0u;

            RENDER3D::GPUDRIVEN::GpuDrivenPassSource& shadowPass =
                g.shadowSceneSource.GetPass(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
            shadowPass = *sourcePass;
            shadowPass.gpuSceneBaseIndex = 0;
            shadowPass.preferredBackend =
                RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::MeshShader;
            shadowPass.traditionalIndirect.gpuSceneBaseIndex =
                shadowPass.gpuSceneInstanceCount;

            g.shadowSceneSource.layoutVersion =
                g.gpuDrivenSceneSource != nullptr
                    ? g.gpuDrivenSceneSource->layoutVersion
                    : 0u;
            g.shadowSceneSource.sourceVersion =
                g.gpuDrivenSceneSource != nullptr
                    ? g.gpuDrivenSceneSource->sourceVersion
                    : 0u;
            g.shadowSceneSource.dirtyBaseSourceVersion =
                g.gpuDrivenSceneSource != nullptr
                    ? g.gpuDrivenSceneSource->dirtyBaseSourceVersion
                    : 0u;
            g.shadowSceneSource.sourceInstanceCount =
                shadowPass.gpuSceneInstanceCount +
                shadowPass.traditionalIndirect.gpuSceneInstanceCount;
            g.shadowSceneSource.meshShaderJointPalettes =
                g.gpuDrivenSceneSource->meshShaderJointPalettes;

            g.frameHasStaticShadowWork =
                g.staticShadowSceneSource.sourceInstanceCount != 0u;
            g.frameHasDynamicShadowWork =
                g.dynamicShadowSceneSource.sourceInstanceCount != 0u;
            g.debugStats.shadowStaticSourceInstanceCount =
                g.staticShadowSceneSource.sourceInstanceCount;
            g.debugStats.shadowDynamicSourceInstanceCount =
                g.dynamicShadowSceneSource.sourceInstanceCount;

            g.shadowSourceCache.Capture(*g.gpuDrivenSceneSource);
            return g.shadowSceneSource.sourceInstanceCount != 0;
        }

        void SyncShadowGpuDrivenBackendAvailability() {
            RENDER3D::GPUDRIVEN::GpuDrivenBackendAvailability availability{};
            const RENDER3D::MESHLET::MeshletRenderBackendStats& meshletStats =
                g.meshletRenderBackend.GetStats();
            availability.meshShaderForwardPipelineReady =
                meshletStats.shadowPipelineReady;
            availability.traditionalIndirectPipelineReady =
                g.rootSig != nullptr &&
                g.skinnedRootSig != nullptr &&
                g.staticPso != nullptr &&
                g.skinnedPso != nullptr;
            g.gpuDrivenLayer.SetBackendAvailability(availability);
        }

        size_t UploadJointPalette(size_t objectIndex, const std::vector<MATH::Mat4>& palette) {
            if (g.jointPaletteMapped == nullptr || objectIndex >= kMaxCasterObjects) {
                return 0;
            }

            JointPaletteCB cb{};
            for (MATH::Mat4& matrix : cb.jointMatrices) {
                matrix = MATH::Mat4::Identity();
            }
            const size_t uploadCount = std::min(palette.size(), kMaxJointPaletteMatrices);
            for (size_t i = 0; i < uploadCount; ++i) {
                cb.jointMatrices[i] = palette[i];
            }

            uint8_t* dst = reinterpret_cast<uint8_t*>(g.jointPaletteMapped) +
                static_cast<size_t>(GFX::AlignD3D12ConstantBufferByteSize(sizeof(JointPaletteCB))) * objectIndex;
            std::memcpy(dst, &cb, sizeof(cb));
            return uploadCount;
        }

        bool CreateMappedUploadBuffer(
            ID3D12Device* device,
            UINT64 byteSize,
            ComPtr<ID3D12Resource>& resource,
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
            ComPtr<ID3D12Resource>& resource) {

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
            ComPtr<ID3D12Resource>& uploadResource,
            ComPtr<ID3D12Resource>& defaultResource,
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

            commandList->CopyBufferRegion(defaultResource, 0, uploadResource, 0, byteCount);

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

        void CommitShadowMaterialDataFrame(ID3D12GraphicsCommandList* commandList) {
            ShadowFrameResources& frame =
                g.frameResources[g.activeFrameResourceIndex % GFX::kFrameResourceCount];
            const UINT64 materialBytes =
                static_cast<UINT64>(sizeof(MESHRENDERER::MaterialGpuData)) *
                static_cast<UINT64>(
                    (std::min)(
                        static_cast<size_t>(g.materialDataFrameTable.count),
                        static_cast<size_t>(MESHRENDERER::kMaxMaterialDataCount)));
            if (materialBytes == 0u) {
                return;
            }

            CommitMappedBufferToGpu(
                commandList,
                frame.materialDataUploadBuffer.Get(),
                frame.materialDataBuffer.Get(),
                frame.materialDataState,
                materialBytes);
        }

        void BindActiveFrameResources(uint32_t frameIndex) {
            g.activeFrameResourceIndex = frameIndex % GFX::kFrameResourceCount;
            ShadowFrameResources& frame = g.frameResources[g.activeFrameResourceIndex];

            g.cameraCB = frame.cameraCB;
            g.objectCB = frame.objectCB;
            g.materialDataUploadBuffer = frame.materialDataUploadBuffer;
            g.materialDataBuffer = frame.materialDataBuffer;
            g.jointPaletteCB = frame.jointPaletteCB;
            g.cameraMapped = frame.cameraMapped;
            g.objectMapped = frame.objectMapped;
            g.materialDataMapped = frame.materialDataMapped;
            g.jointPaletteMapped = frame.jointPaletteMapped;
            g.materialDataSrvCpu = frame.materialDataSrvCpu;
            g.materialDataSrvGpu = frame.materialDataSrvGpu;
        }

        bool CreateBuffers(ID3D12Device* device) {
            const UINT cameraBytes = GFX::AlignD3D12ConstantBufferByteSize(sizeof(ShadowCameraCB));
            const UINT objectBytes = GFX::AlignD3D12ConstantBufferByteSize(sizeof(ShadowObjectCB)) * kMaxCasterObjects;
            const UINT materialDataBytes =
                static_cast<UINT>(sizeof(MESHRENDERER::MaterialGpuData) * MESHRENDERER::kMaxMaterialDataCount);
            const UINT paletteBytes = GFX::AlignD3D12ConstantBufferByteSize(sizeof(JointPaletteCB)) * kMaxCasterObjects;

            ID3D12DescriptorHeap* srvHeap = SERVICES::gCtx.srvHeap;
            if (srvHeap == nullptr) {
                return false;
            }
            const UINT descriptorSize =
                device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            const UINT surfaceGpuSceneSrvIndex =
                GFX::DESCRIPTOR::ToFrameIndex(
                    GFX::DESCRIPTOR::SystemSrv::ShadowSurfaceGpuSceneFrame0,
                    0u);
            const D3D12_CPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrvCpu =
                GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, surfaceGpuSceneSrvIndex);
            const D3D12_GPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrvGpu =
                GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, surfaceGpuSceneSrvIndex);

            D3D12_SHADER_RESOURCE_VIEW_DESC materialDataSrv{};
            materialDataSrv.Format = DXGI_FORMAT_UNKNOWN;
            materialDataSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            materialDataSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            materialDataSrv.Buffer.FirstElement = 0;
            materialDataSrv.Buffer.NumElements = MESHRENDERER::kMaxMaterialDataCount;
            materialDataSrv.Buffer.StructureByteStride = sizeof(MESHRENDERER::MaterialGpuData);
            materialDataSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

            if (!g.surfaceGpuSceneBuffer.Initialize(
                device,
                surfaceGpuSceneSrvCpu,
                surfaceGpuSceneSrvGpu,
                descriptorSize)) {
                DEBUGLOG::PushRenderError("[ShadowMapRenderer][WARN] SurfaceGpuScene buffer initialization failed. GPU-driven shadow pass will be unavailable.");
            }

            for (uint32_t frameIndex = 0; frameIndex < GFX::kFrameResourceCount; ++frameIndex) {
                ShadowFrameResources& frame = g.frameResources[frameIndex];
                if (!CreateMappedUploadBuffer(
                    device,
                    cameraBytes,
                    frame.cameraCB,
                    reinterpret_cast<void**>(&frame.cameraMapped)) ||
                    !CreateMappedUploadBuffer(
                        device,
                        objectBytes,
                        frame.objectCB,
                        reinterpret_cast<void**>(&frame.objectMapped)) ||
                    !CreateGpuResidentMappedBuffer(
                        device,
                        materialDataBytes,
                        frame.materialDataUploadBuffer,
                        frame.materialDataBuffer,
                        reinterpret_cast<void**>(&frame.materialDataMapped)) ||
                    !CreateMappedUploadBuffer(
                        device,
                        paletteBytes,
                        frame.jointPaletteCB,
                        reinterpret_cast<void**>(&frame.jointPaletteMapped))) {
                    return false;
                }
                frame.materialDataState = D3D12_RESOURCE_STATE_COMMON;

                const UINT materialDataSrvIndex =
                    GFX::DESCRIPTOR::ToFrameIndex(
                        GFX::DESCRIPTOR::SystemSrv::ShadowMaterialDataFrame0,
                        frameIndex);
                frame.materialDataSrvCpu =
                    GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, materialDataSrvIndex);
                frame.materialDataSrvGpu =
                    GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, materialDataSrvIndex);
                device->CreateShaderResourceView(
                    frame.materialDataBuffer.Get(),
                    &materialDataSrv,
                    frame.materialDataSrvCpu);
                GFX::SetD3D12Name(frame.materialDataBuffer.Get(), L"Shadow MaterialData Buffer");
            }

            BindActiveFrameResources(SERVICES::gCtx.frameIndex);
            return true;
        }

        bool CreateShadowMap(uint32_t resolution) {
            auto* device = SERVICES::gCtx.device;
            if (device == nullptr || resolution == 0) {
                return false;
            }
            InvalidateShadowCache();

            RENDER3D::ReleaseTextureResource(g.shadowSrvResource);
            g.shadowSrvResource = {};
            g.shadowSrvHandle = -1;

            g.shadowMap.Reset();
            g.staticShadowMap.Reset();
            g.dsvHeap.Reset();
            g.staticShadowState = D3D12_RESOURCE_STATE_COMMON;
            g.shadowCache.SetFinalMatchesStaticCache(false);

            D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
            dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            dsvHeapDesc.NumDescriptors = 1;
            if (FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(g.dsvHeap.GetAddressOf())))) {
                return false;
            }

            auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
                DXGI_FORMAT_R32_TYPELESS,
                resolution,
                resolution,
                1,
                1,
                1,
                0,
                D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
            D3D12_CLEAR_VALUE clearValue{};
            clearValue.Format = DXGI_FORMAT_D32_FLOAT;
            clearValue.DepthStencil.Depth = 1.0f;
            clearValue.DepthStencil.Stencil = 0;
            const HRESULT hr = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &texDesc,
                kShadowShaderReadState,
                &clearValue,
                IID_PPV_ARGS(g.shadowMap.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "ShadowMapRenderer::CreateShadowMapResource")) {
                return false;
            }
            GFX::SetD3D12Name(g.shadowMap.Get(), L"Directional Shadow Map");

            const HRESULT staticHr = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &texDesc,
                D3D12_RESOURCE_STATE_COMMON,
                &clearValue,
                IID_PPV_ARGS(g.staticShadowMap.GetAddressOf()));
            if (!HIKARI_DX_CHECK(staticHr, "ShadowMapRenderer::CreateStaticShadowCacheResource")) {
                g.shadowMap.Reset();
                return false;
            }
            GFX::SetD3D12Name(g.staticShadowMap.Get(), L"Directional Static Shadow Cache");

            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            g.dsv = g.dsvHeap->GetCPUDescriptorHandleForHeapStart();
            device->CreateDepthStencilView(g.shadowMap.Get(), &dsvDesc, g.dsv);

            RENDER3D::RenderResourceDesc shadowSrvDesc{};
            shadowSrvDesc.kind = RENDER3D::RenderResourceKind::Texture;
            shadowSrvDesc.usage =
                RENDER3D::RenderResourceUsageFlags::ShaderResource |
                RENDER3D::RenderResourceUsageFlags::DepthStencil;
            shadowSrvDesc.lifetime = RENDER3D::RenderResourceLifetime::External;
            shadowSrvDesc.debugName = "directional_shadow_map_srv";
            shadowSrvDesc.sourceKey = "shadow/directional_map";
            shadowSrvDesc.width = resolution;
            shadowSrvDesc.height = resolution;
            shadowSrvDesc.mipLevels = 1;
            shadowSrvDesc.depthOrArraySize = 1;
            shadowSrvDesc.format = DXGI_FORMAT_R32_FLOAT;
            g.shadowSrvResource = RENDER3D::RegisterTextureResourceFromNative(
                g.shadowMap.Get(),
                DXGI_FORMAT_R32_FLOAT,
                std::move(shadowSrvDesc));
            g.shadowSrvHandle = RENDER3D::GetTextureResourceBackendHandle(g.shadowSrvResource);
            g.resolution = resolution;
            g.shadowState = kShadowShaderReadState;
            g.staticShadowState = D3D12_RESOURCE_STATE_COMMON;
            ++g.shadowMapRecreateCount;
            return RENDER3D::IsTextureResourceValid(g.shadowSrvResource);
        }

        bool CreatePipeline(ID3D12Device* device) {
            if (!GFX::SupportsShaderModel6(device)) {
                DEBUGLOG::PushRenderError("[Shadow][ERROR] Shader Model 6.0 is not supported by this device.");
                return false;
            }

            ComPtr<ID3DBlob> staticVs;
            ComPtr<ID3DBlob> skinnedVs;
            ComPtr<ID3DBlob> ps;
            if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_ShadowStaticVS.hlsl", "main", GFX::ShaderStage::Vertex, staticVs.GetAddressOf())) {
                DEBUGLOG::PushRenderError("[Shadow][ERROR] Compile ShadowStaticVS failed.");
                return false;
            }
            if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_ShadowSkinnedVS.hlsl", "main", GFX::ShaderStage::Vertex, skinnedVs.GetAddressOf())) {
                DEBUGLOG::PushRenderError("[Shadow][ERROR] Compile ShadowSkinnedVS failed.");
                return false;
            }
            if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_ShadowAlphaPS.hlsl", "main", GFX::ShaderStage::Pixel, ps.GetAddressOf())) {
                DEBUGLOG::PushRenderError("[Shadow][ERROR] Compile ShadowAlphaPS failed.");
                return false;
            }

            D3D12_DESCRIPTOR_RANGE textureRange{};
            textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            textureRange.NumDescriptors = 1;
            textureRange.BaseShaderRegister = 0;
            textureRange.RegisterSpace = 0;
            textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_DESCRIPTOR_RANGE materialDataRange{};
            materialDataRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            materialDataRange.NumDescriptors = 1;
            materialDataRange.BaseShaderRegister = 16;
            materialDataRange.RegisterSpace = 0;
            materialDataRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_DESCRIPTOR_RANGE surfaceGpuSceneRange{};
            surfaceGpuSceneRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            surfaceGpuSceneRange.NumDescriptors = 1;
            surfaceGpuSceneRange.BaseShaderRegister = 17;
            surfaceGpuSceneRange.RegisterSpace = 0;
            surfaceGpuSceneRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_DESCRIPTOR_RANGE objectDataRange{};
            objectDataRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            objectDataRange.NumDescriptors = 1;
            objectDataRange.BaseShaderRegister = 15;
            objectDataRange.RegisterSpace = 0;
            objectDataRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_DESCRIPTOR_RANGE texturePoolRange{};
            texturePoolRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            texturePoolRange.NumDescriptors = GFX::DESCRIPTOR::kUserSrvCount;
            texturePoolRange.BaseShaderRegister = 20;
            texturePoolRange.RegisterSpace = 0;
            texturePoolRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_DESCRIPTOR_RANGE clusterGeometryPoolRange{};
            clusterGeometryPoolRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            clusterGeometryPoolRange.NumDescriptors =
                GFX::DESCRIPTOR::kSystemSrvDynamicCount;
            clusterGeometryPoolRange.BaseShaderRegister = 0;
            clusterGeometryPoolRange.RegisterSpace = 1;
            clusterGeometryPoolRange.OffsetInDescriptorsFromTableStart =
                D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER params[RECORD::kShadowStaticRootParamDeformationPalettes + 1]{};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[0].Descriptor.ShaderRegister = 0;
            params[0].Descriptor.RegisterSpace = 0;
            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[1].Descriptor.ShaderRegister = 1;
            params[1].Descriptor.RegisterSpace = 0;
            params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[2].DescriptorTable.NumDescriptorRanges = 1;
            params[2].DescriptorTable.pDescriptorRanges = &textureRange;
            params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[3].DescriptorTable.NumDescriptorRanges = 1;
            params[3].DescriptorTable.pDescriptorRanges = &materialDataRange;
            params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[4].DescriptorTable.NumDescriptorRanges = 1;
            params[4].DescriptorTable.pDescriptorRanges = &surfaceGpuSceneRange;

            params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[5].Constants.ShaderRegister = 8;
            params[5].Constants.RegisterSpace = 0;
            // GPU-driven record path note.
            params[5].Constants.Num32BitValues =
                RENDER3D::GPUDRIVEN::kGpuTraditionalCommandStreamRootConstantCount;

            params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[6].DescriptorTable.NumDescriptorRanges = 1;
            params[6].DescriptorTable.pDescriptorRanges = &texturePoolRange;
            params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[7].Constants.ShaderRegister = 7;
            params[7].Constants.RegisterSpace = 0;
            // GPU-driven record path note.
            params[7].Constants.Num32BitValues = 1;
            params[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[8].DescriptorTable.NumDescriptorRanges = 1;
            params[8].DescriptorTable.pDescriptorRanges = &objectDataRange;
            params[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[9].DescriptorTable.NumDescriptorRanges = 1;
            params[9].DescriptorTable.pDescriptorRanges = &clusterGeometryPoolRange;
            params[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            params[10].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[10].Descriptor.ShaderRegister = 18;
            params[10].Descriptor.RegisterSpace = 0;
            params[RECORD::kShadowStaticRootParamMeshletVisibleClusterList].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_SRV;
            params[RECORD::kShadowStaticRootParamMeshletVisibleClusterList].ShaderVisibility =
                D3D12_SHADER_VISIBILITY_ALL;
            params[RECORD::kShadowStaticRootParamMeshletVisibleClusterList].Descriptor.ShaderRegister = 19;
            params[RECORD::kShadowStaticRootParamMeshletVisibleClusterList].Descriptor.RegisterSpace = 0;
            params[RECORD::kShadowStaticRootParamCullingCamera].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[RECORD::kShadowStaticRootParamCullingCamera].ShaderVisibility =
                D3D12_SHADER_VISIBILITY_ALL;
            params[RECORD::kShadowStaticRootParamCullingCamera].Descriptor.ShaderRegister = 9;
            params[RECORD::kShadowStaticRootParamCullingCamera].Descriptor.RegisterSpace = 0;
            params[RECORD::kShadowStaticRootParamDeformationPalettes].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_SRV;
            params[RECORD::kShadowStaticRootParamDeformationPalettes].ShaderVisibility =
                D3D12_SHADER_VISIBILITY_ALL;
            params[RECORD::kShadowStaticRootParamDeformationPalettes].Descriptor.ShaderRegister = 0;
            params[RECORD::kShadowStaticRootParamDeformationPalettes].Descriptor.RegisterSpace = 3;
            D3D12_STATIC_SAMPLER_DESC sampler{};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            sampler.ShaderRegister = 0;
            sampler.RegisterSpace = 0;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = static_cast<UINT>(std::size(params));
            rsDesc.pParameters = params;
            rsDesc.NumStaticSamplers = 1;
            rsDesc.pStaticSamplers = &sampler;
            rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ComPtr<ID3DBlob> sigBlob;
            ComPtr<ID3DBlob> errBlob;
            if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, sigBlob.GetAddressOf(), errBlob.GetAddressOf()))) {
                if (errBlob) OutputDebugStringA(static_cast<const char*>(errBlob->GetBufferPointer()));
                return false;
            }
            HRESULT hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(g.rootSig.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "CreateRootSignature: Shadow static")) {
                return false;
            }
            GFX::SetD3D12Name(g.rootSig.Get(), L"Shadow Static RootSignature");

            D3D12_ROOT_PARAMETER skinnedParams[RECORD::kShadowSkinnedRootParamJointPalette + 1]{};
            for (size_t i = 0; i < std::size(params); ++i) {
                skinnedParams[i] = params[i];
            }
            skinnedParams[RECORD::kShadowSkinnedRootParamJointPalette].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_CBV;
            skinnedParams[RECORD::kShadowSkinnedRootParamJointPalette].ShaderVisibility =
                D3D12_SHADER_VISIBILITY_ALL;
            skinnedParams[RECORD::kShadowSkinnedRootParamJointPalette].Descriptor.ShaderRegister = 3;
            skinnedParams[RECORD::kShadowSkinnedRootParamJointPalette].Descriptor.RegisterSpace = 0;

            D3D12_ROOT_SIGNATURE_DESC skinnedRsDesc = rsDesc;
            skinnedRsDesc.NumParameters = static_cast<UINT>(std::size(skinnedParams));
            skinnedRsDesc.pParameters = skinnedParams;
            sigBlob.Reset();
            errBlob.Reset();
            if (FAILED(D3D12SerializeRootSignature(&skinnedRsDesc, D3D_ROOT_SIGNATURE_VERSION_1, sigBlob.GetAddressOf(), errBlob.GetAddressOf()))) {
                if (errBlob) OutputDebugStringA(static_cast<const char*>(errBlob->GetBufferPointer()));
                return false;
            }
            hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(g.skinnedRootSig.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "CreateRootSignature: Shadow skinned")) {
                return false;
            }
            GFX::SetD3D12Name(g.skinnedRootSig.Get(), L"Shadow Skinned RootSignature");

            const D3D12_INPUT_ELEMENT_DESC staticInput[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, normal)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, tangent)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, u)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, uv1)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };
            const D3D12_INPUT_ELEMENT_DESC skinnedInput[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, normal)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, tangent)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, uv0)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, uv1)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, color0)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "JOINTS", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, joints)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "WEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, weights)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = g.rootSig.Get();
            psoDesc.VS = { staticVs->GetBufferPointer(), staticVs->GetBufferSize() };
            psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = TRUE;
            psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            psoDesc.InputLayout = { staticInput, static_cast<UINT>(std::size(staticInput)) };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 0;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = 1;
            hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(g.staticPso.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "Create PSO: Shadow static")) {
                return false;
            }
            GFX::SetD3D12Name(g.staticPso.Get(), L"Shadow Static PSO");

            D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedPsoDesc = psoDesc;
            skinnedPsoDesc.pRootSignature = g.skinnedRootSig.Get();
            skinnedPsoDesc.VS = { skinnedVs->GetBufferPointer(), skinnedVs->GetBufferSize() };
            skinnedPsoDesc.InputLayout = { skinnedInput, static_cast<UINT>(std::size(skinnedInput)) };
            hr = device->CreateGraphicsPipelineState(&skinnedPsoDesc, IID_PPV_ARGS(g.skinnedPso.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "Create PSO: Shadow skinned")) {
                return false;
            }
            GFX::SetD3D12Name(g.skinnedPso.Get(), L"Shadow Skinned PSO");
            if (!g.traditionalCommandStreamBuffer.Initialize(
                device,
                g.rootSig.Get(),
                RECORD::kShadowStaticRootParamSurfaceGpuSceneControl,
                RENDER3D::GPUDRIVEN::kGpuTraditionalCommandStreamRootConstantCount)) {
                DEBUGLOG::PushRenderError("[ShadowMapRenderer][WARN] Shadow indirect draw buffer initialization failed. Direct shadow record path will be used.");
            } else if (!g.traditionalCommandStreamBuffer.InitializeSkinnedCommandStream(
                device,
                g.skinnedRootSig.Get(),
                RECORD::kShadowStaticRootParamSurfaceGpuSceneControl,
                RECORD::kShadowSkinnedRootParamJointPalette)) {
                DEBUGLOG::PushRenderError("[ShadowMapRenderer][WARN] Shadow skinned indirect command signature initialization failed. Skinned GPU-driven shadow stream will be unavailable.");
            }
            if (!g.clusterGpuCullingPass.Initialize(
                device,
                g.rootSig.Get(),
                RECORD::kShadowStaticRootParamSurfaceGpuSceneControl,
                RENDER3D::GPUDRIVEN::kGpuTraditionalCommandStreamRootConstantCount)) {
                DEBUGLOG::PushRenderError("[ShadowMapRenderer][WARN] Shadow cluster GPU culling initialization failed. GPU-driven shadow pass will be unavailable.");
            }
            if (!g.meshletRenderBackend.Initialize(
                device,
                g.rootSig.Get(),
                RENDER3D::MESHLET::MeshletPipelineMask::ShadowRenderer)) {
                DEBUGLOG::PushRenderError("[ShadowMapRenderer][WARN] Shadow meshlet backend initialization failed. GPU-driven shadow mesh shader route will be unavailable.");
            }
            g.clusterGpuDrivenProducer.Attach(&g.clusterGpuCullingPass);
            g.gpuDrivenLayer.Attach(
                &g.surfaceGpuSceneBuffer,
                &g.traditionalCommandStreamBuffer,
                &g.clusterGpuDrivenProducer);
            if (!g.gpuDrivenLayer.Initialize(
                device,
                g.rootSig.Get(),
                RECORD::kShadowStaticRootParamSurfaceGpuSceneControl,
                RENDER3D::GPUDRIVEN::kGpuTraditionalCommandStreamRootConstantCount)) {
                DEBUGLOG::PushRenderError("[ShadowMapRenderer][WARN] Shadow GPU-driven layer initialization failed. Shadow draw backend will be unavailable.");
            }
            return true;
        }

        bool EnsureInitialized() {
            if (g.initialized) {
                return true;
            }
            auto* device = SERVICES::gCtx.device;
            if (device == nullptr) {
                return false;
            }
            g.fallbackTextureResource = RENDER3D::LoadTextureResource(
                "shadow/fallback_white",
                "HIKARI/white1x1.png");
            g.fallbackTextureHandle =
                RENDER3D::GetTextureResourceBackendHandle(g.fallbackTextureResource);
            g.initialized =
                RENDER3D::IsTextureResourceValid(g.fallbackTextureResource) &&
                CreateBuffers(device) &&
                RECORD::InitializeShadowRecordExecutor(device) &&
                CreatePipeline(device);
            return g.initialized;
        }

        void UploadShadowCameraConstants(const ShadowLightFrame& frame) {
            if (g.cameraMapped == nullptr) {
                return;
            }

            const FrameContext& frameContext = TIME::GetFrameContext();
            g.elapsedTimeSec += std::max(0.0f, frameContext.unscaledDt);
            const float resolution =
                static_cast<float>((std::max)(1u, g.resolution));
            g.cameraMapped->lightViewProj = frame.viewProj;
            g.cameraMapped->invLightViewProj = MATH::Inverse(frame.viewProj);
            g.cameraMapped->lightPosition = {
                frame.lightPosition.x,
                frame.lightPosition.y,
                frame.lightPosition.z,
                1.0f
            };
            g.cameraMapped->timeParams = {
                g.elapsedTimeSec,
                frameContext.unscaledDt,
                frameContext.gameDt,
                static_cast<float>(frameContext.frameIndex)
            };
            g.cameraMapped->screenParams = {
                resolution,
                resolution,
                1.0f / resolution,
                1.0f / resolution
            };
        }

        void SubmitDebugFrustum(const SceneEnvironment& environment, const Camera3D& camera) {
            if (!environment.directionalShadow.showDebugFrustum) {
                return;
            }

            const ShadowLightFrame frame =
                BuildShadowLightFrame(environment, camera, g.resolution);
            const MATH::Vec3 lightPos = frame.lightPosition;
            const MATH::Vec3 forward = frame.lightDirection;
            const MATH::Vec3 right = frame.right;
            const MATH::Vec3 actualUp = frame.up;
            const float half = std::max(1.0f, environment.directionalShadow.orthoSize) * 0.5f;
            const float nearPlane = std::max(0.001f, environment.directionalShadow.nearPlane);
            const float farPlane =
                std::max(nearPlane + 0.01f, ResolveShadowDepthSpan(environment, half * 2.0f));
            const MATH::Vec3 nearCenter = lightPos + forward * nearPlane;
            const MATH::Vec3 farCenter = lightPos + forward * farPlane;

            const std::array<MATH::Vec3, 8> corners = {
                nearCenter - right * half - actualUp * half,
                nearCenter + right * half - actualUp * half,
                nearCenter + right * half + actualUp * half,
                nearCenter - right * half + actualUp * half,
                farCenter - right * half - actualUp * half,
                farCenter + right * half - actualUp * half,
                farCenter + right * half + actualUp * half,
                farCenter - right * half + actualUp * half,
            };
            constexpr uint32_t color = 0xFFD45CFF;
            const auto submit = [&](int a, int b) {
                RENDERER3D::DEBUG::SubmitLine3D({
                    corners[static_cast<size_t>(a)],
                    corners[static_cast<size_t>(b)],
                    color,
                    RENDERER3D::DEBUG::DebugDepthMode::XRay
                });
            };
            submit(0, 1); submit(1, 2); submit(2, 3); submit(3, 0);
            submit(4, 5); submit(5, 6); submit(6, 7); submit(7, 4);
            submit(0, 4); submit(1, 5); submit(2, 6); submit(3, 7);
        }

        void RestoreMainRenderTarget() {
            if (POST::PostSystem::RebindCurrentRenderTarget()) {
                return;
            }

            auto* cmd = SERVICES::gCtx.cmdList;
            if (cmd == nullptr) {
                return;
            }
            cmd->OMSetRenderTargets(1, &SERVICES::gCtx.rtv, FALSE, &SERVICES::gCtx.dsv);
            D3D12_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(SERVICES::gCtx.backBufferWidth);
            viewport.Height = static_cast<float>(SERVICES::gCtx.backBufferHeight);
            viewport.MaxDepth = 1.0f;
            D3D12_RECT scissor{ 0, 0, SERVICES::gCtx.backBufferWidth, SERVICES::gCtx.backBufferHeight };
            cmd->RSSetViewports(1, &viewport);
            cmd->RSSetScissorRects(1, &scissor);
        }

        void TransitionResource(
            ID3D12GraphicsCommandList* cmd,
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES& currentState,
            D3D12_RESOURCE_STATES targetState) {

            if (cmd == nullptr || resource == nullptr || currentState == targetState) {
                return;
            }

            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                resource,
                currentState,
                targetState);
            cmd->ResourceBarrier(1, &barrier);
            currentState = targetState;
        }

        void PrepareFinalShadowMapForDepthWrite(ID3D12GraphicsCommandList* cmd, bool clearDepth) {
            if (cmd == nullptr || g.shadowMap == nullptr) {
                return;
            }

            TransitionResource(
                cmd,
                g.shadowMap.Get(),
                g.shadowState,
                D3D12_RESOURCE_STATE_DEPTH_WRITE);

            D3D12_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(g.resolution);
            viewport.Height = static_cast<float>(g.resolution);
            viewport.MaxDepth = 1.0f;
            D3D12_RECT scissor{
                0,
                0,
                static_cast<LONG>(g.resolution),
                static_cast<LONG>(g.resolution)
            };
            cmd->RSSetViewports(1, &viewport);
            cmd->RSSetScissorRects(1, &scissor);
            cmd->OMSetRenderTargets(0, nullptr, FALSE, &g.dsv);
            if (clearDepth) {
                cmd->ClearDepthStencilView(
                    g.dsv,
                    D3D12_CLEAR_FLAG_DEPTH,
                    1.0f,
                    0,
                    0,
                    nullptr);
            }
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
            if (srvHeap != nullptr) {
                ID3D12DescriptorHeap* heaps[] = { srvHeap };
                cmd->SetDescriptorHeaps(1, heaps);
            }
        }

        void FinishFinalShadowMap(ID3D12GraphicsCommandList* cmd) {
            TransitionResource(
                cmd,
                g.shadowMap.Get(),
                g.shadowState,
                kShadowShaderReadState);
            RestoreMainRenderTarget();
        }

        bool CopyStaticShadowCacheToFinal(ID3D12GraphicsCommandList* cmd) {
            if (cmd == nullptr ||
                g.staticShadowMap == nullptr ||
                g.shadowMap == nullptr ||
                !g.shadowCache.IsValid()) {
                return false;
            }

            if (!g.frameHasDynamicShadowWork &&
                g.shadowCache.FinalMatchesStaticCache() &&
                g.shadowState == kShadowShaderReadState) {
                return true;
            }

            TransitionResource(
                cmd,
                g.staticShadowMap.Get(),
                g.staticShadowState,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
            TransitionResource(
                cmd,
                g.shadowMap.Get(),
                g.shadowState,
                D3D12_RESOURCE_STATE_COPY_DEST);
            cmd->CopyResource(g.shadowMap.Get(), g.staticShadowMap.Get());
            g.shadowCache.RecordCopy(!g.frameHasDynamicShadowWork);
            PublishShadowCacheStats();
            return true;
        }

        bool UpdateStaticShadowCacheFromFinal(ID3D12GraphicsCommandList* cmd) {
            if (cmd == nullptr ||
                g.staticShadowMap == nullptr ||
                g.shadowMap == nullptr ||
                !g.frameHasStaticShadowWork) {
                return false;
            }

            TransitionResource(
                cmd,
                g.shadowMap.Get(),
                g.shadowState,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
            TransitionResource(
                cmd,
                g.staticShadowMap.Get(),
                g.staticShadowState,
                D3D12_RESOURCE_STATE_COPY_DEST);
            cmd->CopyResource(g.staticShadowMap.Get(), g.shadowMap.Get());
            TransitionResource(
                cmd,
                g.staticShadowMap.Get(),
                g.staticShadowState,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
            MarkShadowCacheValidAfterRender();
            return true;
        }

        void ClearFrameSubmissions() {
            g.debugStats = {};
            g.frameHasShadowWork = false;
            g.frameHasStaticShadowWork = false;
            g.frameHasDynamicShadowWork = false;
            g.shadowCache.BeginFrame();
            PublishShadowCacheStats();
        }

        void ResetShadowGpuDrivenWorkFrame();
        void BuildShadowGpuDrivenWorkFrame();
        void UploadShadowIndirectDrawFrame();

        bool UploadShadowGpuSceneFrame(
            const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source) {

            const bool hasShadowWork = source.HasAnyGpuSceneRanges();
            g.gpuDrivenLayer.BeginFrame(
                hasShadowWork
                    ? &source
                    : nullptr);

            RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadDesc uploadDesc{};
            uploadDesc.commandList = SERVICES::gCtx.cmdList;
            uploadDesc.frameIndex = SERVICES::gCtx.frameIndex;
            uploadDesc.allowDirtyRangePatching = true;
            const RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadStats& uploadStats =
                g.gpuDrivenLayer.UploadSceneFrame(uploadDesc);
            const RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBufferStats& gpuSceneStats =
                uploadStats.bufferStats;
            g.debugStats.shadowGpuSceneCapacity = gpuSceneStats.capacity;
            g.debugStats.shadowGpuSceneRequestedInstanceCount = gpuSceneStats.requestedInstanceCount;
            g.debugStats.shadowGpuSceneUploadedInstanceCount = gpuSceneStats.uploadedInstanceCount;
            g.debugStats.shadowGpuSceneOverflowInstanceCount = gpuSceneStats.overflowInstanceCount;
            g.debugStats.shadowGpuSceneUploadCallCount = gpuSceneStats.uploadCallCount;
            g.debugStats.shadowGpuSceneFullUploadCount =
                uploadStats.uploadedFullScene ? 1u : 0u;
            g.debugStats.shadowGpuSceneDirtyPatchCount =
                uploadStats.patchedDirtyRanges ? 1u : 0u;
            g.debugStats.shadowGpuSceneReuseCount =
                uploadStats.reusedResidentFrame ? 1u : 0u;
            g.debugStats.shadowGpuSceneSrvValid = gpuSceneStats.srv.ptr != 0;
            g.debugStats.shadowGpuSceneBufferReady = gpuSceneStats.initialized;
            return hasShadowWork;
        }

        bool PrepareShadowSourceForDraw(
            const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source) {

            if (!source.HasAnyGpuSceneRanges()) {
                ResetShadowGpuDrivenWorkFrame();
                return false;
            }

            // The built sources belong to the source cache.  A draw may select a
            // split source, but it must never replace the combined source used by
            // later frames and by the correctness fallback.
            g.activeShadowSceneSource = source;
            ResetShadowMaterialFrame();
            if (!UploadShadowGpuSceneFrame(g.activeShadowSceneSource)) {
                return false;
            }
            PrepareShadowSurfaceGpuSceneMaterialFrame();
            CommitShadowMaterialDataFrame(SERVICES::gCtx.cmdList);
            g.gpuDrivenLayer.CommitSurfaceGpuSceneMaterialFrame(SERVICES::gCtx.cmdList);
            BuildShadowGpuDrivenWorkFrame();
            UploadShadowIndirectDrawFrame();
            return true;
        }

        void ResetShadowGpuDrivenWorkFrame() {
            g.gpuDrivenFrame.Reset();
            g.clusterGpuDrivenProducer.BeginFrame(false);
            g.gpuDrivenLayer.ImportProducerOutput(
                g.clusterGpuDrivenProducer.BuildFrameOutput());
            g.meshletRenderBackend.ResetFrame();
            SyncShadowGpuDrivenBackendAvailability();
            RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDesc{};
            commandFrameDesc.commandList = SERVICES::gCtx.cmdList;
            commandFrameDesc.cullViewProj = &g.lightViewProj;
            commandFrameDesc.frameIndex = SERVICES::gCtx.frameIndex;
            g.gpuDrivenLayer.BuildCommandFrame(commandFrameDesc);
        }

        void BuildShadowGpuDrivenWorkFrame() {
            const RENDER3D::GPUDRIVEN::GpuDrivenFrameBuildInput input =
                RENDER3D::GPUDRIVEN::BuildGpuDrivenFrameInput(
                    g.activeShadowSceneSource);
            g.gpuDrivenFrame =
                RENDER3D::GPUDRIVEN::BuildGpuDrivenFrame(input);

            if (!g.activeShadowSceneSource.HasAnyGpuSceneRanges() ||
                !g.surfaceGpuSceneBuffer.GetStats().initialized ||
                g.surfaceGpuSceneBuffer.GetStats().overflowInstanceCount != 0) {
                ResetShadowGpuDrivenWorkFrame();
                return;
            }

            ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
            if (SERVICES::gCtx.cmdList != nullptr && srvHeap != nullptr) {
                ID3D12DescriptorHeap* heaps[] = { srvHeap };
                SERVICES::gCtx.cmdList->SetDescriptorHeaps(1, heaps);
            }

            g.clusterGpuDrivenProducer.BeginFrame(false);
            RENDER3D::GPUDRIVEN::GpuDrivenWorkContext workContext{};
            workContext.producer = &g.clusterGpuDrivenProducer;
            workContext.commandList = SERVICES::gCtx.cmdList;
            workContext.viewProj = g.lightViewProj;
            workContext.cameraPosition = g.lightCullPosition;
            workContext.geometryPoolSrv = RENDER3D::GetClusterGeometryPoolSrvGpuHandle(SERVICES::gCtx);
            workContext.surfaceGpuSceneGpuAddress =
                g.surfaceGpuSceneBuffer.GetGpuVirtualAddress();
            workContext.frame = &g.gpuDrivenFrame;
            (void)RENDER3D::GPUDRIVEN::BuildGpuDrivenWork(workContext);

            g.gpuDrivenLayer.ImportProducerOutput(
                g.clusterGpuDrivenProducer.BuildFrameOutput());
            g.gpuDrivenLayer.BuildCommandBuffers();
            g.meshletRenderBackend.ResetFrame();
            SyncShadowGpuDrivenBackendAvailability();
            RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDesc{};
            commandFrameDesc.commandList = SERVICES::gCtx.cmdList;
            commandFrameDesc.cullViewProj = &g.lightViewProj;
            commandFrameDesc.frameIndex = SERVICES::gCtx.frameIndex;
            g.gpuDrivenLayer.BuildCommandFrame(commandFrameDesc);
        }

        void UploadShadowIndirectDrawFrame() {
            RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDesc{};
            commandFrameDesc.commandList = SERVICES::gCtx.cmdList;
            commandFrameDesc.cullViewProj = &g.lightViewProj;
            commandFrameDesc.frameIndex = SERVICES::gCtx.frameIndex;
            g.gpuDrivenLayer.BuildCommandFrame(commandFrameDesc);

            const RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamStats& indirectStats =
                g.gpuDrivenLayer.GetCommandFrameStats().traditionalCommandStreamStats;
            g.debugStats.shadowIndirectCapacity = indirectStats.capacity;
            g.debugStats.shadowIndirectRequestedCommandCount = indirectStats.requestedCommandCount;
            g.debugStats.shadowIndirectUploadedCommandCount = indirectStats.uploadedCommandCount;
            g.debugStats.shadowIndirectOverflowCommandCount = indirectStats.overflowCommandCount;
            g.debugStats.shadowIndirectMissingDrawArgsCommandCount = indirectStats.missingDrawArgsCommandCount;
            g.debugStats.shadowIndirectArgumentBufferReady = indirectStats.initialized;
            g.debugStats.shadowIndirectCommandSignatureReady = indirectStats.commandSignatureReady;
        }

        bool ExecuteShadowGpuDrivenBackend(
            RENDER3D::GPUDRIVEN::GeometryBackendKind backend,
            GFX::GPU_PROFILE::Pass traditionalProfilePass,
            GFX::GPU_PROFILE::Pass meshletProfilePass) {

            ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
            if (cmd == nullptr) {
                return false;
            }

            const RENDER3D::GPUDRIVEN::GpuDrivenPassKind shadowPass =
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow;
            const RENDER3D::GPUDRIVEN::GeometryBackendContext backendContext =
                g.gpuDrivenLayer.BuildGeometryBackendContext(
                    cmd,
                    shadowPass,
                    backend);

            if (backend == RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVsPs) {
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

                bool executed = false;
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

                GFX::GPU_PROFILE::ScopedGpuTimer gpuDraw(
                    cmd,
                    traditionalProfilePass);
                if (hasStaticStream && g.staticPso != nullptr) {
                    BindShadowGpuDrivenFrameResources(cmd, nullptr);
                    cmd->SetPipelineState(g.staticPso.Get());
                    for (size_t bucketIndex = 0; bucketIndex < commandBucketCount; ++bucketIndex) {
                        cmd->ExecuteIndirect(
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
                    }
                    executed = true;
                }

                if (hasSkinnedStream && g.skinnedPso != nullptr) {
                    BindShadowGpuDrivenFrameResources(cmd, nullptr, nullptr, true);
                    cmd->SetPipelineState(g.skinnedPso.Get());
                    for (size_t bucketIndex = 0; bucketIndex < commandBucketCount; ++bucketIndex) {
                        cmd->ExecuteIndirect(
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
                    }
                    executed = true;
                }

                g.debugStats.submittedCasterCount += range->commandCount;
                g.debugStats.staticCasterDrawCount += range->staticCommandCount;
                g.debugStats.skinnedCasterDrawCount += range->skinnedCommandCount;
                g.debugStats.totalPrimitiveCasterDrawCount += range->commandCount;
                return executed;
            }

            ID3D12Resource* visibleRangeBuffer =
                backendContext.visibility != nullptr
                    ? backendContext.visibility->visibleMeshletRangeBuffer
                    : nullptr;
            ID3D12Resource* visibleClusterListBuffer =
                backendContext.visibility != nullptr
                    ? backendContext.visibility->visibleMeshletClusterListBuffer
                    : nullptr;
            BindShadowGpuDrivenFrameResources(
                cmd,
                visibleRangeBuffer,
                visibleClusterListBuffer);

            switch (backend) {
            case RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader: {
                if (visibleRangeBuffer == nullptr ||
                    visibleClusterListBuffer == nullptr) {
                    return false;
                }
                RENDER3D::MESHLET::MeshletRenderExecutionContext ctx{};
                ctx.commandList = backendContext.commandList;
                ctx.pass = backendContext.pass;
                ctx.visibility = backendContext.visibility;
                ctx.drawCommandRange = backendContext.drawCommandRange;
                ctx.pipelineKind = RENDER3D::MESHLET::MeshletPipelineKind::Shadow;
                ctx.profilePass = meshletProfilePass;
                const RENDER3D::MESHLET::MeshletRenderBackendStats beforeStats =
                    g.meshletRenderBackend.GetStats();
                const bool executed = g.meshletRenderBackend.Execute(ctx);
                const RENDER3D::MESHLET::MeshletRenderBackendStats afterStats =
                    g.meshletRenderBackend.GetStats();
                AccumulateShadowMeshletStatsDelta(beforeStats, afterStats);
                return executed;
            }
            default:
                return false;
            }
        }

        bool ExecuteShadowGpuDrivenPass(
            GFX::GPU_PROFILE::Pass traditionalProfilePass,
            GFX::GPU_PROFILE::Pass meshletProfilePass) {
            const RENDER3D::GPUDRIVEN::GpuDrivenPassKind shadowPass =
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow;
            SyncShadowGpuDrivenBackendAvailability();
            if (!g.gpuDrivenLayer.IsPassGpuReady(shadowPass)) {
                return false;
            }

            const RENDER3D::GPUDRIVEN::GeometryBackendExecutionPlan plan =
                g.gpuDrivenLayer.GetPassExecutionPlan(shadowPass);
            bool executed = false;
            for (size_t i = 0; i < plan.gpuBackendCount; ++i) {
                if (ExecuteShadowGpuDrivenBackend(
                        plan.gpuBackends[i],
                        traditionalProfilePass,
                        meshletProfilePass)) {
                    executed = true;
                }
            }
            return executed;
        }

    }

    void Reset() {
        ClearFrameSubmissions();
        InvalidateShadowCache();
        SetGpuDrivenSceneSource(nullptr);
    }

    void BeginFrame(const SceneEnvironment& environment, const Camera3D& camera) {
        CPU_PROFILE::ScopedCpuTimer cpuTimer(
            CPU_PROFILE::Pass::ShadowPrepare);
        ClearFrameSubmissions();
        g.frameEnabled = environment.directional.enabled && environment.directionalShadow.enabled;
        g.debugStats.enabled = g.frameEnabled;
        g.debugStats.resolution = ResolveShadowResolution(environment.directionalShadow.resolution);
        g.debugStats.worldTexelSize =
            std::max(1.0f, environment.directionalShadow.orthoSize) /
            static_cast<float>((std::max)(1u, g.debugStats.resolution));
        g.debugStats.shadowMapRecreateCount = g.shadowMapRecreateCount;
        g.debugStats.pcfEnabled = environment.directionalShadow.pcfEnabled ? 1u : 0u;
        g.debugStats.pcfRadius = environment.directionalShadow.pcfRadius;
        g.debugStats.orthoSize = environment.directionalShadow.orthoSize;
        g.debugStats.nearPlane = environment.directionalShadow.nearPlane;
        g.debugStats.farPlane =
            ResolveShadowDepthSpan(environment, std::max(1.0f, environment.directionalShadow.orthoSize));
        g.debugStats.depthBias = environment.directionalShadow.depthBias;
        g.debugStats.normalBias = environment.directionalShadow.normalBias;
        g.debugStats.strength = environment.directionalShadow.strength;
        if (!g.frameEnabled) {
            PublishShadowCacheStats();
            return;
        }
        g.frameHasShadowWork = BuildShadowGpuDrivenSceneSources();
        if (!g.frameHasShadowWork) {
            PublishShadowCacheStats();
            return;
        }
        if (!EnsureInitialized()) {
            g.frameEnabled = false;
            g.debugStats.enabled = false;
            g.frameHasShadowWork = false;
            InvalidateShadowCache();
            return;
        }
        BindActiveFrameResources(SERVICES::gCtx.frameIndex);
        UploadShadowMeshShaderJointPalettes();

        const uint32_t resolution = ResolveShadowResolution(environment.directionalShadow.resolution);
        if (g.shadowMap == nullptr || g.resolution != resolution) {
            if (!CreateShadowMap(resolution)) {
                g.frameEnabled = false;
                g.debugStats.enabled = false;
                g.frameHasShadowWork = false;
                InvalidateShadowCache();
                return;
            }
            g.debugStats.shadowMapRecreateCount = g.shadowMapRecreateCount;
        }
        const ShadowLightFrame shadowFrame =
            BuildShadowLightFrame(environment, camera, resolution);
        g.lightViewProj = shadowFrame.viewProj;
        g.lightCullPosition = shadowFrame.lightPosition;
        g.lightAnchor = shadowFrame.anchor;
        g.lightAnchorGrid = shadowFrame.anchorGrid;
        UploadShadowCameraConstants(shadowFrame);
        if (CanReuseShadowCache(resolution)) {
            MarkShadowCacheHit();
            SubmitDebugFrustum(environment, camera);
            return;
        }

        MarkShadowCacheMiss();
        SubmitDebugFrustum(environment, camera);
    }

    void SetGpuDrivenSceneSource(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source) {

        g.gpuDrivenSceneSource = source;
        if (source == nullptr) {
            g.shadowSceneSource.Reset();
            g.staticShadowSceneSource.Reset();
            g.dynamicShadowSceneSource.Reset();
            g.activeShadowSceneSource.Reset();
            g.staticShadowPrimaryInstances.clear();
            g.staticShadowPrimaryMaterialSources.clear();
            g.dynamicShadowPrimaryInstances.clear();
            g.dynamicShadowPrimaryMaterialSources.clear();
            gShadowStaticTraditionalIndirectStream.Clear();
            gShadowDynamicTraditionalIndirectStream.Clear();
            InvalidateShadowSourceCache();
        }
    }

    void InvalidateSceneCache() {
        g.shadowSceneSource.Reset();
        g.staticShadowSceneSource.Reset();
        g.dynamicShadowSceneSource.Reset();
        g.activeShadowSceneSource.Reset();
        gShadowStaticTraditionalIndirectStream.Clear();
        gShadowDynamicTraditionalIndirectStream.Clear();
        // Keep the static depth cache as a candidate.  The next rebuilt static
        // source is checked against its content key before any reuse, allowing a
        // dynamic-only refresh to keep valid static shadows without accepting
        // stale static geometry.
        InvalidateShadowSourceCache();
    }

    void RenderDirectionalShadowMap() {
        if (!g.frameEnabled || !g.frameHasShadowWork || g.shadowMap == nullptr) {
            return;
        }
        auto* cmd = SERVICES::gCtx.cmdList;
        if (cmd == nullptr) {
            return;
        }
        GFX::GPU_PROFILE::ScopedGpuTimer gpuShadow(cmd, GFX::GPU_PROFILE::Pass::ShadowMap);

        bool finalHasDepth = false;
        bool staticRendered = false;
        bool dynamicRendered = false;
        bool unifiedRendered = false;
        bool fallbackRendered = false;

        if (g.shadowCache.WasHitThisFrame()) {
            if (!CopyStaticShadowCacheToFinal(cmd)) {
                MarkShadowCacheMiss();
            } else {
                finalHasDepth = true;
            }
        }

        // The shadow GPU-driven context owns one transient upload/cull submission
        // per frame.  Preparing static and dynamic sources back-to-back would make
        // both queued GPU copies read the last CPU upload page, corrupting the
        // static cache exactly when a skinned caster enters the scene.  If there
        // is no reusable static depth, render the combined source once instead.
        const bool needsUnifiedDynamicRender =
            g.frameHasDynamicShadowWork && !finalHasDepth;
        if (needsUnifiedDynamicRender) {
            PrepareFinalShadowMapForDepthWrite(cmd, true);
            if (PrepareShadowSourceForDraw(g.shadowSceneSource)) {
                unifiedRendered = ExecuteShadowGpuDrivenPass(
                    GFX::GPU_PROFILE::Pass::TraditionalDrawShadowFallback,
                    GFX::GPU_PROFILE::Pass::MeshletDrawShadowFallback);
            }
            finalHasDepth = unifiedRendered;
            g.shadowCache.SetFinalMatchesStaticCache(false);
        } else {
            if (!finalHasDepth && g.frameHasStaticShadowWork) {
                PrepareFinalShadowMapForDepthWrite(cmd, true);
                if (PrepareShadowSourceForDraw(g.staticShadowSceneSource)) {
                    staticRendered = ExecuteShadowGpuDrivenPass(
                        GFX::GPU_PROFILE::Pass::TraditionalDrawShadowStatic,
                        GFX::GPU_PROFILE::Pass::MeshletDrawShadowStatic);
                }
                if (staticRendered) {
                    finalHasDepth = true;
                    (void)UpdateStaticShadowCacheFromFinal(cmd);
                    g.shadowCache.SetFinalMatchesStaticCache(true);
                } else {
                    InvalidateShadowCache();
                }
            }

            const bool staticSplitFailed =
                !g.shadowCache.WasHitThisFrame() &&
                g.frameHasStaticShadowWork &&
                !staticRendered;

            if (!staticSplitFailed && g.frameHasDynamicShadowWork) {
                PrepareFinalShadowMapForDepthWrite(cmd, false);
                if (PrepareShadowSourceForDraw(g.dynamicShadowSceneSource)) {
                    dynamicRendered = ExecuteShadowGpuDrivenPass(
                        GFX::GPU_PROFILE::Pass::TraditionalDrawShadowDynamic,
                        GFX::GPU_PROFILE::Pass::MeshletDrawShadowDynamic);
                }
                if (dynamicRendered) {
                    g.shadowCache.SetFinalMatchesStaticCache(false);
                } else {
                    // Keep the copied static depth this frame.  Invalidating the
                    // cache makes the next frame use the single-submit unified path.
                    InvalidateShadowCache();
                }
            }

            if (staticSplitFailed) {
                PrepareFinalShadowMapForDepthWrite(cmd, true);
                if (PrepareShadowSourceForDraw(g.shadowSceneSource)) {
                    fallbackRendered = ExecuteShadowGpuDrivenPass(
                        GFX::GPU_PROFILE::Pass::TraditionalDrawShadowFallback,
                        GFX::GPU_PROFILE::Pass::MeshletDrawShadowFallback);
                }
                finalHasDepth = fallbackRendered;
                if (fallbackRendered) {
                    g.shadowCache.SetFinalMatchesStaticCache(false);
                }
            }
        }

        if (!finalHasDepth) {
            PrepareFinalShadowMapForDepthWrite(cmd, true);
            finalHasDepth = true;
        }

        g.debugStats.shadowStaticRendered = staticRendered;
        g.debugStats.shadowDynamicRendered = dynamicRendered;
        g.debugStats.shadowUnifiedRendered = unifiedRendered;
        g.debugStats.shadowFallbackRendered = fallbackRendered;
        FinishFinalShadowMap(cmd);
    }

    bool IsDirectionalShadowEnabled() {
        return g.frameEnabled &&
            g.frameHasShadowWork &&
            g.shadowMap != nullptr &&
            RENDER3D::IsTextureResourceValid(g.shadowSrvResource);
    }

    const MATH::Mat4& GetDirectionalLightViewProj() {
        return g.lightViewProj;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetDirectionalShadowSrv() {
        return RENDER3D::GetTextureResourceSrvGpuHandle(g.shadowSrvResource);
    }

    uint32_t GetShadowResolution() {
        return g.resolution;
    }

    float GetShadowStrength() {
        return 0.75f;
    }

    float GetDepthBias() {
        return 0.001f;
    }

    float GetNormalBias() {
        return 0.02f;
    }

    const ShadowMapDebugStats& GetDebugStats() {
        return g.debugStats;
    }

} // namespace HIKARI::SHADOW
