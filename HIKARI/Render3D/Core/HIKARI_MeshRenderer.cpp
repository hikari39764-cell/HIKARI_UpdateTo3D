#include "HIKARI_MeshRenderer.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <d3dx12.h>
#include <wrl/client.h>

#include "HIKARI_DxTexture.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "HIKARI_Services.h"
#include "Core/HIKARI_TimeService.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshDrawExecutor.h"
#include "Render3D/Core/HIKARI_MeshRendererBindings.h"
#include "Render3D/Core/HIKARI_MeshRendererPso.h"
#include "Render3D/Core/HIKARI_MeshRendererRootParams.h"
#include "Render3D/Core/HIKARI_MeshRendererUpload.h"
#include "Render3D/Pipeline/HIKARI_RenderQueue.h"
#include "Render3D/Resources/HIKARI_ResourceStateTracker.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::MESHRENDERER {

    using Microsoft::WRL::ComPtr;

    namespace {
        struct State {
            bool initialized = false;
            MeshPipelineStore pipelines;
            RENDER3D::ResourceStateTracker resourceStates;
            ComPtr<ID3D12Resource> cameraCB;
            ComPtr<ID3D12Resource> objectCB;
            ComPtr<ID3D12Resource> lightCB;
            ComPtr<ID3D12Resource> shadowCB;
            ComPtr<ID3D12Resource> skyEnvironmentCB;
            ComPtr<ID3D12Resource> jointPaletteCB;
            CameraCB* cameraMapped = nullptr;
            ObjectCB* objectMapped = nullptr;
            LightCB* lightMapped = nullptr;
            ShadowCB* shadowMapped = nullptr;
            SkyEnvironmentCB* skyEnvironmentMapped = nullptr;
            JointPaletteCB* jointPaletteMapped = nullptr;
            std::vector<DrawItem> drawItems;
            MeshRendererDebugStats debugStats;
            int fallbackTextureHandle = -1;
            int fallbackNormalTextureHandle = -1;
            int fallbackBlackTextureHandle = -1;
            std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>> primitiveMeshCache;
            std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>> primitiveSkinnedMeshCache;
            std::unordered_map<std::string, int> materialTextureCache;
            float elapsedTimeSec = 0.0f;
        };

        State g;

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

        void ApplyProfileToVariant(const MaterialFxProfile& profile, VFX::VariantKey& variant) {
            if (!profile.shaderProfileId.empty()) {
                variant.shaderId = profile.shaderProfileId;
            }
            if (!profile.vertexShaderId.empty()) {
                variant.vertexShaderId = profile.vertexShaderId;
            }
            if (!profile.pixelShaderId.empty()) {
                variant.pixelShaderId = profile.pixelShaderId;
            }
            variant.featureBits = profile.featureBits;
            variant.composite = profile.composite;
            variant.depthTest = profile.depthTest;
            variant.depthWrite = profile.depthWrite;
            variant.doubleSided = profile.doubleSided;
        }

        void ApplyMaterialFxOverride(DrawItem& item) {
            item.hasResolvedMaterialFxProfile = false;
            item.resolvedMaterialFxProfile = {};

            if (item.materialFxProfileId.empty()) {
                return;
            }

            MaterialFxProfile profile{};
            if (!MaterialFxProfile::LoadById(item.materialFxProfileId, profile)) {
                return;
            }

            item.hasResolvedMaterialFxProfile = true;
            item.resolvedMaterialFxProfile = std::move(profile);
            ApplyProfileToVariant(item.resolvedMaterialFxProfile, item.variant);
        }

        void ResolveDrawVariant(DrawItem& item) {
            if (!item.asset) {
                return;
            }
            const Material* material = item.asset->GetMaterial();
            if (material) {
                item.variant.shaderId = material->GetShaderProfileId();
                item.variant.featureBits = material->GetFeatureBits();
            }
            item.variant.composite = VFX::CompositeMode::Alpha;
            item.variant.depthTest = true;
            item.variant.depthWrite = true;
            item.variant.doubleSided = false;

            ApplyMaterialFxOverride(item);

            item.fxValues = {};
            item.fxFlags = 0;
            if (!item.materialFxValuesInitialized) {
                return;
            }
            for (size_t i = 0; i < item.fxValues.size(); ++i) {
                const DirectX::XMFLOAT4& value = item.materialFxParamValues[i];
                item.fxValues[i] = { value.x, value.y, value.z, value.w };
            }
            item.fxFlags = item.variant.featureBits;
        }

        VFX::VariantKey ResolvePrimitiveVariant(const DrawItem& item, const MaterialAsset* materialAsset) {
            VFX::VariantKey variant = item.variant;

            if (materialAsset != nullptr) {
                variant.shaderId = materialAsset->shaderProfileId;
                variant.featureBits = materialAsset->featureBits;
                variant.doubleSided = materialAsset->doubleSided;
            }

            if (item.hasResolvedMaterialFxProfile) {
                ApplyProfileToVariant(item.resolvedMaterialFxProfile, variant);
            }

            return variant;
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

            g.fallbackTextureHandle = DXTEX::DxTextureManager::LoadTexture("mesh_renderer/fallback_white", "HIKARI/white1x1.png");
            g.fallbackNormalTextureHandle = DXTEX::DxTextureManager::LoadTexture("mesh_renderer/fallback_normal", "HIKARI/normal_flat_1x1.png");
            if (g.fallbackNormalTextureHandle < 0) {
                g.fallbackNormalTextureHandle = g.fallbackTextureHandle;
            }
            g.fallbackBlackTextureHandle = g.fallbackTextureHandle;

            g.initialized = true;
            return true;
        }

        int ResolvePrimitiveTextureHandle(const ModelAsset& asset, const MaterialAsset* materialAsset) {
            if (materialAsset == nullptr) {
                return g.fallbackTextureHandle;
            }

            const int textureIndex = materialAsset->baseColorTexture.textureIndex;
            if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
                return g.fallbackTextureHandle;
            }

            const std::string& texturePath = asset.textures[static_cast<size_t>(textureIndex)].sourcePath;
            if (texturePath.empty()) {
                return g.fallbackTextureHandle;
            }

            auto found = g.materialTextureCache.find(texturePath);
            if (found != g.materialTextureCache.end()) {
                ++g.debugStats.materialTextureCacheHitCount;
                return found->second;
            }

            ++g.debugStats.materialTextureCacheMissCount;
            const int handle = DXTEX::DxTextureManager::LoadTexture("model_material/" + texturePath, texturePath);
            g.materialTextureCache[texturePath] = handle;
            return handle >= 0 ? handle : g.fallbackTextureHandle;
        }

        int ResolvePrimitiveNormalTextureHandle(const ModelAsset& asset, const MaterialAsset* materialAsset) {
            if (materialAsset == nullptr) {
                return g.fallbackNormalTextureHandle;
            }

            const int textureIndex = materialAsset->normalTexture.textureIndex;
            if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
                ++g.debugStats.normalMapFallbackCount;
                return g.fallbackNormalTextureHandle;
            }

            const std::string& texturePath = asset.textures[static_cast<size_t>(textureIndex)].sourcePath;
            if (texturePath.empty()) {
                ++g.debugStats.normalMapFallbackCount;
                return g.fallbackNormalTextureHandle;
            }

            const std::string cacheKey = "normal:" + texturePath;
            auto found = g.materialTextureCache.find(cacheKey);
            if (found != g.materialTextureCache.end()) {
                ++g.debugStats.normalTextureCacheHitCount;
                return found->second >= 0 ? found->second : g.fallbackNormalTextureHandle;
            }

            ++g.debugStats.normalTextureCacheMissCount;
            const int handle = DXTEX::DxTextureManager::LoadTexture("model_material/normal/" + texturePath, texturePath);
            g.materialTextureCache[cacheKey] = handle;
            return handle >= 0 ? handle : g.fallbackNormalTextureHandle;
        }

        int ResolvePrimitiveEmissiveTextureHandle(const ModelAsset& asset, const MaterialAsset* materialAsset) {
            if (materialAsset == nullptr) {
                return g.fallbackBlackTextureHandle;
            }

            const int textureIndex = materialAsset->emissiveTexture.textureIndex;
            if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
                ++g.debugStats.emissiveMapFallbackCount;
                return g.fallbackBlackTextureHandle;
            }

            const std::string& texturePath = asset.textures[static_cast<size_t>(textureIndex)].sourcePath;
            if (texturePath.empty()) {
                ++g.debugStats.emissiveMapFallbackCount;
                return g.fallbackBlackTextureHandle;
            }

            const std::string cacheKey = "emissive:" + texturePath;
            auto found = g.materialTextureCache.find(cacheKey);
            if (found != g.materialTextureCache.end()) {
                ++g.debugStats.emissiveTextureCacheHitCount;
                return found->second >= 0 ? found->second : g.fallbackBlackTextureHandle;
            }

            ++g.debugStats.emissiveTextureCacheMissCount;
            const int handle = DXTEX::DxTextureManager::LoadTexture("model_material/emissive/" + texturePath, texturePath);
            g.materialTextureCache[cacheKey] = handle;
            return handle >= 0 ? handle : g.fallbackBlackTextureHandle;
        }

        int ResolvePrimitiveMetallicRoughnessTextureHandle(const ModelAsset& asset, const MaterialAsset* materialAsset) {
            if (materialAsset == nullptr) {
                ++g.debugStats.metallicRoughnessFallbackCount;
                return g.fallbackTextureHandle;
            }

            const int textureIndex = materialAsset->metallicRoughnessTexture.textureIndex;
            if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
                ++g.debugStats.metallicRoughnessFallbackCount;
                return g.fallbackTextureHandle;
            }

            const std::string& texturePath = asset.textures[static_cast<size_t>(textureIndex)].sourcePath;
            if (texturePath.empty()) {
                ++g.debugStats.metallicRoughnessFallbackCount;
                return g.fallbackTextureHandle;
            }

            const std::string cacheKey = "metallicRoughness:" + texturePath;
            auto found = g.materialTextureCache.find(cacheKey);
            if (found != g.materialTextureCache.end()) {
                ++g.debugStats.metallicRoughnessTextureCacheHitCount;
                return found->second >= 0 ? found->second : g.fallbackTextureHandle;
            }

            ++g.debugStats.metallicRoughnessTextureCacheMissCount;
            const int handle = DXTEX::DxTextureManager::LoadTexture("model_material/metallic_roughness/" + texturePath, texturePath);
            g.materialTextureCache[cacheKey] = handle;
            if (handle < 0) {
                DEBUGLOG::PushRenderError(std::string("[MeshRenderer][PBRTexture][WARN] metallicRoughness texture failed. material=") +
                    materialAsset->name + " sourcePath=" + texturePath + " fallback used");
            }
            return handle >= 0 ? handle : g.fallbackTextureHandle;
        }

        int ResolvePrimitiveOcclusionTextureHandle(const ModelAsset& asset, const MaterialAsset* materialAsset) {
            if (materialAsset == nullptr) {
                ++g.debugStats.occlusionFallbackCount;
                return g.fallbackTextureHandle;
            }

            const int textureIndex = materialAsset->occlusionTexture.textureIndex;
            if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
                ++g.debugStats.occlusionFallbackCount;
                return g.fallbackTextureHandle;
            }

            const std::string& texturePath = asset.textures[static_cast<size_t>(textureIndex)].sourcePath;
            if (texturePath.empty()) {
                ++g.debugStats.occlusionFallbackCount;
                return g.fallbackTextureHandle;
            }

            const std::string cacheKey = "occlusion:" + texturePath;
            auto found = g.materialTextureCache.find(cacheKey);
            if (found != g.materialTextureCache.end()) {
                ++g.debugStats.occlusionTextureCacheHitCount;
                return found->second >= 0 ? found->second : g.fallbackTextureHandle;
            }

            ++g.debugStats.occlusionTextureCacheMissCount;
            const int handle = DXTEX::DxTextureManager::LoadTexture("model_material/occlusion/" + texturePath, texturePath);
            g.materialTextureCache[cacheKey] = handle;
            if (handle < 0) {
                DEBUGLOG::PushRenderError(std::string("[MeshRenderer][PBRTexture][WARN] occlusion texture failed. material=") +
                    materialAsset->name + " sourcePath=" + texturePath + " fallback used");
            }
            return handle >= 0 ? handle : g.fallbackTextureHandle;
        }

        MATH::Vec4 SanitizeTangent(const MATH::Vec4& tangent) {
            const float lenSq =
                tangent.x * tangent.x +
                tangent.y * tangent.y +
                tangent.z * tangent.z;
            if (lenSq <= 1e-8f) {
                return { 1.0f, 0.0f, 0.0f, 1.0f };
            }
            return tangent;
        }

        Mesh* GetOrCreatePrimitiveMesh(const MeshPrimitive& primitive) {
            auto found = g.primitiveMeshCache.find(&primitive);
            if (found != g.primitiveMeshCache.end()) {
                ++g.debugStats.primitiveMeshCacheHitCount;
                return found->second.get();
            }

            if (primitive.layout != VertexLayoutKind::StaticPNTT || primitive.staticVertices.empty() || primitive.indices.empty()) {
                return nullptr;
            }

            ++g.debugStats.primitiveMeshCacheMissCount;
            std::vector<VertexStatic3D> vertices;
            vertices.reserve(primitive.staticVertices.size());
            for (const Vertex3D& src : primitive.staticVertices) {
                VertexStatic3D dst{};
                dst.position = src.position;
                dst.normal = src.normal;
                dst.tangent = SanitizeTangent(src.tangent);
                dst.u = src.uv0.x;
                dst.v = src.uv0.y;
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
                ++g.debugStats.primitiveSkinnedMeshCacheHitCount;
                return found->second.get();
            }

            if (primitive.skinnedVertices.empty() || primitive.indices.empty()) {
                return nullptr;
            }

            ++g.debugStats.primitiveSkinnedMeshCacheMissCount;
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

        bool PrepareMeshFrame(const Camera3D& camera, const SceneEnvironment& environment) {
            if (g.cameraMapped == nullptr || g.lightMapped == nullptr || g.shadowMapped == nullptr || g.skyEnvironmentMapped == nullptr) {
                return false;
            }

            g.cameraMapped->viewProj = camera.GetViewProj();
            const MATH::Vec3 cameraPos = camera.GetPosition();
            g.cameraMapped->cameraPos = { cameraPos.x, cameraPos.y, cameraPos.z, 1.0f };
            const FrameContext& frame = TIME::GetFrameContext();
            g.elapsedTimeSec += std::max(0.0f, frame.unscaledDt);
            g.cameraMapped->timeParams = { g.elapsedTimeSec, frame.unscaledDt, frame.gameDt, static_cast<float>(frame.frameIndex) };

            FillLightCB(environment, *g.lightMapped, g.debugStats);
            FillShadowCB(environment, *g.shadowMapped);
            FillSkyEnvironmentCB(*g.skyEnvironmentMapped);
            return true;
        }

        MeshDrawContext BuildDrawContext(bool depthAwarePhase) {
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
            ctx.binding.cmd = ctx.cmd;
            ctx.binding.depthAwarePhase = depthAwarePhase;
            ctx.binding.fallbackTextureHandle = g.fallbackTextureHandle;
            ctx.binding.fallbackNormalTextureHandle = g.fallbackNormalTextureHandle;
            ctx.materialFill.fallbackTextureHandle = g.fallbackTextureHandle;
            ctx.materialFill.fallbackNormalTextureHandle = g.fallbackNormalTextureHandle;
            ctx.materialFill.fallbackBlackTextureHandle = g.fallbackBlackTextureHandle;
            ctx.materialFill.stats = &g.debugStats;
            ctx.stats = &g.debugStats;
            ctx.getPrimitiveMesh = [](const MeshPrimitive& primitive) {
                return GetOrCreatePrimitiveMesh(primitive);
            };
            ctx.getSkinnedPrimitiveMesh = [](const MeshPrimitive& primitive) {
                return GetOrCreateSkinnedPrimitiveMesh(primitive);
            };
            ctx.resolveBaseColorTexture = [](const ModelAsset& asset, const MaterialAsset* materialAsset) {
                return ResolvePrimitiveTextureHandle(asset, materialAsset);
            };
            ctx.resolveNormalTexture = [](const ModelAsset& asset, const MaterialAsset* materialAsset) {
                return ResolvePrimitiveNormalTextureHandle(asset, materialAsset);
            };
            ctx.resolveEmissiveTexture = [](const ModelAsset& asset, const MaterialAsset* materialAsset) {
                return ResolvePrimitiveEmissiveTextureHandle(asset, materialAsset);
            };
            ctx.resolveMetallicRoughnessTexture = [](const ModelAsset& asset, const MaterialAsset* materialAsset) {
                return ResolvePrimitiveMetallicRoughnessTextureHandle(asset, materialAsset);
            };
            ctx.resolveOcclusionTexture = [](const ModelAsset& asset, const MaterialAsset* materialAsset) {
                return ResolvePrimitiveOcclusionTextureHandle(asset, materialAsset);
            };
            ctx.resolvePrimitiveVariant = [](const DrawItem& item, const MaterialAsset* materialAsset) {
                return ResolvePrimitiveVariant(item, materialAsset);
            };
            ctx.getOrCreatePso = [](const VFX::VariantKey& key, bool skinned, bool wireframe) {
                return GetOrCreateVariantPso(g.pipelines, SERVICES::gCtx.device, g.debugStats, key, skinned, wireframe);
            };
            return ctx;
        }

        bool RenderMeshPhase(const RENDER3D::RenderQueue& queue, RENDER3D::RenderPhase phase, size_t& objectIndex) {
            const bool depthAwarePhase = phase == RENDER3D::RenderPhase::DepthAware;
            const MeshDrawContext drawCtx = BuildDrawContext(depthAwarePhase);

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

        struct DepthAwarePhaseScope {
            bool active = false;
            bool postSystem = false;
            D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        };

        bool BeginDepthAwarePhase(DepthAwarePhaseScope& scope) {
            scope = {};

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

            g.resourceStates.Transition(
                cmd,
                sceneDepthResource,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

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

            g.resourceStates.Transition(
                cmd,
                sceneDepthResource,
                D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_DEPTH_WRITE);

            D3D12_CPU_DESCRIPTOR_HANDLE writableDsv = SERVICES::gCtx.dsv;
            cmd->OMSetRenderTargets(1, &rtv, FALSE, &writableDsv);
        }
    }

    void Reset() {
        g.drawItems.clear();
        g.debugStats = {};
    }

    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4(&materialFxParamValues)[VFX::kMaterialFxUserCount], bool materialFxValuesInitialized, bool receiveShadow, MeshRenderDebugMode renderDebugMode) {
        DrawItem item{};
        item.asset = &asset;
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

    void SubmitSkinnedMesh(const ModelAsset& asset, const Transform3D& transform, const std::vector<MATH::Mat4>& jointPalette, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4(&materialFxParamValues)[VFX::kMaterialFxUserCount], bool materialFxValuesInitialized, bool receiveShadow, MeshRenderDebugMode renderDebugMode) {
        DrawItem item{};
        item.asset = &asset;
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

    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment) {
        if (g.drawItems.empty()) {
            return;
        }
        if (!EnsureInitialized()) {
            return;
        }
        if (!PrepareMeshFrame(camera, environment)) {
            return;
        }

        ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
        if (cmd == nullptr || g.objectMapped == nullptr || g.objectCB == nullptr) {
            return;
        }

        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12DescriptorHeap* srvHeap = DXTEX::DxTextureManager::GetSrvHeap();
        if (srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            cmd->SetDescriptorHeaps(1, heaps);
        }

        RENDER3D::RenderQueue queue;
        queue.Build(g.drawItems);

        size_t objectIndex = 0;
        const bool opaqueOk = RenderMeshPhase(queue, RENDER3D::RenderPhase::Opaque, objectIndex);

        if (opaqueOk && queue.HasPhase(RENDER3D::RenderPhase::DepthAware)) {
            DepthAwarePhaseScope depthAwareScope{};
            if (BeginDepthAwarePhase(depthAwareScope)) {
                RenderMeshPhase(queue, RENDER3D::RenderPhase::DepthAware, objectIndex);
                EndDepthAwarePhase(depthAwareScope);
            } else {
                g.drawItems.clear();
                return;
            }
        }

        g.drawItems.clear();
    }

    const MeshRendererDebugStats& GetDebugStats() {
        const MaterialFxProfileCacheStats fxCacheStats = MaterialFxProfile::GetCacheStats();
        g.debugStats.materialFxProfileCacheHitCount = fxCacheStats.hitCount;
        g.debugStats.materialFxProfileCacheMissCount = fxCacheStats.missCount;
        g.debugStats.materialFxProfileCacheFailCount = fxCacheStats.failCount;
        return g.debugStats;
    }

} // namespace HIKARI::MESHRENDERER
