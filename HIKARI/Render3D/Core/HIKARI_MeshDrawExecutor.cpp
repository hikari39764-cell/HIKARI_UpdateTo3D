#include "Render3D/Core/HIKARI_MeshDrawExecutor.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "Render3D/HIKARI_Mesh.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshMaterialResolver.h"
#include "Render3D/Core/HIKARI_MeshPrimitiveCache.h"
#include "Render3D/Core/HIKARI_MeshRendererPso.h"
#include "Render3D/Core/HIKARI_MeshRendererRootParams.h"
#include "Render3D/Core/HIKARI_MeshRendererUpload.h"
#include "Render3D/Core/HIKARI_MeshVariantResolver.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"
#include "Render3D/Core/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/Core/HIKARI_SurfaceIndirectDrawBuffer.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPacket.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawRoute.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::MESHRENDERER {

    namespace {
        // GPU Scene は SurfacePacket の通常経路として消費する。
        constexpr bool kEnableSurfaceGpuSceneConsumption = true;

        const MaterialAsset* GetPrimitiveMaterial(const ModelAsset& asset, uint32_t materialIndex) {
            if (materialIndex >= asset.materials.size()) {
                return nullptr;
            }
            return &asset.materials[materialIndex];
        }

        void CopyObjectCB(const MeshDrawContext& ctx, const ObjectCB& obj, size_t objectIndex) {
            constexpr UINT kObjectStride = AlignConstantBufferSize(sizeof(ObjectCB));
            uint8_t* dst = reinterpret_cast<uint8_t*>(ctx.objectMapped) + static_cast<size_t>(kObjectStride) * objectIndex;
            std::memcpy(dst, &obj, sizeof(ObjectCB));
            if (ctx.services.stats != nullptr) {
                ++ctx.services.stats->legacyObjectCbWriteCount;
            }
        }

        ObjectGpuData BuildObjectGpuData(const ObjectCB& obj, uint32_t materialDataIndex) {
            ObjectGpuData data{};
            data.world = obj.world;
            data.normalMatrix = obj.normalMatrix;
            data.baseColor = obj.baseColor;
            data.hasBaseColorTexture = obj.hasBaseColorTexture;
            data.fxFlags = obj.fxFlags;
            data.materialFlags = obj.materialFlags;
            data.alphaCutoff = obj.alphaCutoff;
            data.emissiveFactor = obj.emissiveFactor;
            data.hasNormalTexture = obj.hasNormalTexture;
            data.normalScale = obj.normalScale;
            data.normalPadding[0] = obj.normalPadding[0];
            data.normalPadding[1] = obj.normalPadding[1];
            data.receiveShadow = obj.receiveShadow;
            data.shadowObjectPadding[0] = obj.shadowObjectPadding[0];
            data.shadowObjectPadding[1] = obj.shadowObjectPadding[1];
            data.shadowObjectPadding[2] = obj.shadowObjectPadding[2];
            data.hasEmissiveTexture = obj.hasEmissiveTexture;
            data.emissivePadding[0] = obj.emissivePadding[0];
            data.emissivePadding[1] = obj.emissivePadding[1];
            data.emissivePadding[2] = obj.emissivePadding[2];
            data.metallicFactor = obj.metallicFactor;
            data.roughnessFactor = obj.roughnessFactor;
            data.hasMetallicRoughnessTexture = obj.hasMetallicRoughnessTexture;
            data.hasOcclusionTexture = obj.hasOcclusionTexture;
            data.occlusionStrength = obj.occlusionStrength;
            data.materialDataIndex = materialDataIndex;
            data.pbrPadding[0] = obj.pbrPadding[0];
            data.pbrPadding[1] = obj.pbrPadding[1];
            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                data.fxUser[i] = obj.fxUser[i];
            }
            return data;
        }

        void CopyObjectData(
            const MeshDrawContext& ctx,
            const ObjectCB& obj,
            size_t objectIndex,
            uint32_t materialDataIndex = kInvalidMaterialDataIndex) {
            if (ctx.objectDataMapped == nullptr || objectIndex >= kMaxObjectCount) {
                return;
            }
            ctx.objectDataMapped[objectIndex] = BuildObjectGpuData(obj, materialDataIndex);
            if (ctx.services.stats != nullptr) {
                ++ctx.services.stats->objectDataWriteCount;
            }
        }

        uint32_t ResolveTextureDescriptorIndex(int textureHandle) {
            const UINT descriptorIndex =
                RENDER3D::GetTextureResourceSrvDescriptorIndexFromBackendHandle(textureHandle);
            return descriptorIndex == UINT32_MAX
                ? kInvalidTextureDescriptorIndex
                : static_cast<uint32_t>(descriptorIndex);
        }

        MaterialTextureDescriptorIndices ResolveMaterialTextureDescriptorIndices(
            const MaterialTextureHandles& textures) {

            MaterialTextureDescriptorIndices indices{};
            indices.baseColor = ResolveTextureDescriptorIndex(textures.baseColor);
            indices.normal = ResolveTextureDescriptorIndex(textures.normal);
            indices.emissive = ResolveTextureDescriptorIndex(textures.emissive);
            indices.metallicRoughness = ResolveTextureDescriptorIndex(textures.metallicRoughness);
            indices.occlusion = ResolveTextureDescriptorIndex(textures.occlusion);
            return indices;
        }

        MaterialGpuData BuildMaterialGpuData(
            const ObjectCB& obj,
            const MaterialTextureHandles& textures) {

            MaterialGpuData data{};
            const MaterialTextureDescriptorIndices textureIndices =
                ResolveMaterialTextureDescriptorIndices(textures);
            data.baseColor = obj.baseColor;
            data.emissiveFactor = obj.emissiveFactor;
            data.pbrParams = {
                obj.metallicFactor,
                obj.roughnessFactor,
                obj.occlusionStrength,
                obj.alphaCutoff
            };
            data.materialFlags = obj.materialFlags;
            data.hasBaseColorTexture = obj.hasBaseColorTexture;
            data.hasNormalTexture = obj.hasNormalTexture;
            data.hasEmissiveTexture = obj.hasEmissiveTexture;
            data.hasMetallicRoughnessTexture = obj.hasMetallicRoughnessTexture;
            data.hasOcclusionTexture = obj.hasOcclusionTexture;
            data.normalScale = obj.normalScale;
            data.baseColorTextureHandle = textures.baseColor;
            data.normalTextureHandle = textures.normal;
            data.emissiveTextureHandle = textures.emissive;
            data.metallicRoughnessTextureHandle = textures.metallicRoughness;
            data.occlusionTextureHandle = textures.occlusion;
            data.baseColorTextureDescriptorIndex = textureIndices.baseColor;
            data.normalTextureDescriptorIndex = textureIndices.normal;
            data.emissiveTextureDescriptorIndex = textureIndices.emissive;
            data.metallicRoughnessTextureDescriptorIndex = textureIndices.metallicRoughness;
            data.occlusionTextureDescriptorIndex = textureIndices.occlusion;
            return data;
        }

        void RegisterMaterialTextureDescriptors(
            MaterialDataFrameTable& table,
            const MaterialGpuData& data) {

            const uint32_t descriptorIndices[] = {
                data.baseColorTextureDescriptorIndex,
                data.normalTextureDescriptorIndex,
                data.emissiveTextureDescriptorIndex,
                data.metallicRoughnessTextureDescriptorIndex,
                data.occlusionTextureDescriptorIndex,
            };

            for (uint32_t descriptorIndex : descriptorIndices) {
                if (descriptorIndex != kInvalidTextureDescriptorIndex) {
                    table.textureDescriptorIndices.insert(descriptorIndex);
                }
            }
        }

        void RecordMaterialTexturePoolStats(
            const MeshDrawContext& ctx,
            const MaterialGpuData& data) {

            MeshRendererDebugStats* stats = ctx.services.stats;
            if (stats == nullptr) {
                return;
            }

            const uint32_t descriptorIndices[] = {
                data.baseColorTextureDescriptorIndex,
                data.normalTextureDescriptorIndex,
                data.emissiveTextureDescriptorIndex,
                data.metallicRoughnessTextureDescriptorIndex,
                data.occlusionTextureDescriptorIndex,
            };

            for (uint32_t descriptorIndex : descriptorIndices) {
                ++stats->materialTexturePoolSlotCount;
                if (descriptorIndex == kInvalidTextureDescriptorIndex) {
                    ++stats->materialTexturePoolInvalidSlotCount;
                } else {
                    ++stats->materialTexturePoolResolvedSlotCount;
                }
            }
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

        uint64_t BuildMaterialDataKey(
            uint64_t stableMaterialKey,
            const MaterialGpuData& data) {

            uint64_t seed = HashAppend(1469598103934665603ull, stableMaterialKey);
            return HashBytes(seed, &data, sizeof(data));
        }

        uint32_t UploadMaterialData(
            const MeshDrawContext& ctx,
            uint64_t key,
            const MaterialGpuData& data) {

            if (ctx.materialDataTable == nullptr || ctx.materialDataMapped == nullptr) {
                return kInvalidMaterialDataIndex;
            }

            MaterialDataFrameTable& table = *ctx.materialDataTable;
            const auto found = table.indexByKey.find(key);
            if (found != table.indexByKey.end()) {
                if (ctx.services.stats != nullptr) {
                    ++ctx.services.stats->materialDataCacheHitCount;
                    ctx.services.stats->materialDataCachedCount = table.count;
                    ctx.services.stats->materialTexturePoolUniqueDescriptorCount =
                        table.textureDescriptorIndices.size();
                }
                return found->second;
            }

            if (table.count >= kMaxMaterialDataCount) {
                if (ctx.services.stats != nullptr) {
                    ++ctx.services.stats->materialDataOverflowCount;
                    ctx.services.stats->materialDataCachedCount = table.count;
                }
                return kInvalidMaterialDataIndex;
            }

            const uint32_t index = table.count++;
            ctx.materialDataMapped[index] = data;
            table.indexByKey.emplace(key, index);
            RegisterMaterialTextureDescriptors(table, data);
            if (ctx.services.stats != nullptr) {
                ++ctx.services.stats->materialDataCacheMissCount;
                ++ctx.services.stats->materialDataWriteCount;
                ctx.services.stats->materialDataCachedCount = table.count;
                ctx.services.stats->materialTexturePoolUniqueDescriptorCount =
                    table.textureDescriptorIndices.size();
            }
            return index;
        }

        D3D12_GPU_VIRTUAL_ADDRESS ObjectAddress(const MeshDrawContext& ctx, size_t objectIndex) {
            constexpr UINT kObjectStride = AlignConstantBufferSize(sizeof(ObjectCB));
            return ctx.objectCB->GetGPUVirtualAddress() + static_cast<UINT64>(kObjectStride) * objectIndex;
        }

        D3D12_GPU_VIRTUAL_ADDRESS JointPaletteAddress(const MeshDrawContext& ctx, size_t objectIndex) {
            constexpr UINT kJointPaletteStride = AlignConstantBufferSize(sizeof(JointPaletteCB));
            return ctx.jointPaletteCB->GetGPUVirtualAddress() + static_cast<UINT64>(kJointPaletteStride) * objectIndex;
        }

        void BindPerDrawCommon(
            const MeshDrawContext& ctx,
            ID3D12RootSignature* rootSig,
            D3D12_GPU_VIRTUAL_ADDRESS objectAddress,
            uint32_t objectDataIndex,
            uint32_t materialDataIndex,
            bool bindLegacyObjectCB) {
            BindFrameCommonResources(
                ctx.binding,
                rootSig,
                ctx.cameraAddress,
                ctx.lightAddress,
                ctx.shadowAddress,
                ctx.skyEnvironmentAddress);
            BindObjectDataBuffer(ctx.binding, ctx.objectDataSrv);
            BindObjectDataIndex(ctx.binding, objectDataIndex);
            BindMaterialDataBuffer(ctx.binding, ctx.materialDataSrv);
            BindMaterialDataIndex(
                ctx.binding,
                materialDataIndex == kInvalidMaterialDataIndex ? 0u : materialDataIndex);
            BindSurfaceGpuSceneBuffer(ctx.binding, ctx.surfaceGpuSceneSrv);
            BindSurfaceGpuSceneControl(ctx.binding, 0u, false);
            if (bindLegacyObjectCB) {
                BindObjectConstantBuffer(ctx.binding, objectAddress);
            }
        }

        bool VariantCanUseObjectDataOnly(
            MeshDrawPassKind passKind,
            const VFX::VariantKey& variant) {
            if (passKind == MeshDrawPassKind::GeometryBuffer) {
                return RENDER3D::RUNTIME::IsSurfaceObjectDataVertexShader(variant.vertexShaderId);
            }
            return
                RENDER3D::RUNTIME::IsSurfaceObjectDataVertexShader(variant.vertexShaderId) &&
                RENDER3D::RUNTIME::IsSurfaceObjectDataPixelShader(variant.shaderId, variant.pixelShaderId);
        }

        bool VariantCanUseSurfaceGpuScene(
            MeshDrawPassKind passKind,
            const VFX::VariantKey& variant) {
            if (!RENDER3D::RUNTIME::IsSurfaceObjectDataVertexShader(variant.vertexShaderId)) {
                return false;
            }
            if (passKind == MeshDrawPassKind::GeometryBuffer) {
                return true;
            }

            const std::string& pixelId = !variant.pixelShaderId.empty()
                ? variant.pixelShaderId
                : variant.shaderId;
            return
                pixelId.empty() ||
                pixelId == "PBR" ||
                pixelId == "StaticLit" ||
                pixelId == "StaticFx" ||
                pixelId == "MaterialFx" ||
                pixelId == "Render3D_StaticPS" ||
                pixelId == "Render3D_StaticFxPS" ||
                pixelId == "Render3D_FxWaterPS";
        }

        bool PatchSurfaceGpuSceneMaterialData(
            const MeshDrawContext& ctx,
            size_t gpuSceneInstanceIndex,
            uint32_t materialDataIndex) {

            const uint32_t resolvedMaterialDataIndex =
                materialDataIndex == kInvalidMaterialDataIndex ? 0u : materialDataIndex;
            const bool patched =
                ctx.surfaceGpuSceneFrameBuffer != nullptr &&
                ctx.surfaceGpuSceneFrameBuffer->PatchMaterialDataIndex(
                    gpuSceneInstanceIndex,
                    resolvedMaterialDataIndex);
            if (ctx.services.stats != nullptr) {
                if (patched) {
                    ++ctx.services.stats->surfaceGpuSceneMaterialPatchCount;
                } else {
                    ++ctx.services.stats->surfaceGpuSceneMaterialPatchFailCount;
                }
            }
            return patched;
        }

        bool HasPreparedSurfaceGpuSceneMaterial(
            const MeshDrawContext& ctx,
            size_t gpuSceneInstanceIndex) {

            return
                ctx.surfaceGpuSceneFrameBuffer != nullptr &&
                ctx.surfaceGpuSceneFrameBuffer->HasMaterialDataIndex(gpuSceneInstanceIndex);
        }

        void DrawPrimitiveWithDebugMode(
            const MeshDrawContext& ctx,
            const Mesh& mesh,
            const VFX::VariantKey& variant,
            bool drawingSkinned,
            MeshRenderDebugMode mode) {
            if (ctx.cmd == nullptr ||
                ctx.services.pipelines == nullptr ||
                ctx.services.device == nullptr ||
                ctx.services.stats == nullptr) {
                return;
            }

            if (ctx.passKind == MeshDrawPassKind::GeometryBuffer) {
                ID3D12PipelineState* pso = GetGeometryPso(*ctx.services.pipelines, drawingSkinned);
                if (pso == nullptr) {
                    return;
                }
                BindPipelineState(ctx.binding, pso);
                ctx.cmd->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
                return;
            }

            const bool drawSolid = mode != MeshRenderDebugMode::WireOnly;
            const bool drawWire = mode == MeshRenderDebugMode::WireOnly ||
                mode == MeshRenderDebugMode::WireOverlay;

            auto drawPrimitive = [&](bool wireframe) {
                ID3D12PipelineState* pso = GetOrCreateVariantPso(
                    *ctx.services.pipelines,
                    ctx.services.device,
                    *ctx.services.stats,
                    variant,
                    drawingSkinned,
                    wireframe);
                if (pso == nullptr) {
                    return;
                }
                BindPipelineState(ctx.binding, pso);
                ctx.cmd->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
                if (drawingSkinned) {
                    ++ctx.services.stats->skinnedGpuDrawCount;
                }
                if (wireframe) {
                    ++ctx.services.stats->wireGpuDrawCount;
                }
            };

            if (drawSolid) {
                drawPrimitive(false);
            }
            if (drawWire) {
                drawPrimitive(true);
            }
        }

        MaterialTextureHandles ResolveRuntimeMaterialTextureHandles(
            const Material* material,
            const MeshBindingContext& binding,
            const MeshMaterialFillContext& fill) {

            MaterialTextureHandles textureHandles{};
            textureHandles.baseColor = binding.fallbackTextureHandle;
            textureHandles.normal = binding.fallbackNormalTextureHandle;
            textureHandles.emissive = fill.fallbackBlackTextureHandle;
            textureHandles.metallicRoughness = binding.fallbackTextureHandle;
            textureHandles.occlusion = binding.fallbackTextureHandle;

            if (material == nullptr) {
                return textureHandles;
            }
            if (material->HasBaseColorTexture()) {
                textureHandles.baseColor = material->GetBaseColorTextureHandle();
            }
            if (material->HasTextureSlot(ModelTextureUsage::Normal)) {
                textureHandles.normal = material->GetTextureSlot(ModelTextureUsage::Normal).handle;
            }
            if (material->HasTextureSlot(ModelTextureUsage::Emissive)) {
                textureHandles.emissive = material->GetTextureSlot(ModelTextureUsage::Emissive).handle;
            }
            if (material->HasTextureSlot(ModelTextureUsage::MetallicRoughness)) {
                textureHandles.metallicRoughness = material->GetTextureSlot(ModelTextureUsage::MetallicRoughness).handle;
            }
            if (material->HasTextureSlot(ModelTextureUsage::Occlusion)) {
                textureHandles.occlusion = material->GetTextureSlot(ModelTextureUsage::Occlusion).handle;
            }
            return textureHandles;
        }

        void FillRuntimeMaterialValues(ObjectCB& obj, const Material& material) {
            obj.baseColor = material.GetBaseColor();
            obj.hasBaseColorTexture = material.HasBaseColorTexture() ? 1u : 0u;
            obj.hasNormalTexture = material.HasTextureSlot(ModelTextureUsage::Normal) ? 1u : 0u;
            obj.hasMetallicRoughnessTexture = material.HasTextureSlot(ModelTextureUsage::MetallicRoughness) ? 1u : 0u;
            obj.hasOcclusionTexture = material.HasTextureSlot(ModelTextureUsage::Occlusion) ? 1u : 0u;
            obj.hasEmissiveTexture = material.HasTextureSlot(ModelTextureUsage::Emissive) ? 1u : 0u;
            obj.normalScale = material.GetNormalScale();
            obj.metallicFactor = material.GetMetallicFactor();
            obj.roughnessFactor = material.GetRoughnessFactor();
            obj.occlusionStrength = material.GetOcclusionStrength();
            const MATH::Vec3& emissive = material.GetEmissiveFactor();
            obj.emissiveFactor = {
                emissive.x,
                emissive.y,
                emissive.z,
                material.GetEmissiveStrength()
            };
            obj.materialFlags = material.GetFeatureBits();
        }

        struct SurfacePacketBatchState {
            const ModelAsset* model = nullptr;
            const MaterialAsset* materialAsset = nullptr;
            const Material* runtimeMaterial = nullptr;
            ResolvedMaterialTextures textures{};
            VFX::VariantKey variant{};
            std::array<MATH::Vec4, VFX::kMaterialFxUserCount> defaultFxValues{};
            RENDER3D::RUNTIME::SurfaceDrawBatchKey batchKey{};
            uint32_t fxFlags = 0;
            bool objectDataCompatible = false;
        };

        struct SurfacePacketPreparedObject {
            ObjectCB object{};
            uint32_t materialDataIndex = kInvalidMaterialDataIndex;
        };

        bool CanUseSurfaceGpuSceneCommand(
            const MeshDrawContext& ctx,
            const SurfacePacketBatchState& state,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command) {
            if (!kEnableSurfaceGpuSceneConsumption) {
                return false;
            }
            if (!state.objectDataCompatible) {
                return false;
            }
            if (ctx.surfaceGpuSceneFrameBuffer == nullptr ||
                ctx.surfaceGpuSceneSrv.ptr == 0 ||
                command.firstGpuSceneInstanceIndex == RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex ||
                command.gpuSceneInstanceCount < command.packetCount ||
                !VariantCanUseSurfaceGpuScene(ctx.passKind, state.variant)) {
                return false;
            }

            const RENDER3D::CORE::SurfaceGpuSceneFrameBufferStats& gpuSceneStats =
                ctx.surfaceGpuSceneFrameBuffer->GetStats();
            if (!gpuSceneStats.initialized) {
                return false;
            }
            const size_t commandFirst =
                ctx.surfaceGpuSceneBaseOffset +
                static_cast<size_t>(command.firstGpuSceneInstanceIndex);
            const size_t commandEnd = commandFirst + static_cast<size_t>(command.gpuSceneInstanceCount);
            return
                commandEnd >= commandFirst &&
                commandEnd <= gpuSceneStats.uploadedInstanceCount;
        }

        Transform3D BuildPacketDrawTransform(const RENDER3D::RUNTIME::SurfaceDrawPacket& packet) {
            Transform3D transform = packet.objectWorldTransform;
            transform.useExplicitMatrix = true;
            transform.explicitMatrix = packet.drawWorldMatrix;
            return transform;
        }

        DrawItem BuildBatchVariantAdapter(const RENDER3D::RUNTIME::SurfaceDrawPacket& packet) {
            DrawItem item{};
            item.asset = packet.model;
            item.materialOverride = packet.materialOverride;
            item.materialFxProfileId = packet.materialFxProfileId;
            item.postGroupMask = packet.postGroupMask;
            // Batch の既定値だけを解決し、個別 override は packet 側で反映する。
            item.materialFxValuesInitialized = false;
            ResolveDrawVariant(item);
            return item;
        }


        ResolvedMaterialTextures ResolvePacketTextures(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& packet,
            const MaterialAsset* materialAsset) {

            ResolvedMaterialTextures textures{};
            if (packet.materialOverride != nullptr) {
                const MaterialTextureHandles handles =
                    ResolveRuntimeMaterialTextureHandles(packet.materialOverride, ctx.binding, ctx.materialFill);
                textures.baseColor = handles.baseColor;
                textures.normal = handles.normal;
                textures.emissive = handles.emissive;
                textures.metallicRoughness = handles.metallicRoughness;
                textures.occlusion = handles.occlusion;
                return textures;
            }

            if (ctx.services.materialResolver != nullptr && packet.model != nullptr) {
                return ctx.services.materialResolver->Resolve(*packet.model, materialAsset, ctx.services.stats);
            }

            textures.baseColor = ctx.binding.fallbackTextureHandle;
            textures.normal = ctx.binding.fallbackNormalTextureHandle;
            textures.emissive = ctx.materialFill.fallbackBlackTextureHandle;
            textures.metallicRoughness = ctx.binding.fallbackTextureHandle;
            textures.occlusion = ctx.binding.fallbackTextureHandle;
            return textures;
        }

        MaterialTextureHandles ToMaterialTextureHandles(const ResolvedMaterialTextures& textures) {
            return {
                textures.baseColor,
                textures.normal,
                textures.emissive,
                textures.metallicRoughness,
                textures.occlusion
            };
        }

        void BindSurfacePacketFrameResourcesInternal(const MeshDrawContext& ctx) {

            BindFrameCommonResources(
                ctx.binding,
                ctx.staticRootSig,
                ctx.cameraAddress,
                ctx.lightAddress,
                ctx.shadowAddress,
                ctx.skyEnvironmentAddress);
            BindObjectDataBuffer(ctx.binding, ctx.objectDataSrv);
            BindMaterialDataBuffer(ctx.binding, ctx.materialDataSrv);
            BindSurfaceGpuSceneBuffer(ctx.binding, ctx.surfaceGpuSceneSrv);
            BindSurfaceGpuSceneControl(ctx.binding, 0u, false);
            BindShadowMap(ctx.binding);
            if (ctx.passKind == MeshDrawPassKind::Forward) {
                BindSkyCube(ctx.binding);
                BindSceneDepth(ctx.binding);
                BindSceneColor(ctx.binding);
                BindIblResources(ctx.binding);
                BindReflectionProbeResources(ctx.binding);
                BindSsao(ctx.binding);
                BindLightProbeResources(ctx.binding);
            }
        }

        bool PrepareSurfacePacketBatch(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& firstPacket,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            SurfacePacketBatchState& outState) {

            if (ctx.cmd == nullptr ||
                ctx.services.device == nullptr ||
                ctx.services.pipelines == nullptr ||
                ctx.services.stats == nullptr ||
                firstPacket.model == nullptr ||
                firstPacket.meshIndex >= firstPacket.model->meshes.size()) {
                return false;
            }
            if (!RENDER3D::RUNTIME::IsValidSurfaceDrawBatchKey(command.batchKey) ||
                !RENDER3D::RUNTIME::IsSameSurfaceDrawBatchKey(
                    RENDER3D::RUNTIME::BuildSurfaceDrawBatchKey(command.pass, firstPacket.key),
                    command.batchKey)) {
                return false;
            }

            const MeshAsset& meshAsset = firstPacket.model->meshes[firstPacket.meshIndex];
            if (firstPacket.primitiveIndex >= meshAsset.primitives.size()) {
                return false;
            }

            const MeshPrimitive& primitive = meshAsset.primitives[firstPacket.primitiveIndex];
            outState = {};
            outState.model = firstPacket.model;
            outState.runtimeMaterial = firstPacket.materialOverride;
            outState.materialAsset = GetPrimitiveMaterial(*firstPacket.model, primitive.materialIndex);
            outState.batchKey = command.batchKey;

            DrawItem variantItem = BuildBatchVariantAdapter(firstPacket);
            outState.defaultFxValues = variantItem.fxValues;
            outState.fxFlags = variantItem.fxFlags;
            outState.variant = ResolvePrimitiveVariant(
                variantItem,
                outState.runtimeMaterial != nullptr ? nullptr : outState.materialAsset);
            outState.objectDataCompatible = VariantCanUseObjectDataOnly(ctx.passKind, outState.variant);
            if (ctx.passKind == MeshDrawPassKind::GeometryBuffer && !outState.objectDataCompatible) {
                return false;
            }

            BindMaterialDataIndex(ctx.binding, 0u);

            ID3D12PipelineState* pso = nullptr;
            if (ctx.passKind == MeshDrawPassKind::GeometryBuffer) {
                pso = GetGeometryPso(*ctx.services.pipelines, false);
            } else {
                pso = GetOrCreateVariantPso(
                    *ctx.services.pipelines,
                    ctx.services.device,
                    *ctx.services.stats,
                    outState.variant,
                    false,
                    false);
            }
            if (pso == nullptr) {
                return false;
            }
            BindPipelineState(ctx.binding, pso);
            return true;
        }

        bool IsBatchCompatiblePacket(
            const SurfacePacketBatchState& state,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& packet) {

            return RENDER3D::RUNTIME::IsSameSurfaceDrawBatchKey(
                RENDER3D::RUNTIME::BuildSurfaceDrawBatchKey(state.batchKey.pass, packet.key),
                state.batchKey);
        }

        Mesh* ResolveSurfacePacketStaticMesh(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& packet) {

            if (packet.model == nullptr ||
                packet.meshIndex >= packet.model->meshes.size()) {
                return nullptr;
            }

            const MeshAsset& meshAsset = packet.model->meshes[packet.meshIndex];
            if (packet.primitiveIndex >= meshAsset.primitives.size()) {
                return nullptr;
            }

            MeshPrimitiveCache* primitiveCache = ctx.services.primitiveCache;
            if (primitiveCache == nullptr) {
                return nullptr;
            }

            const MeshPrimitive& primitive = meshAsset.primitives[packet.primitiveIndex];
            return primitiveCache->GetOrCreateStatic(ctx.services.device, primitive, ctx.services.stats);
        }

        void FillPacketFxValues(
            ObjectCB& obj,
            const SurfacePacketBatchState& state,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& packet) {

            obj.fxFlags = state.fxFlags;
            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                obj.fxUser[i] = state.defaultFxValues[i];
            }

            if (!packet.materialFxValuesInitialized) {
                return;
            }

            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                const DirectX::XMFLOAT4& value = packet.materialFxParamValues[i];
                obj.fxUser[i] = { value.x, value.y, value.z, value.w };
            }
        }

        void FillPacketFxValues(
            ObjectCB& obj,
            const DrawItem& variantItem,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& packet) {

            obj.fxFlags = variantItem.fxFlags;
            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                obj.fxUser[i] = variantItem.fxValues[i];
            }

            if (!packet.materialFxValuesInitialized) {
                return;
            }

            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                const DirectX::XMFLOAT4& value = packet.materialFxParamValues[i];
                obj.fxUser[i] = { value.x, value.y, value.z, value.w };
            }
        }

        void FillSurfacePacketObject(
            const MeshDrawContext& ctx,
            const SurfacePacketBatchState& state,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& packet,
            ObjectCB& obj) {

            const Transform3D drawTransform = BuildPacketDrawTransform(packet);
            obj = {};
            obj.world = packet.drawWorldMatrix;
            obj.normalMatrix = BuildNormalMatrix(drawTransform);
            FillMaterialValues(
                obj,
                state.materialAsset,
                state.textures.normal,
                state.textures.emissive,
                state.textures.metallicRoughness,
                state.textures.occlusion,
                ctx.materialFill);
            if (state.runtimeMaterial != nullptr) {
                FillRuntimeMaterialValues(obj, *state.runtimeMaterial);
            }
            obj.hasBaseColorTexture =
                (state.textures.baseColor >= 0 && state.textures.baseColor != ctx.binding.fallbackTextureHandle) ? 1u : 0u;
            obj.receiveShadow = packet.receiveShadow ? 1u : 0u;
            FillPacketFxValues(obj, state, packet);
        }

        bool PrepareSurfacePacketObjectData(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& packet,
            SurfacePacketPreparedObject& outPrepared) {

            if (packet.model == nullptr ||
                packet.meshIndex >= packet.model->meshes.size()) {
                return false;
            }

            const MeshAsset& meshAsset = packet.model->meshes[packet.meshIndex];
            if (packet.primitiveIndex >= meshAsset.primitives.size()) {
                return false;
            }

            const MeshPrimitive& primitive = meshAsset.primitives[packet.primitiveIndex];
            const MaterialAsset* materialAsset = GetPrimitiveMaterial(*packet.model, primitive.materialIndex);
            const ResolvedMaterialTextures textures = ResolvePacketTextures(ctx, packet, materialAsset);
            const DrawItem variantItem = BuildBatchVariantAdapter(packet);

            ObjectCB obj{};
            const Transform3D drawTransform = BuildPacketDrawTransform(packet);
            obj.world = packet.drawWorldMatrix;
            obj.normalMatrix = BuildNormalMatrix(drawTransform);
            FillMaterialValues(
                obj,
                materialAsset,
                textures.normal,
                textures.emissive,
                textures.metallicRoughness,
                textures.occlusion,
                ctx.materialFill);
            if (packet.materialOverride != nullptr) {
                FillRuntimeMaterialValues(obj, *packet.materialOverride);
            }
            obj.hasBaseColorTexture =
                (textures.baseColor >= 0 && textures.baseColor != ctx.binding.fallbackTextureHandle) ? 1u : 0u;
            obj.receiveShadow = packet.receiveShadow ? 1u : 0u;
            FillPacketFxValues(obj, variantItem, packet);

            const MaterialTextureHandles textureHandles = ToMaterialTextureHandles(textures);
            const MaterialGpuData materialData = BuildMaterialGpuData(obj, textureHandles);
            RecordMaterialTexturePoolStats(ctx, materialData);
            const uint32_t materialDataIndex = UploadMaterialData(
                ctx,
                BuildMaterialDataKey(packet.key.materialKey, materialData),
                materialData);

            outPrepared.object = obj;
            outPrepared.materialDataIndex =
                materialDataIndex == kInvalidMaterialDataIndex ? 0u : materialDataIndex;
            return true;
        }

        bool IsInstanceBatchCompatiblePacket(
            const MeshDrawContext& ctx,
            const SurfacePacketBatchState& state,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& firstPacket,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& packet) {

            (void)ctx;
            if (!IsBatchCompatiblePacket(state, packet) ||
                packet.meshIndex != firstPacket.meshIndex ||
                packet.primitiveIndex != firstPacket.primitiveIndex) {
                return false;
            }

            // StaticVS / WaterVS + ObjectData で安全に扱える範囲だけを instance 化する。
            return state.objectDataCompatible;
        }


        bool BindSurfacePacketMesh(
            const MeshDrawContext& ctx,
            const Mesh* mesh,
            const Mesh*& activeMesh) {

            if (mesh == nullptr || !mesh->IsValid()) {
                return false;
            }
            if (activeMesh != mesh) {
                D3D12_VERTEX_BUFFER_VIEW vb = mesh->GetVBView();
                D3D12_INDEX_BUFFER_VIEW ib = mesh->GetIBView();
                ctx.cmd->IASetVertexBuffers(0, 1, &vb);
                ctx.cmd->IASetIndexBuffer(&ib);
                activeMesh = mesh;
            }
            return true;
        }

        bool DrawPreparedSurfacePacket(
            const MeshDrawContext& ctx,
            const SurfacePacketBatchState& state,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& packet,
            const Mesh*& activeMesh,
            size_t& objectIndex) {

            if (objectIndex >= kMaxObjectCount ||
                !packet.hasDrawWorldMatrix ||
                packet.model == nullptr ||
                packet.meshIndex >= packet.model->meshes.size() ||
                !IsBatchCompatiblePacket(state, packet)) {
                return false;
            }

            Mesh* mesh = ResolveSurfacePacketStaticMesh(ctx, packet);
            if (mesh == nullptr || !mesh->IsValid()) {
                return false;
            }

            SurfacePacketPreparedObject prepared{};
            if (!PrepareSurfacePacketObjectData(ctx, packet, prepared)) {
                return false;
            }
            const bool bindLegacyObjectCB = !state.objectDataCompatible;
            if (bindLegacyObjectCB) {
                CopyObjectCB(ctx, prepared.object, objectIndex);
            }
            CopyObjectData(ctx, prepared.object, objectIndex, prepared.materialDataIndex);

            BindPerDrawCommon(
                ctx,
                ctx.staticRootSig,
                ObjectAddress(ctx, objectIndex),
                static_cast<uint32_t>(objectIndex),
                prepared.materialDataIndex,
                bindLegacyObjectCB);

            if (!BindSurfacePacketMesh(ctx, mesh, activeMesh)) {
                return false;
            }

            ctx.cmd->DrawIndexedInstanced(mesh->GetIndexCount(), 1, 0, 0, 0);
            ++objectIndex;
            return true;
        }

        bool DrawSurfacePacketInstanceBatch(
            const MeshDrawContext& ctx,
            const SurfacePacketBatchState& state,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
            size_t packetCount,
            const uint32_t* executablePacketIndices,
            size_t executablePacketIndexCount,
            size_t batchEnd,
            size_t& executableIndex,
            const Mesh*& activeMesh,
            size_t& objectIndex,
            SurfacePacketCommandDrawResult& result) {

            const uint32_t firstPacketIndex = executablePacketIndices[executableIndex];
            if (firstPacketIndex >= packetCount) {
                ++result.skippedPacketCount;
                return false;
            }

            const RENDER3D::RUNTIME::SurfaceDrawPacket& firstPacket = packets[firstPacketIndex];
            if (!IsInstanceBatchCompatiblePacket(ctx, state, firstPacket, firstPacket)) {
                return false;
            }

            Mesh* mesh = ResolveSurfacePacketStaticMesh(ctx, firstPacket);
            if (mesh == nullptr || !mesh->IsValid()) {
                return false;
            }

            const bool useSurfaceGpuScene = CanUseSurfaceGpuSceneCommand(ctx, state, command);
            if (useSurfaceGpuScene) {
                BindSurfaceGpuSceneBuffer(ctx.binding, ctx.surfaceGpuSceneSrv);
            }

            const size_t batchObjectStart = objectIndex;
            const size_t batchGpuSceneStart =
                ctx.surfaceGpuSceneBaseOffset +
                static_cast<size_t>(command.firstGpuSceneInstanceIndex) +
                (executableIndex - static_cast<size_t>(command.firstExecutableIndex));
            uint32_t batchMaterialDataIndex = 0u;
            size_t batchCount = 0;
            size_t cursor = executableIndex;
            for (; cursor < batchEnd && cursor < executablePacketIndexCount; ++cursor) {
                const uint32_t packetIndex = executablePacketIndices[cursor];
                if (packetIndex >= packetCount) {
                    break;
                }

                const RENDER3D::RUNTIME::SurfaceDrawPacket& packet = packets[packetIndex];
                if (!IsInstanceBatchCompatiblePacket(ctx, state, firstPacket, packet) ||
                    (!useSurfaceGpuScene && objectIndex + batchCount >= kMaxObjectCount)) {
                    break;
                }

                SurfacePacketPreparedObject prepared{};
                if (!PrepareSurfacePacketObjectData(ctx, packet, prepared)) {
                    break;
                }
                if (batchCount == 0) {
                    batchMaterialDataIndex = prepared.materialDataIndex;
                }
                if (useSurfaceGpuScene) {
                    if (!PatchSurfaceGpuSceneMaterialData(
                        ctx,
                        batchGpuSceneStart + batchCount,
                        prepared.materialDataIndex)) {
                        break;
                    }
                } else {
                    CopyObjectData(ctx, prepared.object, objectIndex + batchCount, prepared.materialDataIndex);
                }
                ++batchCount;
            }

            if (batchCount == 0) {
                return false;
            }

            if (useSurfaceGpuScene) {
                BindSurfaceGpuSceneControl(ctx.binding, static_cast<uint32_t>(batchGpuSceneStart), true);
                BindObjectDataIndex(ctx.binding, 0u);
                if (ctx.services.stats != nullptr) {
                    ++ctx.services.stats->surfacePacketExecutorGpuSceneDrawCount;
                    ctx.services.stats->surfacePacketExecutorGpuScenePacketCount += batchCount;
                }
            } else {
                BindSurfaceGpuSceneControl(ctx.binding, 0u, false);
                BindObjectDataIndex(ctx.binding, static_cast<uint32_t>(batchObjectStart));
                if (ctx.services.stats != nullptr) {
                    ++ctx.services.stats->surfacePacketExecutorGpuSceneFallbackCount;
                }
            }
            BindMaterialDataIndex(ctx.binding, batchMaterialDataIndex);
            if (!BindSurfacePacketMesh(ctx, mesh, activeMesh)) {
                return false;
            }

            // command 側の args を優先し、未整備のケースだけ mesh 実体から補う。
            const uint32_t indexCount = command.drawArgsValid
                ? command.drawArgs.indexCountPerInstance
                : mesh->GetIndexCount();
            ctx.cmd->DrawIndexedInstanced(
                indexCount,
                static_cast<UINT>(batchCount),
                command.drawArgs.startIndexLocation,
                command.drawArgs.baseVertexLocation,
                command.drawArgs.startInstanceLocation);
            if (!useSurfaceGpuScene) {
                objectIndex += batchCount;
            }
            result.submittedPacketCount += batchCount;
            ++result.drawCallCount;
            result.maxInstanceCount = (std::max)(result.maxInstanceCount, batchCount);
            if (batchCount > 1) {
                ++result.instancedDrawCount;
                result.instancedPacketCount += batchCount;
            }
            executableIndex = cursor - 1;
            return true;
        }

        void RecordSurfaceIndirectFallback(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command) {

            if (command.backend == RENDER3D::RUNTIME::SurfaceDrawCommandBackend::GpuDriven &&
                ctx.services.stats != nullptr) {
                ++ctx.services.stats->surfaceIndirectFallbackCommandCount;
            }
        }

        void RecordSurfaceIndirectSubmit(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawBatchKey& batchKey,
            size_t commandCount,
            size_t packetCount) {

            if (ctx.services.stats == nullptr || commandCount == 0) {
                return;
            }

            MeshRendererDebugStats& stats = *ctx.services.stats;
            stats.surfacePacketExecutorGpuSceneDrawCount += commandCount;
            stats.surfacePacketExecutorGpuScenePacketCount += packetCount;
            stats.surfaceIndirectExecutedDrawCount += commandCount;
            stats.surfaceIndirectExecutedPacketCount += packetCount;
            if (batchKey.pass == RENDER3D::RUNTIME::SurfaceDrawCommandPass::DepthAware) {
                stats.surfaceIndirectDepthAwareCommandCount += commandCount;
                stats.surfaceIndirectDepthAwarePacketCount += packetCount;
            } else if (batchKey.transparent) {
                stats.surfaceIndirectTransparentCommandCount += commandCount;
                stats.surfaceIndirectTransparentPacketCount += packetCount;
            } else {
                stats.surfaceIndirectOpaqueCommandCount += commandCount;
                stats.surfaceIndirectOpaquePacketCount += packetCount;
            }
            ++stats.surfaceIndirectBatchSubmitCount;
            stats.surfaceIndirectBatchedCommandCount += commandCount;
            stats.surfaceIndirectSavedSubmitCount += commandCount - 1;
            stats.surfaceIndirectMaxBatchCommandCount =
                (std::max)(stats.surfaceIndirectMaxBatchCommandCount, commandCount);
        }

        bool CanStartSurfaceIndirectCommandRange(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command) {

            const bool supportedPass =
                ctx.passKind == MeshDrawPassKind::Forward ||
                (ctx.passKind == MeshDrawPassKind::GeometryBuffer && !command.transparent);
            const bool supportedCommandPass =
                command.pass == RENDER3D::RUNTIME::SurfaceDrawCommandPass::Forward ||
                (ctx.passKind == MeshDrawPassKind::Forward &&
                    command.pass == RENDER3D::RUNTIME::SurfaceDrawCommandPass::DepthAware);
            return
                supportedPass &&
                supportedCommandPass &&
                command.backend == RENDER3D::RUNTIME::SurfaceDrawCommandBackend::GpuDriven &&
                command.packetCount > 0 &&
                RENDER3D::RUNTIME::IsValidSurfaceDrawBatchKey(command.batchKey);
        }

        bool IsSameSurfaceIndirectPipeline(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawBatchKey& lhs,
            const RENDER3D::RUNTIME::SurfaceDrawBatchKey& rhs) {

            if (lhs.pass != rhs.pass ||
                lhs.transparent != rhs.transparent) {
                return false;
            }
            if (ctx.passKind == MeshDrawPassKind::GeometryBuffer) {
                // GeometryBuffer は固定 PSO で描くため、Forward 用 PSO key の差で分割しない。
                return !lhs.transparent;
            }
            return
                lhs.psoKey != 0 &&
                lhs.psoKey == rhs.psoKey;
        }

        bool TryResolveSurfaceCommandPacketRange(
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            size_t executablePacketIndexCount,
            size_t& outBegin,
            size_t& outEnd) {

            const size_t begin = command.firstExecutableIndex;
            const size_t count = static_cast<size_t>(command.packetCount);
            if (count == 0 || begin >= executablePacketIndexCount) {
                return false;
            }

            const size_t end = begin + count;
            if (end < begin || end > executablePacketIndexCount) {
                return false;
            }

            outBegin = begin;
            outEnd = end;
            return true;
        }

        struct SurfaceIndirectPreparedCommand {
            SurfacePacketBatchState state{};
            const Mesh* mesh = nullptr;
            UINT64 argumentOffset = 0;
            size_t packetCount = 0;
        };

        bool TryPrepareSurfaceIndirectCommand(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawBatchKey& rangeKey,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
            size_t packetCount,
            const uint32_t* executablePacketIndices,
            size_t executablePacketIndexCount,
            SurfaceIndirectPreparedCommand& outPrepared) {

            outPrepared = {};
            if (!CanStartSurfaceIndirectCommandRange(ctx, command) ||
                !IsSameSurfaceIndirectPipeline(ctx, rangeKey, command.batchKey) ||
                ctx.surfaceIndirectDrawBuffer == nullptr) {
                return false;
            }

            size_t commandBegin = 0;
            size_t commandEnd = 0;
            if (!TryResolveSurfaceCommandPacketRange(
                command,
                executablePacketIndexCount,
                commandBegin,
                commandEnd)) {
                return false;
            }

            if (!ctx.surfaceIndirectDrawBuffer->TryGetArgumentBufferOffset(
                command,
                outPrepared.argumentOffset) ||
                !ctx.surfaceIndirectDrawBuffer->HasDrawBinding(command)) {
                return false;
            }

            const uint32_t firstPacketIndex = executablePacketIndices[commandBegin];
            if (firstPacketIndex >= packetCount) {
                return false;
            }

            const RENDER3D::RUNTIME::SurfaceDrawPacket& firstPacket = packets[firstPacketIndex];
            if (!PrepareSurfacePacketBatch(ctx, firstPacket, command, outPrepared.state) ||
                !CanUseSurfaceGpuSceneCommand(ctx, outPrepared.state, command) ||
                !IsInstanceBatchCompatiblePacket(ctx, outPrepared.state, firstPacket, firstPacket)) {
                return false;
            }

            Mesh* mesh = ResolveSurfacePacketStaticMesh(ctx, firstPacket);
            if (mesh == nullptr || !mesh->IsValid()) {
                return false;
            }

            outPrepared.mesh = mesh;
            size_t preparedPacketCount = 0;
            const size_t gpuSceneStart =
                ctx.surfaceGpuSceneBaseOffset +
                static_cast<size_t>(command.firstGpuSceneInstanceIndex);
            for (size_t executableIndex = commandBegin; executableIndex < commandEnd; ++executableIndex) {
                const uint32_t packetIndex = executablePacketIndices[executableIndex];
                if (packetIndex >= packetCount) {
                    return false;
                }

                const RENDER3D::RUNTIME::SurfaceDrawPacket& packet = packets[packetIndex];
                if (!IsInstanceBatchCompatiblePacket(ctx, outPrepared.state, firstPacket, packet)) {
                    return false;
                }
                if (!HasPreparedSurfaceGpuSceneMaterial(
                    ctx,
                    gpuSceneStart + preparedPacketCount)) {
                    return false;
                }
                ++preparedPacketCount;
            }

            outPrepared.packetCount = preparedPacketCount;
            return
                outPrepared.packetCount > 0 &&
                outPrepared.packetCount == static_cast<size_t>(command.drawArgs.instanceCount);
        }

        bool TryExecuteSurfacePacketIndirectCommandRange(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawCommand* commands,
            size_t commandCount,
            size_t commandIndex,
            const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
            size_t packetCount,
            const uint32_t* executablePacketIndices,
            size_t executablePacketIndexCount,
            const Mesh*& activeMesh,
            size_t& outNextCommandIndex,
            SurfacePacketCommandDrawResult& result) {

            if (ctx.cmd == nullptr ||
                ctx.surfaceIndirectDrawBuffer == nullptr ||
                commands == nullptr ||
                packets == nullptr ||
                executablePacketIndices == nullptr ||
                commandIndex >= commandCount ||
                !CanStartSurfaceIndirectCommandRange(ctx, commands[commandIndex])) {
                return false;
            }

            ID3D12Resource* argumentBuffer = ctx.surfaceIndirectDrawBuffer->GetArgumentBuffer();
            ID3D12CommandSignature* commandSignature =
                ctx.surfaceIndirectDrawBuffer->GetCommandSignature();
            if (argumentBuffer == nullptr || commandSignature == nullptr) {
                return false;
            }

            const RENDER3D::RUNTIME::SurfaceDrawBatchKey rangeKey =
                commands[commandIndex].batchKey;
            const UINT64 argumentStride =
                static_cast<UINT64>(sizeof(RENDER3D::CORE::SurfaceIndirectDrawArgument));
            UINT64 firstArgumentOffset = 0;
            size_t preparedCommandCount = 0;
            size_t preparedPacketCount = 0;
            size_t maxInstanceCount = 0;
            size_t instancedDrawCount = 0;
            size_t instancedPacketCount = 0;

            const size_t maxIndirectCommandCount =
                static_cast<size_t>((std::numeric_limits<UINT>::max)());
            for (size_t scanIndex = commandIndex;
                scanIndex < commandCount && preparedCommandCount < maxIndirectCommandCount;
                ++scanIndex) {

                const RENDER3D::RUNTIME::SurfaceDrawCommand& command = commands[scanIndex];
                if (!CanStartSurfaceIndirectCommandRange(ctx, command) ||
                    !IsSameSurfaceIndirectPipeline(ctx, rangeKey, command.batchKey)) {
                    break;
                }

                SurfaceIndirectPreparedCommand prepared{};
                if (!TryPrepareSurfaceIndirectCommand(
                    ctx,
                    rangeKey,
                    command,
                    packets,
                    packetCount,
                    executablePacketIndices,
                    executablePacketIndexCount,
                    prepared)) {
                    break;
                }

                if (preparedCommandCount == 0) {
                    firstArgumentOffset = prepared.argumentOffset;
                } else {
                    const UINT64 expectedOffset =
                        firstArgumentOffset + argumentStride * static_cast<UINT64>(preparedCommandCount);
                    if (prepared.argumentOffset != expectedOffset) {
                        break;
                    }
                }

                ++preparedCommandCount;
                preparedPacketCount += prepared.packetCount;
                maxInstanceCount = (std::max)(maxInstanceCount, prepared.packetCount);
                if (prepared.packetCount > 1) {
                    ++instancedDrawCount;
                    instancedPacketCount += prepared.packetCount;
                }
            }

            if (preparedCommandCount == 0) {
                return false;
            }

            BindSurfaceGpuSceneBuffer(ctx.binding, ctx.surfaceGpuSceneSrv);
            BindObjectDataIndex(ctx.binding, 0u);
            BindMaterialDataIndex(ctx.binding, 0u);
            activeMesh = nullptr;

            ctx.cmd->ExecuteIndirect(
                commandSignature,
                static_cast<UINT>(preparedCommandCount),
                argumentBuffer,
                firstArgumentOffset,
                nullptr,
                0);

            result.submittedPacketCount += preparedPacketCount;
            result.drawCallCount += preparedCommandCount;
            result.maxInstanceCount = (std::max)(result.maxInstanceCount, maxInstanceCount);
            result.instancedDrawCount += instancedDrawCount;
            result.instancedPacketCount += instancedPacketCount;
            RecordSurfaceIndirectSubmit(ctx, rangeKey, preparedCommandCount, preparedPacketCount);

            outNextCommandIndex = commandIndex + preparedCommandCount;
            return true;
        }

        bool TryExecuteSurfacePacketIndirectCommand(
            const MeshDrawContext& ctx,
            const SurfacePacketBatchState& state,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
            size_t packetCount,
            const uint32_t* executablePacketIndices,
            size_t executablePacketIndexCount,
            const Mesh*& activeMesh,
            SurfacePacketCommandDrawResult& result) {

            if (!CanStartSurfaceIndirectCommandRange(ctx, command)) {
                return false;
            }

            auto fail = [&]() {
                RecordSurfaceIndirectFallback(ctx, command);
                return false;
            };

            if (ctx.cmd == nullptr ||
                ctx.surfaceIndirectDrawBuffer == nullptr ||
                packets == nullptr ||
                executablePacketIndices == nullptr ||
                command.packetCount == 0 ||
                command.firstExecutableIndex >= executablePacketIndexCount ||
                !CanUseSurfaceGpuSceneCommand(ctx, state, command)) {
                return fail();
            }

            ID3D12Resource* argumentBuffer = ctx.surfaceIndirectDrawBuffer->GetArgumentBuffer();
            ID3D12CommandSignature* commandSignature =
                ctx.surfaceIndirectDrawBuffer->GetCommandSignature();
            UINT64 argumentOffset = 0;
            if (argumentBuffer == nullptr ||
                commandSignature == nullptr ||
                !ctx.surfaceIndirectDrawBuffer->TryGetArgumentBufferOffset(command, argumentOffset) ||
                !ctx.surfaceIndirectDrawBuffer->HasDrawBinding(command)) {
                return fail();
            }

            const size_t commandBegin = command.firstExecutableIndex;
            const size_t commandEnd = std::min(
                executablePacketIndexCount,
                commandBegin + static_cast<size_t>(command.packetCount));
            if (commandEnd != commandBegin + static_cast<size_t>(command.packetCount)) {
                return fail();
            }

            const uint32_t firstPacketIndex = executablePacketIndices[commandBegin];
            if (firstPacketIndex >= packetCount) {
                return fail();
            }
            const RENDER3D::RUNTIME::SurfaceDrawPacket& firstPacket = packets[firstPacketIndex];
            if (!IsInstanceBatchCompatiblePacket(ctx, state, firstPacket, firstPacket)) {
                return fail();
            }

            size_t preparedCount = 0;
            const size_t gpuSceneStart =
                ctx.surfaceGpuSceneBaseOffset +
                static_cast<size_t>(command.firstGpuSceneInstanceIndex);
            for (size_t executableIndex = commandBegin; executableIndex < commandEnd; ++executableIndex) {
                const uint32_t packetIndex = executablePacketIndices[executableIndex];
                if (packetIndex >= packetCount) {
                    return fail();
                }

                const RENDER3D::RUNTIME::SurfaceDrawPacket& packet = packets[packetIndex];
                if (!IsInstanceBatchCompatiblePacket(ctx, state, firstPacket, packet)) {
                    return fail();
                }
                if (!HasPreparedSurfaceGpuSceneMaterial(ctx, gpuSceneStart + preparedCount)) {
                    return fail();
                }
                ++preparedCount;
            }

            if (preparedCount == 0 ||
                preparedCount != static_cast<size_t>(command.drawArgs.instanceCount)) {
                return fail();
            }

            BindSurfaceGpuSceneBuffer(ctx.binding, ctx.surfaceGpuSceneSrv);
            BindObjectDataIndex(ctx.binding, 0u);
            BindMaterialDataIndex(ctx.binding, 0u);
            activeMesh = nullptr;

            ctx.cmd->ExecuteIndirect(
                commandSignature,
                1,
                argumentBuffer,
                argumentOffset,
                nullptr,
                0);

            result.submittedPacketCount += preparedCount;
            ++result.drawCallCount;
            result.maxInstanceCount = (std::max)(result.maxInstanceCount, preparedCount);
            if (preparedCount > 1) {
                ++result.instancedDrawCount;
                result.instancedPacketCount += preparedCount;
            }
            RecordSurfaceIndirectSubmit(ctx, command.batchKey, 1u, preparedCount);
            return true;
        }


        bool DrawStructuredMeshItem(
            const MeshDrawContext& ctx,
            const DrawItem& item,
            size_t& objectIndex) {
            const MATH::Mat4 world = item.transform.GetWorldMatrix();
            const MATH::Mat4 normalMatrix = BuildNormalMatrix(item.transform);

            for (size_t meshIndex = 0; meshIndex < item.asset->meshes.size(); ++meshIndex) {
                if (item.usePrimitiveFilter && meshIndex != item.meshIndexFilter) {
                    continue;
                }

                const MeshAsset& meshAsset = item.asset->meshes[meshIndex];
                for (size_t primitiveIndex = 0; primitiveIndex < meshAsset.primitives.size(); ++primitiveIndex) {
                    if (item.usePrimitiveFilter && primitiveIndex != item.primitiveIndexFilter) {
                        continue;
                    }

                    const MeshPrimitive& primitive = meshAsset.primitives[primitiveIndex];
                    if (objectIndex >= kMaxObjectCount) {
                        break;
                    }

                    const bool shouldDrawSkinned = !item.jointPalette.empty() && !primitive.skinnedVertices.empty();
                    bool drawingSkinned = false;
                    MeshPrimitiveCache* primitiveCache = ctx.services.primitiveCache;
                    Mesh* mesh = shouldDrawSkinned && primitiveCache != nullptr
                        ? primitiveCache->GetOrCreateSkinned(ctx.services.device, primitive, ctx.services.stats)
                        : (primitiveCache != nullptr ? primitiveCache->GetOrCreateStatic(ctx.services.device, primitive, ctx.services.stats) : nullptr);
                    drawingSkinned = shouldDrawSkinned && mesh != nullptr && mesh->IsValid();
                    if (mesh == nullptr || !mesh->IsValid()) {
                        if (shouldDrawSkinned && ctx.services.stats != nullptr) {
                            ++ctx.services.stats->skinnedFallbackCount;
                        }
                        mesh = primitiveCache != nullptr
                            ? primitiveCache->GetOrCreateStatic(ctx.services.device, primitive, ctx.services.stats)
                            : nullptr;
                        if (mesh == nullptr || !mesh->IsValid()) {
                            continue;
                        }
                    }

                    const MaterialAsset* materialAsset = GetPrimitiveMaterial(*item.asset, primitive.materialIndex);
                    ResolvedMaterialTextures textures{};
                    const Material* runtimeMaterial = item.materialOverride;
                    if (runtimeMaterial != nullptr) {
                        const MaterialTextureHandles handles =
                            ResolveRuntimeMaterialTextureHandles(runtimeMaterial, ctx.binding, ctx.materialFill);
                        textures.baseColor = handles.baseColor;
                        textures.normal = handles.normal;
                        textures.emissive = handles.emissive;
                        textures.metallicRoughness = handles.metallicRoughness;
                        textures.occlusion = handles.occlusion;
                    } else if (ctx.services.materialResolver != nullptr) {
                        textures = ctx.services.materialResolver->Resolve(*item.asset, materialAsset, ctx.services.stats);
                    } else {
                        textures.baseColor = ctx.binding.fallbackTextureHandle;
                        textures.normal = ctx.binding.fallbackNormalTextureHandle;
                        textures.emissive = ctx.materialFill.fallbackBlackTextureHandle;
                        textures.metallicRoughness = ctx.binding.fallbackTextureHandle;
                        textures.occlusion = ctx.binding.fallbackTextureHandle;
                    }

                    const VFX::VariantKey primitiveVariant = ResolvePrimitiveVariant(
                        item,
                        runtimeMaterial ? nullptr : materialAsset);
                    const bool bindLegacyObjectCB = drawingSkinned ||
                        !VariantCanUseObjectDataOnly(ctx.passKind, primitiveVariant);

                    ObjectCB obj{};
                    obj.world = world;
                    obj.normalMatrix = normalMatrix;
                    FillMaterialValues(
                        obj,
                        materialAsset,
                        textures.normal,
                        textures.emissive,
                        textures.metallicRoughness,
                        textures.occlusion,
                        ctx.materialFill);
                    if (runtimeMaterial != nullptr) {
                        // Material Asset override はモデル内 MaterialAsset より優先する。
                        FillRuntimeMaterialValues(obj, *runtimeMaterial);
                    }
                    obj.hasBaseColorTexture = (textures.baseColor >= 0 && textures.baseColor != ctx.binding.fallbackTextureHandle) ? 1u : 0u;
                    obj.receiveShadow = item.receiveShadow ? 1u : 0u;
                    FillFxValues(obj, item);
                    if (bindLegacyObjectCB) {
                        CopyObjectCB(ctx, obj, objectIndex);
                    }
                    const MaterialTextureHandles materialTextureHandles = ToMaterialTextureHandles(textures);
                    const MaterialGpuData materialData = BuildMaterialGpuData(obj, materialTextureHandles);
                    RecordMaterialTexturePoolStats(ctx, materialData);
                    const uint32_t materialDataIndex = UploadMaterialData(
                        ctx,
                        BuildMaterialDataKey(0u, materialData),
                        materialData);
                    CopyObjectData(ctx, obj, objectIndex, materialDataIndex);

                    const D3D12_GPU_VIRTUAL_ADDRESS objectAddress = ObjectAddress(ctx, objectIndex);
                    BindPerDrawCommon(
                        ctx,
                        drawingSkinned ? ctx.skinnedRootSig : ctx.staticRootSig,
                        objectAddress,
                        static_cast<uint32_t>(objectIndex),
                        materialDataIndex,
                        bindLegacyObjectCB);

                    if (drawingSkinned) {
                        const size_t uploadedJointCount = UploadJointPalette(ctx.jointPaletteMapped, objectIndex, item.jointPalette);
                        if (ctx.cmd != nullptr && ctx.jointPaletteCB != nullptr) {
                            ctx.cmd->SetGraphicsRootConstantBufferView(ROOT_PARAM::JointPalette, JointPaletteAddress(ctx, objectIndex));
                        }
                        if (ctx.services.stats != nullptr) {
                            ctx.services.stats->uploadedJointCount += uploadedJointCount;
                            ctx.services.stats->maxJointCount = std::max(ctx.services.stats->maxJointCount, item.jointPalette.size());
                            ctx.services.stats->lastSkinnedVertexCount = primitive.skinnedVertices.size();
                        }
                    }

                    BindShadowMap(ctx.binding);
                    BindSkyCube(ctx.binding);
                    BindSceneDepth(ctx.binding);
                    BindSceneColor(ctx.binding);
                    BindIblResources(ctx.binding);
                    BindReflectionProbeResources(ctx.binding);
                    BindSsao(ctx.binding);
                    BindLightProbeResources(ctx.binding);

                    D3D12_VERTEX_BUFFER_VIEW vb = mesh->GetVBView();
                    D3D12_INDEX_BUFFER_VIEW ib = mesh->GetIBView();
                    ctx.cmd->IASetVertexBuffers(0, 1, &vb);
                    ctx.cmd->IASetIndexBuffer(&ib);

                    DrawPrimitiveWithDebugMode(ctx, *mesh, primitiveVariant, drawingSkinned, item.renderDebugMode);

                    ++objectIndex;
                }
            }

            return true;
        }

        bool DrawLegacyMeshItem(
            const MeshDrawContext& ctx,
            const DrawItem& item,
            size_t& objectIndex) {
            if (!item.asset->GetMesh() || !item.asset->GetMesh()->IsValid()) {
                return true;
            }

            ObjectCB obj{};
            obj.world = item.transform.GetWorldMatrix();
            obj.normalMatrix = BuildNormalMatrix(item.transform);
            FillMaterialValues(
                obj,
                nullptr,
                ctx.binding.fallbackNormalTextureHandle,
                ctx.materialFill.fallbackBlackTextureHandle,
                ctx.binding.fallbackTextureHandle,
                ctx.binding.fallbackTextureHandle,
                ctx.materialFill);
            obj.receiveShadow = item.receiveShadow ? 1u : 0u;
            if (const Material* material = item.materialOverride ? item.materialOverride : item.asset->GetMaterial()) {
                obj.baseColor = material->GetBaseColor();
                obj.hasBaseColorTexture = material->HasBaseColorTexture() ? 1u : 0u;
                obj.hasNormalTexture = material->HasTextureSlot(ModelTextureUsage::Normal) ? 1u : 0u;
                obj.hasMetallicRoughnessTexture = material->HasTextureSlot(ModelTextureUsage::MetallicRoughness) ? 1u : 0u;
                obj.hasOcclusionTexture = material->HasTextureSlot(ModelTextureUsage::Occlusion) ? 1u : 0u;
                obj.hasEmissiveTexture = material->HasTextureSlot(ModelTextureUsage::Emissive) ? 1u : 0u;
                obj.normalScale = material->GetNormalScale();
                obj.metallicFactor = material->GetMetallicFactor();
                obj.roughnessFactor = material->GetRoughnessFactor();
                obj.occlusionStrength = material->GetOcclusionStrength();
                const MATH::Vec3& emissive = material->GetEmissiveFactor();
                obj.emissiveFactor = {
                    emissive.x,
                    emissive.y,
                    emissive.z,
                    material->GetEmissiveStrength()
                };
                obj.materialFlags = material->GetFeatureBits();
            } else {
                obj.baseColor = { 1, 1, 1, 1 };
                obj.hasBaseColorTexture = 0u;
            }
            FillFxValues(obj, item);
            const bool bindLegacyObjectCB =
                !VariantCanUseObjectDataOnly(ctx.passKind, item.variant);
            if (bindLegacyObjectCB) {
                CopyObjectCB(ctx, obj, objectIndex);
            }

            MaterialTextureHandles textureHandles{};
            textureHandles = ResolveRuntimeMaterialTextureHandles(
                item.materialOverride ? item.materialOverride : item.asset->GetMaterial(),
                ctx.binding,
                ctx.materialFill);
            const MaterialGpuData materialData = BuildMaterialGpuData(obj, textureHandles);
            RecordMaterialTexturePoolStats(ctx, materialData);
            const uint32_t materialDataIndex = UploadMaterialData(
                ctx,
                BuildMaterialDataKey(0u, materialData),
                materialData);
            CopyObjectData(ctx, obj, objectIndex, materialDataIndex);

            const D3D12_GPU_VIRTUAL_ADDRESS objectAddress = ObjectAddress(ctx, objectIndex);
            BindPerDrawCommon(
                ctx,
                ctx.staticRootSig,
                objectAddress,
                static_cast<uint32_t>(objectIndex),
                materialDataIndex,
                bindLegacyObjectCB);

            // 材質テクスチャは MaterialData の descriptor index から参照する。
            BindShadowMap(ctx.binding);
            BindSkyCube(ctx.binding);
            BindSceneDepth(ctx.binding);
            BindSceneColor(ctx.binding);
            BindIblResources(ctx.binding);
            BindReflectionProbeResources(ctx.binding);
            BindSsao(ctx.binding);
            BindLightProbeResources(ctx.binding);

            const Mesh* mesh = item.asset->GetMesh();
            D3D12_VERTEX_BUFFER_VIEW vb = mesh->GetVBView();
            D3D12_INDEX_BUFFER_VIEW ib = mesh->GetIBView();
            ctx.cmd->IASetVertexBuffers(0, 1, &vb);
            ctx.cmd->IASetIndexBuffer(&ib);

            DrawPrimitiveWithDebugMode(ctx, *mesh, item.variant, false, item.renderDebugMode);

            ++objectIndex;
            return true;
        }
    }

    bool DrawMeshItem(
        const MeshDrawContext& ctx,
        const DrawItem& item,
        size_t& objectIndex) {
        if (objectIndex >= kMaxObjectCount || item.asset == nullptr) {
            return false;
        }
        if (ctx.cmd == nullptr ||
            ctx.objectMapped == nullptr ||
            ctx.objectCB == nullptr ||
            ctx.objectDataMapped == nullptr ||
            ctx.objectDataBuffer == nullptr ||
            ctx.objectDataSrv.ptr == 0 ||
            ctx.materialDataMapped == nullptr ||
            ctx.materialDataBuffer == nullptr ||
            ctx.materialDataSrv.ptr == 0 ||
            ctx.materialDataTable == nullptr) {
            return false;
        }

        const bool hasStructuredGltfMeshes = !item.asset->meshes.empty();
        if (hasStructuredGltfMeshes) {
            return DrawStructuredMeshItem(ctx, item, objectIndex);
        }

        return DrawLegacyMeshItem(ctx, item, objectIndex);
    }

    void BindSurfacePacketFrameResources(const MeshDrawContext& ctx) {
        // SurfacePacket は frame 共通リソースを plan 単位で束縛する。
        BindSurfacePacketFrameResourcesInternal(ctx);
    }

    bool PrepareSurfacePacketGpuSceneMaterials(
        const MeshDrawContext& ctx,
        const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
        size_t packetCount,
        const uint32_t* executablePacketIndices,
        size_t executablePacketIndexCount,
        const RENDER3D::RUNTIME::SurfaceDrawCommand* commands,
        size_t commandCount) {

        if (ctx.surfaceGpuSceneFrameBuffer == nullptr ||
            packets == nullptr ||
            executablePacketIndices == nullptr ||
            commands == nullptr) {
            return false;
        }

        bool patchedAny = false;
        for (size_t commandIndex = 0; commandIndex < commandCount; ++commandIndex) {
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command = commands[commandIndex];
            if (command.packetCount == 0 ||
                command.firstGpuSceneInstanceIndex == RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex) {
                continue;
            }

            size_t commandBegin = 0;
            size_t commandEnd = 0;
            if (!TryResolveSurfaceCommandPacketRange(
                command,
                executablePacketIndexCount,
                commandBegin,
                commandEnd)) {
                continue;
            }

            const uint32_t firstPacketIndex = executablePacketIndices[commandBegin];
            if (firstPacketIndex >= packetCount) {
                continue;
            }

            SurfacePacketBatchState state{};
            const RENDER3D::RUNTIME::SurfaceDrawPacket& firstPacket = packets[firstPacketIndex];
            if (!PrepareSurfacePacketBatch(ctx, firstPacket, command, state) ||
                !CanUseSurfaceGpuSceneCommand(ctx, state, command) ||
                !IsInstanceBatchCompatiblePacket(ctx, state, firstPacket, firstPacket)) {
                continue;
            }

            size_t localIndex = 0;
            for (size_t executableIndex = commandBegin; executableIndex < commandEnd; ++executableIndex) {
                const uint32_t packetIndex = executablePacketIndices[executableIndex];
                if (packetIndex >= packetCount) {
                    break;
                }

                const RENDER3D::RUNTIME::SurfaceDrawPacket& packet = packets[packetIndex];
                if (!IsInstanceBatchCompatiblePacket(ctx, state, firstPacket, packet)) {
                    break;
                }

                SurfacePacketPreparedObject prepared{};
                if (!PrepareSurfacePacketObjectData(ctx, packet, prepared)) {
                    break;
                }

                // Execute時ではなく frame 準備段階で material index を SurfaceGpuScene に確定する。
                patchedAny =
                    PatchSurfaceGpuSceneMaterialData(
                        ctx,
                        ctx.surfaceGpuSceneBaseOffset +
                        static_cast<size_t>(command.firstGpuSceneInstanceIndex) +
                        localIndex,
                        prepared.materialDataIndex) ||
                    patchedAny;
                ++localIndex;
            }
        }

        return patchedAny;
    }

    bool PrepareSurfacePacketIndirectDrawBindings(
        const MeshDrawContext& ctx,
        const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
        size_t packetCount,
        const uint32_t* executablePacketIndices,
        size_t executablePacketIndexCount,
        const RENDER3D::RUNTIME::SurfaceDrawCommand* commands,
        size_t commandCount) {

        if (ctx.surfaceIndirectDrawBuffer == nullptr ||
            packets == nullptr ||
            executablePacketIndices == nullptr ||
            commands == nullptr) {
            return false;
        }

        bool patchedAny = false;
        for (size_t commandIndex = 0; commandIndex < commandCount; ++commandIndex) {
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command = commands[commandIndex];
            if (!CanStartSurfaceIndirectCommandRange(ctx, command)) {
                continue;
            }

            size_t commandBegin = 0;
            size_t commandEnd = 0;
            if (!TryResolveSurfaceCommandPacketRange(
                command,
                executablePacketIndexCount,
                commandBegin,
                commandEnd)) {
                continue;
            }
            (void)commandEnd;

            const uint32_t firstPacketIndex = executablePacketIndices[commandBegin];
            if (firstPacketIndex >= packetCount) {
                continue;
            }

            Mesh* mesh = ResolveSurfacePacketStaticMesh(ctx, packets[firstPacketIndex]);
            if (mesh == nullptr || !mesh->IsValid()) {
                continue;
            }

            patchedAny =
                ctx.surfaceIndirectDrawBuffer->PatchDrawBinding(
                    command,
                    mesh->GetVBView(),
                    mesh->GetIBView()) ||
                patchedAny;
        }

        return patchedAny;
    }

    SurfacePacketCommandDrawResult DrawSurfacePacketCommand(
        const MeshDrawContext& ctx,
        const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
        size_t packetCount,
        const uint32_t* executablePacketIndices,
        size_t executablePacketIndexCount,
        const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
        size_t& objectIndex) {

        SurfacePacketCommandDrawResult result{};
        if (packets == nullptr ||
            executablePacketIndices == nullptr ||
            ctx.objectDataMapped == nullptr ||
            ctx.objectDataBuffer == nullptr ||
            ctx.objectDataSrv.ptr == 0 ||
            ctx.materialDataMapped == nullptr ||
            ctx.materialDataBuffer == nullptr ||
            ctx.materialDataSrv.ptr == 0 ||
            ctx.materialDataTable == nullptr ||
            command.packetCount == 0 ||
            command.firstExecutableIndex >= executablePacketIndexCount) {
            return result;
        }

        const size_t commandBegin = command.firstExecutableIndex;
        const size_t commandEnd = std::min(
            executablePacketIndexCount,
            commandBegin + static_cast<size_t>(command.packetCount));

        SurfacePacketBatchState state{};
        size_t firstDrawableIndex = commandBegin;
        for (; firstDrawableIndex < commandEnd; ++firstDrawableIndex) {
            const uint32_t packetIndex = executablePacketIndices[firstDrawableIndex];
            if (packetIndex >= packetCount) {
                ++result.skippedPacketCount;
                continue;
            }
            if (PrepareSurfacePacketBatch(ctx, packets[packetIndex], command, state)) {
                break;
            }
            ++result.skippedPacketCount;
        }

        if (firstDrawableIndex >= commandEnd) {
            return result;
        }

        const Mesh* activeMesh = nullptr;
        if (firstDrawableIndex == commandBegin &&
            TryExecuteSurfacePacketIndirectCommand(
                ctx,
                state,
                command,
                packets,
                packetCount,
                executablePacketIndices,
                executablePacketIndexCount,
                activeMesh,
                result)) {
            return result;
        }

        for (size_t executableIndex = firstDrawableIndex; executableIndex < commandEnd; ++executableIndex) {
            const uint32_t packetIndex = executablePacketIndices[executableIndex];
            if (packetIndex >= packetCount) {
                ++result.skippedPacketCount;
                continue;
            }

            if (DrawSurfacePacketInstanceBatch(
                ctx,
                state,
                command,
                packets,
                packetCount,
                executablePacketIndices,
                executablePacketIndexCount,
                commandEnd,
                executableIndex,
                activeMesh,
                objectIndex,
                result)) {
                continue;
            }

            if (DrawPreparedSurfacePacket(ctx, state, packets[packetIndex], activeMesh, objectIndex)) {
                ++result.submittedPacketCount;
                ++result.drawCallCount;
                result.maxInstanceCount = (std::max)(result.maxInstanceCount, size_t{ 1 });
            } else {
                ++result.skippedPacketCount;
            }
        }

        return result;
    }

    SurfacePacketCommandDrawResult DrawSurfacePacketCommandRange(
        const MeshDrawContext& ctx,
        const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
        size_t packetCount,
        const uint32_t* executablePacketIndices,
        size_t executablePacketIndexCount,
        const RENDER3D::RUNTIME::SurfaceDrawCommand* commands,
        size_t commandCount,
        size_t& commandIndex,
        size_t& objectIndex) {

        SurfacePacketCommandDrawResult result{};
        if (commands == nullptr || commandIndex >= commandCount) {
            return result;
        }

        const size_t currentCommandIndex = commandIndex;
        const Mesh* activeMesh = nullptr;
        size_t nextCommandIndex = currentCommandIndex;
        if (TryExecuteSurfacePacketIndirectCommandRange(
            ctx,
            commands,
            commandCount,
            currentCommandIndex,
            packets,
            packetCount,
            executablePacketIndices,
            executablePacketIndexCount,
            activeMesh,
            nextCommandIndex,
            result)) {
            commandIndex = nextCommandIndex;
            return result;
        }

        result = DrawSurfacePacketCommand(
            ctx,
            packets,
            packetCount,
            executablePacketIndices,
            executablePacketIndexCount,
            commands[currentCommandIndex],
            objectIndex);
        commandIndex = currentCommandIndex + 1;
        return result;
    }

} // namespace HIKARI::MESHRENDERER
