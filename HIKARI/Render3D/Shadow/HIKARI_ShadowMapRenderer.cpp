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
#include <vector>

#include <d3dcompiler.h>
#include <d3dx12.h>
#include <wrl/client.h>

#include "HIKARI_Services.h"
#include "Core/HIKARI_TimeService.h"
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
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Shadow/HIKARI_ShadowRecordExecutor.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::SHADOW {

    using Microsoft::WRL::ComPtr;

    namespace {
        constexpr UINT kMaxCasterObjects = 2048u;
        constexpr size_t kMaxJointPaletteMatrices = 128u;

        constexpr UINT AlignConstantBufferSize(size_t size) {
            return static_cast<UINT>((size + 255u) & ~255u);
        }

        struct ShadowCameraCB {
            MATH::Mat4 lightViewProj{};
            MATH::Mat4 invLightViewProj{};
            MATH::Vec4 lightPosition{};
            MATH::Vec4 timeParams{};
            MATH::Vec4 screenParams{};
        };

        struct ShadowLightFrame {
            MATH::Mat4 view{};
            MATH::Mat4 viewProj{};
            MATH::Vec3 anchor{};
            MATH::Vec3 lightPosition{};
            MATH::Vec3 lightDirection{};
            MATH::Vec3 right{};
            MATH::Vec3 up{};
            float anchorGrid = 0.0f;
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

        struct State {
            bool initialized = false;
            bool frameEnabled = false;
            bool frameHasShadowWork = false;
            uint32_t resolution = 0;
            D3D12_RESOURCE_STATES shadowState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            MATH::Mat4 lightViewProj = MATH::Mat4::Identity();
            MATH::Vec3 lightCullPosition{};
            MATH::Vec3 lightAnchor{};
            float lightAnchorGrid = 0.0f;
            float elapsedTimeSec = 0.0f;

            ComPtr<ID3D12Resource> shadowMap;
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
            MESHRENDERER::MaterialDataFrameTable materialDataFrameTable{};

            const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* gpuDrivenSceneSource = nullptr;
            RENDER3D::GPUDRIVEN::GpuDrivenSceneSource shadowSceneSource{};
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
            bool shadowCacheValid = false;
            bool shadowCacheHitThisFrame = false;
            uint64_t shadowCacheLayoutVersion = 0;
            uint64_t shadowCacheSourceVersion = 0;
            size_t shadowCacheSourceInstanceCount = 0;
            uint32_t shadowCacheResolution = 0;
            MATH::Mat4 shadowCacheLightViewProj = MATH::Mat4::Identity();
            size_t shadowCacheHitCount = 0;
            size_t shadowCacheMissCount = 0;
        };

        State g;

        bool AlmostEqualMat4(const MATH::Mat4& lhs, const MATH::Mat4& rhs) {
            constexpr float kEpsilon = 0.0001f;
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    if (std::fabs(lhs.m[col][row] - rhs.m[col][row]) > kEpsilon) {
                        return false;
                    }
                }
            }
            return true;
        }

        void PublishShadowCacheStats() {
            g.debugStats.shadowCacheValid = g.shadowCacheValid;
            g.debugStats.shadowCacheHit = g.shadowCacheHitThisFrame;
            g.debugStats.shadowCacheHitCount = g.shadowCacheHitCount;
            g.debugStats.shadowCacheMissCount = g.shadowCacheMissCount;
        }

        void InvalidateShadowCache() {
            g.shadowCacheValid = false;
            g.shadowCacheHitThisFrame = false;
            PublishShadowCacheStats();
        }

        bool HasTraditionalShadowWork() {
            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& shadowPass =
                g.shadowSceneSource.GetPass(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
            return
                shadowPass.traditionalIndirect.HasCommands() ||
                shadowPass.traditionalIndirect.HasGpuSceneInstances();
        }

        bool CanReuseShadowCache(uint32_t resolution) {
            return
                g.shadowCacheValid &&
                g.shadowMap != nullptr &&
                RENDER3D::IsTextureResourceValid(g.shadowSrvResource) &&
                g.shadowState == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE &&
                !HasTraditionalShadowWork() &&
                (g.gpuDrivenSceneSource == nullptr ||
                    !g.gpuDrivenSceneSource->HasAnyDirtyGpuSceneRanges()) &&
                g.shadowCacheResolution == resolution &&
                g.shadowCacheLayoutVersion == g.shadowSceneSource.layoutVersion &&
                g.shadowCacheSourceVersion == g.shadowSceneSource.sourceVersion &&
                g.shadowCacheSourceInstanceCount == g.shadowSceneSource.sourceInstanceCount &&
                AlmostEqualMat4(g.shadowCacheLightViewProj, g.lightViewProj);
        }

        void MarkShadowCacheHit() {
            g.shadowCacheHitThisFrame = true;
            ++g.shadowCacheHitCount;
            PublishShadowCacheStats();
        }

        void MarkShadowCacheMiss() {
            g.shadowCacheHitThisFrame = false;
            g.shadowCacheValid = false;
            ++g.shadowCacheMissCount;
            PublishShadowCacheStats();
        }

        void MarkShadowCacheValidAfterRender() {
            g.shadowCacheValid = true;
            g.shadowCacheHitThisFrame = false;
            g.shadowCacheLayoutVersion = g.shadowSceneSource.layoutVersion;
            g.shadowCacheSourceVersion = g.shadowSceneSource.sourceVersion;
            g.shadowCacheSourceInstanceCount = g.shadowSceneSource.sourceInstanceCount;
            g.shadowCacheResolution = g.resolution;
            g.shadowCacheLightViewProj = g.lightViewProj;
            PublishShadowCacheStats();
        }

        struct OwnedShadowTraditionalIndirectStream {
            std::vector<RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord> records{};
            std::vector<uint32_t> executableRecordIndices{};
            std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand> commands{};
            std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance> instances{};
            std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource> materialSources{};
            std::vector<std::vector<MATH::Mat4>> jointPalettes{};
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
                gpuSceneBaseIndex = view.gpuSceneBaseIndex;
                gpuSceneInstanceCount = view.gpuSceneInstanceCount;
                staticCommandCount = view.staticCommandCount;
                skinnedCommandCount = view.skinnedCommandCount;
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
                pass.traditionalIndirect.gpuSceneBaseIndex = gpuSceneBaseIndex;
                pass.traditionalIndirect.gpuSceneInstanceCount =
                    gpuSceneInstanceCount;
                pass.traditionalIndirect.staticCommandCount =
                    staticCommandCount;
                pass.traditionalIndirect.skinnedCommandCount =
                    skinnedCommandCount;
            }
        };

        OwnedShadowTraditionalIndirectStream gShadowTraditionalIndirectStream{};

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
                static_cast<UINT64>(AlignConstantBufferSize(sizeof(JointPaletteCB))) *
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
                static_cast<size_t>(AlignConstantBufferSize(sizeof(JointPaletteCB))) *
                    objectIndex;
            std::memcpy(dst, &cb, sizeof(cb));
            return uploadCount;
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

        D3D12_GPU_DESCRIPTOR_HANDLE ResolveMaterialTexturePoolSrv() {
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
                GFX::DESCRIPTOR::kUserSrvBegin);
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
                ResolveMaterialTexturePoolSrv();
            if (texturePoolSrv.ptr != 0) {
                cmd->SetGraphicsRootDescriptorTable(
                    RECORD::kShadowStaticRootParamTexturePool,
                    texturePoolSrv);
            }
            const D3D12_GPU_DESCRIPTOR_HANDLE clusterPoolSrv =
                ResolveClusterGeometryPoolSrv();
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

        uint64_t BuildShadowMaterialDataKey(
            uint64_t stableMaterialKey,
            const MESHRENDERER::MaterialGpuData& data) {

            uint64_t seed = HashAppend(1469598103934665603ull, stableMaterialKey);
            return HashBytes(seed, &data, sizeof(data));
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
                g.shadowSceneSource.GetPass(
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

        bool BuildShadowGpuDrivenSceneSource() {
            g.shadowSceneSource.Reset();
            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource* sourcePass =
                GetSourceShadowPass();
            if (sourcePass == nullptr ||
                !sourcePass->HasGpuSceneInstances()) {
                return false;
            }

            RENDER3D::GPUDRIVEN::GpuDrivenPassSource& shadowPass =
                g.shadowSceneSource.GetPass(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
            shadowPass = *sourcePass;
            shadowPass.gpuSceneBaseIndex = 0;
            shadowPass.preferredBackend =
                RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::MeshShader;
            shadowPass.dirtyRanges.clear();
            shadowPass.traditionalIndirect.gpuSceneBaseIndex =
                shadowPass.gpuSceneInstanceCount;
            (void)gShadowTraditionalIndirectStream.CopyFrom(
                shadowPass.traditionalIndirect);
            HydrateShadowTraditionalIndirectStream(
                gShadowTraditionalIndirectStream);
            gShadowTraditionalIndirectStream.AttachTo(shadowPass);

            g.shadowSceneSource.layoutVersion =
                g.gpuDrivenSceneSource != nullptr
                    ? g.gpuDrivenSceneSource->layoutVersion
                    : 0u;
            g.shadowSceneSource.sourceVersion =
                g.gpuDrivenSceneSource != nullptr
                    ? g.gpuDrivenSceneSource->sourceVersion
                    : 0u;
            g.shadowSceneSource.sourceInstanceCount =
                shadowPass.gpuSceneInstanceCount +
                shadowPass.traditionalIndirect.gpuSceneInstanceCount;
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
                static_cast<size_t>(AlignConstantBufferSize(sizeof(JointPaletteCB))) * objectIndex;
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
            const UINT cameraBytes = AlignConstantBufferSize(sizeof(ShadowCameraCB));
            const UINT objectBytes = AlignConstantBufferSize(sizeof(ShadowObjectCB)) * kMaxCasterObjects;
            const UINT materialDataBytes =
                static_cast<UINT>(sizeof(MESHRENDERER::MaterialGpuData) * MESHRENDERER::kMaxMaterialDataCount);
            const UINT paletteBytes = AlignConstantBufferSize(sizeof(JointPaletteCB)) * kMaxCasterObjects;

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
            g.dsvHeap.Reset();

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
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                &clearValue,
                IID_PPV_ARGS(g.shadowMap.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "ShadowMapRenderer::CreateShadowMapResource")) {
                return false;
            }
            GFX::SetD3D12Name(g.shadowMap.Get(), L"Directional Shadow Map");

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
            g.shadowState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
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

            D3D12_ROOT_PARAMETER params[RECORD::kShadowStaticRootParamCullingCamera + 1]{};
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

        uint32_t ResolveShadowResolution(uint32_t resolution) {
            if (resolution <= 1024) {
                return 1024;
            }
            if (resolution <= 2048) {
                return 2048;
            }
            return 4096;
        }

        float SnapShadowAnchorValue(float value, float grid) {
            if (grid <= 0.0001f) {
                return value;
            }
            return std::round(value / grid) * grid;
        }

        float ResolveShadowAnchorGrid(
            const SceneEnvironment& environment,
            float orthoSize) {

            if (!environment.directionalShadow.stabilize ||
                g.resolution == 0u) {
                return 0.0f;
            }

            constexpr float kAnchorGridFraction = 1.0f / 8.0f;
            constexpr float kMinGridTexels = 64.0f;
            const float texelWorldSize =
                orthoSize / static_cast<float>((std::max)(1u, g.resolution));
            return (std::max)(
                orthoSize * kAnchorGridFraction,
                texelWorldSize * kMinGridTexels);
        }

        MATH::Vec3 ResolveLightDirection(const SceneEnvironment& environment) {
            MATH::Vec3 lightDir = MATH::Normalize(environment.directional.direction);
            if (MATH::Length(lightDir) <= 1e-6f) {
                lightDir = MATH::Normalize(MATH::Vec3{ 0.4f, -1.0f, -0.6f });
            }
            return lightDir;
        }

        ShadowLightFrame BuildShadowLightFrame(
            const SceneEnvironment& environment,
            const Camera3D& camera) {

            ShadowLightFrame frame{};
            const MATH::Vec3 lightDir = ResolveLightDirection(environment);
            MATH::Vec3 up{ 0.0f, 1.0f, 0.0f };
            if (std::abs(MATH::Dot(lightDir, up)) > 0.95f) {
                up = { 1.0f, 0.0f, 0.0f };
            }

            MATH::Vec3 right = MATH::Normalize(MATH::Cross(up, lightDir));
            if (MATH::Length(right) <= 1e-6f) {
                right = { 1.0f, 0.0f, 0.0f };
            }
            MATH::Vec3 actualUp = MATH::Normalize(MATH::Cross(lightDir, right));
            if (MATH::Length(actualUp) <= 1e-6f) {
                actualUp = up;
            }

            const float orthoSize = std::max(1.0f, environment.directionalShadow.orthoSize);
            const float nearPlane = std::max(0.001f, environment.directionalShadow.nearPlane);
            const float farPlane = std::max(nearPlane + 0.01f, environment.directionalShadow.farPlane);
            const float anchorGrid = ResolveShadowAnchorGrid(environment, orthoSize);

            const MATH::Vec3 cameraCenter = camera.GetPosition();
            MATH::Vec3 anchor = cameraCenter;
            if (anchorGrid > 0.0f) {
                const float snappedX =
                    SnapShadowAnchorValue(MATH::Dot(cameraCenter, right), anchorGrid);
                const float snappedY =
                    SnapShadowAnchorValue(MATH::Dot(cameraCenter, actualUp), anchorGrid);
                const float snappedZ =
                    SnapShadowAnchorValue(MATH::Dot(cameraCenter, lightDir), anchorGrid);
                anchor =
                    right * snappedX +
                    actualUp * snappedY +
                    lightDir * snappedZ;
            }

            const float lightDistance =
                std::max(1.0f, environment.directionalShadow.shadowDistance);
            const MATH::Vec3 lightPos = anchor - lightDir * lightDistance;
            frame.view = MATH::Mat4::LookAtRH(lightPos, anchor, actualUp);
            frame.viewProj =
                MATH::Mat4::OrthoRH_ZO(orthoSize, orthoSize, nearPlane, farPlane) *
                frame.view;
            frame.anchor = anchor;
            frame.lightPosition = lightPos;
            frame.lightDirection = lightDir;
            frame.right = right;
            frame.up = actualUp;
            frame.anchorGrid = anchorGrid;
            return frame;
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
                BuildShadowLightFrame(environment, camera);
            const MATH::Vec3 lightPos = frame.lightPosition;
            const MATH::Vec3 forward = frame.lightDirection;
            const MATH::Vec3 right = frame.right;
            const MATH::Vec3 actualUp = frame.up;
            const float half = std::max(1.0f, environment.directionalShadow.orthoSize) * 0.5f;
            const float nearPlane = std::max(0.001f, environment.directionalShadow.nearPlane);
            const float farPlane = std::max(nearPlane + 0.01f, environment.directionalShadow.farPlane);
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

        void ClearFrameSubmissions() {
            g.debugStats = {};
            g.frameHasShadowWork = false;
            g.shadowCacheHitThisFrame = false;
            PublishShadowCacheStats();
        }

        void CountSkippedNoCastShadow() {
            if (g.frameEnabled) {
                ++g.debugStats.skippedNoCastShadowCount;
            }
        }

        bool UploadShadowGpuSceneFrame() {
            g.gpuDrivenLayer.BeginFrame(
                g.frameHasShadowWork &&
                    g.shadowSceneSource.HasAnyGpuSceneRanges()
                    ? &g.shadowSceneSource
                    : nullptr);

            RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadDesc uploadDesc{};
            uploadDesc.commandList = SERVICES::gCtx.cmdList;
            uploadDesc.frameIndex = SERVICES::gCtx.frameIndex;
            const RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadStats& uploadStats =
                g.gpuDrivenLayer.UploadSceneFrame(uploadDesc);
            const RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBufferStats& gpuSceneStats =
                uploadStats.bufferStats;
            g.debugStats.shadowGpuSceneCapacity = gpuSceneStats.capacity;
            g.debugStats.shadowGpuSceneRequestedInstanceCount = gpuSceneStats.requestedInstanceCount;
            g.debugStats.shadowGpuSceneUploadedInstanceCount = gpuSceneStats.uploadedInstanceCount;
            g.debugStats.shadowGpuSceneOverflowInstanceCount = gpuSceneStats.overflowInstanceCount;
            g.debugStats.shadowGpuSceneUploadCallCount = gpuSceneStats.uploadCallCount;
            g.debugStats.shadowGpuSceneSrvValid = gpuSceneStats.srv.ptr != 0;
            g.debugStats.shadowGpuSceneBufferReady = gpuSceneStats.initialized;
            return g.frameHasShadowWork;
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
                    g.shadowSceneSource);
            g.gpuDrivenFrame =
                RENDER3D::GPUDRIVEN::BuildGpuDrivenFrame(input);

            if (!g.shadowSceneSource.HasAnyGpuSceneRanges() ||
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
            workContext.geometryPoolSrv = ResolveClusterGeometryPoolSrv();
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
            RENDER3D::GPUDRIVEN::GeometryBackendKind backend) {

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
                return g.meshletRenderBackend.Execute(ctx);
            }
            default:
                return false;
            }
        }

        bool ExecuteShadowGpuDrivenPass() {
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
                if (ExecuteShadowGpuDrivenBackend(plan.gpuBackends[i])) {
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
        ClearFrameSubmissions();
        g.frameEnabled = environment.directional.enabled && environment.directionalShadow.enabled;
        g.debugStats.enabled = g.frameEnabled;
        g.debugStats.resolution = ResolveShadowResolution(environment.directionalShadow.resolution);
        g.debugStats.shadowMapRecreateCount = g.shadowMapRecreateCount;
        g.debugStats.pcfEnabled = environment.directionalShadow.pcfEnabled ? 1u : 0u;
        g.debugStats.pcfRadius = environment.directionalShadow.pcfRadius;
        g.debugStats.orthoSize = environment.directionalShadow.orthoSize;
        g.debugStats.nearPlane = environment.directionalShadow.nearPlane;
        g.debugStats.farPlane = environment.directionalShadow.farPlane;
        g.debugStats.depthBias = environment.directionalShadow.depthBias;
        g.debugStats.normalBias = environment.directionalShadow.normalBias;
        g.debugStats.strength = environment.directionalShadow.strength;
        if (!g.frameEnabled) {
            InvalidateShadowCache();
            return;
        }
        g.frameHasShadowWork = BuildShadowGpuDrivenSceneSource();
        if (!g.frameHasShadowWork) {
            InvalidateShadowCache();
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
            BuildShadowLightFrame(environment, camera);
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
        ResetShadowMaterialFrame();
        if (!UploadShadowGpuSceneFrame()) {
            InvalidateShadowCache();
            return;
        }
        PrepareShadowSurfaceGpuSceneMaterialFrame();
        CommitShadowMaterialDataFrame(SERVICES::gCtx.cmdList);
        g.gpuDrivenLayer.CommitSurfaceGpuSceneMaterialFrame(SERVICES::gCtx.cmdList);
        BuildShadowGpuDrivenWorkFrame();
        UploadShadowIndirectDrawFrame();
        SubmitDebugFrustum(environment, camera);
    }

    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform, bool castShadow) {
        (void)asset;
        (void)transform;
        if (!castShadow) {
            CountSkippedNoCastShadow();
        }
    }

    void SubmitStaticSubmesh(
        const ModelAsset& asset,
        const Transform3D& transform,
        uint32_t meshIndex,
        uint32_t primitiveIndex,
        bool castShadow) {

        (void)asset;
        (void)transform;
        (void)meshIndex;
        (void)primitiveIndex;
        if (!castShadow) {
            CountSkippedNoCastShadow();
        }
    }

    void SubmitSkinnedMesh(const ModelAsset& asset, const Transform3D& transform, const std::vector<MATH::Mat4>& jointPalette, bool castShadow) {
        (void)asset;
        (void)transform;
        (void)jointPalette;
        if (!castShadow) {
            CountSkippedNoCastShadow();
        }
    }

    void SubmitSkinnedSubmesh(
        const ModelAsset& asset,
        const Transform3D& transform,
        const std::vector<MATH::Mat4>& jointPalette,
        uint32_t meshIndex,
        uint32_t primitiveIndex,
        bool castShadow) {

        (void)asset;
        (void)transform;
        (void)jointPalette;
        (void)meshIndex;
        (void)primitiveIndex;
        if (!castShadow) {
            CountSkippedNoCastShadow();
        }
    }

    void SetGpuDrivenSceneSource(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source) {

        g.gpuDrivenSceneSource = source;
        if (source == nullptr) {
            g.shadowSceneSource.Reset();
            gShadowTraditionalIndirectStream.Clear();
            InvalidateShadowCache();
        }
    }

    void RenderDirectionalShadowMap() {
        if (!g.frameEnabled || !g.frameHasShadowWork || g.shadowMap == nullptr) {
            return;
        }
        auto* cmd = SERVICES::gCtx.cmdList;
        if (cmd == nullptr) {
            return;
        }
        if (g.shadowCacheHitThisFrame) {
            if (g.shadowState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
                auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                    g.shadowMap.Get(),
                    g.shadowState,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                cmd->ResourceBarrier(1, &barrier);
                g.shadowState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            }
            return;
        }
        GFX::GPU_PROFILE::ScopedGpuTimer gpuShadow(cmd, GFX::GPU_PROFILE::Pass::ShadowMap);

        if (g.shadowState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(g.shadowMap.Get(), g.shadowState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            cmd->ResourceBarrier(1, &barrier);
            g.shadowState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }

        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(g.resolution);
        viewport.Height = static_cast<float>(g.resolution);
        viewport.MaxDepth = 1.0f;
        D3D12_RECT scissor{ 0, 0, static_cast<LONG>(g.resolution), static_cast<LONG>(g.resolution) };
        cmd->RSSetViewports(1, &viewport);
        cmd->RSSetScissorRects(1, &scissor);
        cmd->OMSetRenderTargets(0, nullptr, FALSE, &g.dsv);
        cmd->ClearDepthStencilView(g.dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
        if (srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            cmd->SetDescriptorHeaps(1, heaps);
        }

        auto finishShadowRender = [&]() {
            if (g.shadowState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
                auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(g.shadowMap.Get(), g.shadowState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                cmd->ResourceBarrier(1, &barrier);
                g.shadowState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            }
            RestoreMainRenderTarget();
        };

        const bool executed = ExecuteShadowGpuDrivenPass();
        finishShadowRender();
        if (executed) {
            MarkShadowCacheValidAfterRender();
        } else {
            InvalidateShadowCache();
        }
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
