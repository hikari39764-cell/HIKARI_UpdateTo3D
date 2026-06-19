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
#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceIndirectDrawBuffer.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawRoute.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::MESHRENDERER {

    namespace {
        // GPU Scene は SurfaceRecord の通常経路として消費する、E
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
            if (passKind == MeshDrawPassKind::GeometryAux) {
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
            if (passKind == MeshDrawPassKind::GeometryAux) {
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

            if (ctx.passKind == MeshDrawPassKind::GeometryAux) {
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

        struct SurfaceRecordBatchState {
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

        struct SurfaceRecordPreparedObject {
            ObjectCB object{};
            uint32_t materialDataIndex = kInvalidMaterialDataIndex;
        };

        bool CanUseSurfaceGpuSceneCommand(
            const MeshDrawContext& ctx,
            const SurfaceRecordBatchState& state,
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
                command.gpuSceneInstanceCount < command.recordCount ||
                !VariantCanUseSurfaceGpuScene(ctx.passKind, state.variant)) {
                return false;
            }

            const RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBufferStats& gpuSceneStats =
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

        Transform3D BuildRecordDrawTransform(const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record) {
            Transform3D transform = record.objectWorldTransform;
            transform.useExplicitMatrix = true;
            transform.explicitMatrix = record.drawWorldMatrix;
            return transform;
        }

        DrawItem BuildBatchVariantAdapter(const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record) {
            DrawItem item{};
            item.asset = record.model;
            item.materialOverride = record.materialOverride;
            item.materialFxProfileId = record.materialFxProfileId;
            item.postGroupMask = record.postGroupMask;
            // Batch の既定値だけを解決し、個別 override は record 側で反映する、E
            item.materialFxValuesInitialized = false;
            ResolveDrawVariant(item);
            return item;
        }


        ResolvedMaterialTextures ResolveRecordTextures(
            const MeshDrawContext& ctx,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record,
            const MaterialAsset* materialAsset) {

            ResolvedMaterialTextures textures{};
            if (record.materialOverride != nullptr) {
                const MaterialTextureHandles handles =
                    ResolveRuntimeMaterialTextureHandles(record.materialOverride, ctx.binding, ctx.materialFill);
                textures.baseColor = handles.baseColor;
                textures.normal = handles.normal;
                textures.emissive = handles.emissive;
                textures.metallicRoughness = handles.metallicRoughness;
                textures.occlusion = handles.occlusion;
                return textures;
            }

            if (ctx.services.materialResolver != nullptr && record.model != nullptr) {
                return ctx.services.materialResolver->Resolve(*record.model, materialAsset, ctx.services.stats);
            }

            textures.baseColor = ctx.binding.fallbackTextureHandle;
            textures.normal = ctx.binding.fallbackNormalTextureHandle;
            textures.emissive = ctx.materialFill.fallbackBlackTextureHandle;
            textures.metallicRoughness = ctx.binding.fallbackTextureHandle;
            textures.occlusion = ctx.binding.fallbackTextureHandle;
            return textures;
        }

        ResolvedMaterialTextures ResolveGpuSceneMaterialSourceTextures(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source,
            const MaterialAsset* materialAsset) {

            ResolvedMaterialTextures textures{};
            if (source.materialOverride != nullptr) {
                const MaterialTextureHandles handles =
                    ResolveRuntimeMaterialTextureHandles(source.materialOverride, ctx.binding, ctx.materialFill);
                textures.baseColor = handles.baseColor;
                textures.normal = handles.normal;
                textures.emissive = handles.emissive;
                textures.metallicRoughness = handles.metallicRoughness;
                textures.occlusion = handles.occlusion;
                return textures;
            }

            if (ctx.services.materialResolver != nullptr && source.model != nullptr) {
                return ctx.services.materialResolver->Resolve(*source.model, materialAsset, ctx.services.stats);
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

        void BindSurfaceRecordFrameResourcesInternal(const MeshDrawContext& ctx) {

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

        bool ResolveSurfaceRecordBatchState(
            const MeshDrawContext& ctx,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& firstRecord,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            SurfaceRecordBatchState& outState) {

            if (ctx.services.device == nullptr ||
                ctx.services.pipelines == nullptr ||
                firstRecord.model == nullptr ||
                firstRecord.meshIndex >= firstRecord.model->meshes.size()) {
                return false;
            }
            if (!RENDER3D::RUNTIME::IsValidSurfaceDrawBatchKey(command.batchKey) ||
                !RENDER3D::RUNTIME::IsSameSurfaceDrawBatchKey(
                    RENDER3D::RUNTIME::BuildSurfaceDrawBatchKey(command.pass, firstRecord.key),
                    command.batchKey)) {
                return false;
            }

            const MeshAsset& meshAsset = firstRecord.model->meshes[firstRecord.meshIndex];
            if (firstRecord.primitiveIndex >= meshAsset.primitives.size()) {
                return false;
            }

            const MeshPrimitive& primitive = meshAsset.primitives[firstRecord.primitiveIndex];
            outState = {};
            outState.model = firstRecord.model;
            outState.runtimeMaterial = firstRecord.materialOverride;
            outState.materialAsset = GetPrimitiveMaterial(*firstRecord.model, primitive.materialIndex);
            outState.batchKey = command.batchKey;

            DrawItem variantItem = BuildBatchVariantAdapter(firstRecord);
            outState.defaultFxValues = variantItem.fxValues;
            outState.fxFlags = variantItem.fxFlags;
            outState.variant = ResolvePrimitiveVariant(
                variantItem,
                outState.runtimeMaterial != nullptr ? nullptr : outState.materialAsset,
                &primitive);
            outState.objectDataCompatible = VariantCanUseObjectDataOnly(ctx.passKind, outState.variant);
            if (ctx.passKind == MeshDrawPassKind::GeometryAux && !outState.objectDataCompatible) {
                return false;
            }

            return true;
        }

        bool PrepareSurfaceRecordBatch(
            const MeshDrawContext& ctx,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& firstRecord,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            SurfaceRecordBatchState& outState) {

            if (ctx.cmd == nullptr ||
                ctx.services.stats == nullptr ||
                !ResolveSurfaceRecordBatchState(ctx, firstRecord, command, outState)) {
                return false;
            }

            BindMaterialDataIndex(ctx.binding, 0u);

            ID3D12PipelineState* pso = nullptr;
            if (ctx.passKind == MeshDrawPassKind::GeometryAux) {
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

        bool IsBatchCompatibleRecord(
            const SurfaceRecordBatchState& state,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record) {

            return RENDER3D::RUNTIME::IsSameSurfaceDrawBatchKey(
                RENDER3D::RUNTIME::BuildSurfaceDrawBatchKey(state.batchKey.pass, record.key),
                state.batchKey);
        }

        Mesh* ResolveSurfaceRecordStaticMesh(
            const MeshDrawContext& ctx,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record) {

            if (record.model == nullptr ||
                record.meshIndex >= record.model->meshes.size()) {
                return nullptr;
            }

            const MeshAsset& meshAsset = record.model->meshes[record.meshIndex];
            if (record.primitiveIndex >= meshAsset.primitives.size()) {
                return nullptr;
            }

            MeshPrimitiveCache* primitiveCache = ctx.services.primitiveCache;
            if (primitiveCache == nullptr) {
                return nullptr;
            }

            const MeshPrimitive& primitive = meshAsset.primitives[record.primitiveIndex];
            return primitiveCache->GetOrCreateStatic(ctx.services.device, primitive, ctx.services.stats);
        }

        const MeshPrimitive* ResolveSurfaceRecordPrimitive(
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

        Mesh* ResolveSurfaceRecordSkinnedMesh(
            const MeshDrawContext& ctx,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record) {

            const MeshPrimitive* primitive = ResolveSurfaceRecordPrimitive(record);
            MeshPrimitiveCache* primitiveCache = ctx.services.primitiveCache;
            if (primitive == nullptr || primitiveCache == nullptr) {
                return nullptr;
            }

            return primitiveCache->GetOrCreateSkinned(
                ctx.services.device,
                *primitive,
                ctx.services.stats);
        }

        void FillRecordFxValues(
            ObjectCB& obj,
            const SurfaceRecordBatchState& state,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record) {

            obj.fxFlags = state.fxFlags;
            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                obj.fxUser[i] = state.defaultFxValues[i];
            }

            if (!record.materialFxValuesInitialized) {
                return;
            }

            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                const DirectX::XMFLOAT4& value = record.materialFxParamValues[i];
                obj.fxUser[i] = { value.x, value.y, value.z, value.w };
            }
        }

        void FillRecordFxValues(
            ObjectCB& obj,
            const DrawItem& variantItem,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record) {

            obj.fxFlags = variantItem.fxFlags;
            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                obj.fxUser[i] = variantItem.fxValues[i];
            }

            if (!record.materialFxValuesInitialized) {
                return;
            }

            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                const DirectX::XMFLOAT4& value = record.materialFxParamValues[i];
                obj.fxUser[i] = { value.x, value.y, value.z, value.w };
            }
        }

        void FillSurfaceRecordObject(
            const MeshDrawContext& ctx,
            const SurfaceRecordBatchState& state,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record,
            ObjectCB& obj) {

            const Transform3D drawTransform = BuildRecordDrawTransform(record);
            obj = {};
            obj.world = record.drawWorldMatrix;
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
            obj.receiveShadow = record.receiveShadow ? 1u : 0u;
            FillRecordFxValues(obj, state, record);
        }

        bool PrepareSurfaceRecordObjectData(
            const MeshDrawContext& ctx,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record,
            SurfaceRecordPreparedObject& outPrepared) {

            if (record.model == nullptr ||
                record.meshIndex >= record.model->meshes.size()) {
                return false;
            }

            const MeshAsset& meshAsset = record.model->meshes[record.meshIndex];
            if (record.primitiveIndex >= meshAsset.primitives.size()) {
                return false;
            }

            const MeshPrimitive& primitive = meshAsset.primitives[record.primitiveIndex];
            const MaterialAsset* materialAsset = GetPrimitiveMaterial(*record.model, primitive.materialIndex);
            const ResolvedMaterialTextures textures = ResolveRecordTextures(ctx, record, materialAsset);
            const DrawItem variantItem = BuildBatchVariantAdapter(record);

            ObjectCB obj{};
            const Transform3D drawTransform = BuildRecordDrawTransform(record);
            obj.world = record.drawWorldMatrix;
            obj.normalMatrix = BuildNormalMatrix(drawTransform);
            FillMaterialValues(
                obj,
                materialAsset,
                textures.normal,
                textures.emissive,
                textures.metallicRoughness,
                textures.occlusion,
                ctx.materialFill);
            if (record.materialOverride != nullptr) {
                FillRuntimeMaterialValues(obj, *record.materialOverride);
            }
            obj.hasBaseColorTexture =
                (textures.baseColor >= 0 && textures.baseColor != ctx.binding.fallbackTextureHandle) ? 1u : 0u;
            obj.receiveShadow = record.receiveShadow ? 1u : 0u;
            FillRecordFxValues(obj, variantItem, record);

            const MaterialTextureHandles textureHandles = ToMaterialTextureHandles(textures);
            const MaterialGpuData materialData = BuildMaterialGpuData(obj, textureHandles);
            RecordMaterialTexturePoolStats(ctx, materialData);
            const uint32_t materialDataIndex = UploadMaterialData(
                ctx,
                BuildMaterialDataKey(record.key.materialKey, materialData),
                materialData);

            outPrepared.object = obj;
            outPrepared.materialDataIndex =
                materialDataIndex == kInvalidMaterialDataIndex ? 0u : materialDataIndex;
            return true;
        }

        bool PrepareGpuSceneMaterialSourceData(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source,
            SurfaceRecordPreparedObject& outPrepared) {

            if (source.model == nullptr) {
                return false;
            }

            const MaterialAsset* materialAsset =
                GetPrimitiveMaterial(*source.model, source.materialIndex);
            const ResolvedMaterialTextures textures =
                ResolveGpuSceneMaterialSourceTextures(ctx, source, materialAsset);

            ObjectCB obj{};
            obj.world = source.world;
            obj.normalMatrix = source.normalMatrix;
            FillMaterialValues(
                obj,
                materialAsset,
                textures.normal,
                textures.emissive,
                textures.metallicRoughness,
                textures.occlusion,
                ctx.materialFill);
            if (source.materialOverride != nullptr) {
                FillRuntimeMaterialValues(obj, *source.materialOverride);
            }
            obj.hasBaseColorTexture =
                (textures.baseColor >= 0 && textures.baseColor != ctx.binding.fallbackTextureHandle) ? 1u : 0u;
            obj.receiveShadow = source.receiveShadow ? 1u : 0u;
            obj.fxFlags = source.fxFlags;
            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                obj.fxUser[i] = source.fxUser[i];
            }

            const MaterialTextureHandles textureHandles = ToMaterialTextureHandles(textures);
            const MaterialGpuData materialData = BuildMaterialGpuData(obj, textureHandles);
            RecordMaterialTexturePoolStats(ctx, materialData);
            const uint32_t materialDataIndex = UploadMaterialData(
                ctx,
                BuildMaterialDataKey(source.materialKey, materialData),
                materialData);

            outPrepared.object = obj;
            outPrepared.materialDataIndex =
                materialDataIndex == kInvalidMaterialDataIndex ? 0u : materialDataIndex;
            return true;
        }

        bool IsInstanceBatchCompatibleRecord(
            const MeshDrawContext& ctx,
            const SurfaceRecordBatchState& state,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& firstRecord,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record) {

            (void)ctx;
            if (!IsBatchCompatibleRecord(state, record) ||
                record.meshIndex != firstRecord.meshIndex ||
                record.primitiveIndex != firstRecord.primitiveIndex) {
                return false;
            }

            return state.objectDataCompatible;
        }


        bool BindSurfaceRecordMesh(
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

        bool DrawPreparedSurfaceRecord(
            const MeshDrawContext& ctx,
            const SurfaceRecordBatchState& state,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record,
            const Mesh*& activeMesh,
            size_t& objectIndex) {

            if (objectIndex >= kMaxObjectCount ||
                !record.hasDrawWorldMatrix ||
                record.model == nullptr ||
                record.meshIndex >= record.model->meshes.size() ||
                !IsBatchCompatibleRecord(state, record)) {
                return false;
            }

            Mesh* mesh = ResolveSurfaceRecordStaticMesh(ctx, record);
            if (mesh == nullptr || !mesh->IsValid()) {
                return false;
            }

            SurfaceRecordPreparedObject prepared{};
            if (!PrepareSurfaceRecordObjectData(ctx, record, prepared)) {
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

            if (!BindSurfaceRecordMesh(ctx, mesh, activeMesh)) {
                return false;
            }

            ctx.cmd->DrawIndexedInstanced(mesh->GetIndexCount(), 1, 0, 0, 0);
            ++objectIndex;
            return true;
        }

        bool DrawSurfaceRecordInstanceBatch(
            const MeshDrawContext& ctx,
            const SurfaceRecordBatchState& state,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
            size_t recordCount,
            const uint32_t* executableRecordIndices,
            size_t executableRecordIndexCount,
            size_t batchEnd,
            size_t& executableIndex,
            const Mesh*& activeMesh,
            size_t& objectIndex,
            SurfaceRecordCommandDrawResult& result) {

            const uint32_t firstRecordIndex = executableRecordIndices[executableIndex];
            if (firstRecordIndex >= recordCount) {
                ++result.skippedRecordCount;
                return false;
            }

            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& firstRecord = records[firstRecordIndex];
            if (!IsInstanceBatchCompatibleRecord(ctx, state, firstRecord, firstRecord)) {
                return false;
            }

            Mesh* mesh = ResolveSurfaceRecordStaticMesh(ctx, firstRecord);
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
            for (; cursor < batchEnd && cursor < executableRecordIndexCount; ++cursor) {
                const uint32_t recordIndex = executableRecordIndices[cursor];
                if (recordIndex >= recordCount) {
                    break;
                }

                const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record = records[recordIndex];
                if (!IsInstanceBatchCompatibleRecord(ctx, state, firstRecord, record) ||
                    (!useSurfaceGpuScene && objectIndex + batchCount >= kMaxObjectCount)) {
                    break;
                }

                SurfaceRecordPreparedObject prepared{};
                if (!PrepareSurfaceRecordObjectData(ctx, record, prepared)) {
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
                    ++ctx.services.stats->surfaceRecordExecutorGpuSceneDrawCount;
                    ctx.services.stats->surfaceRecordExecutorGpuSceneRecordCount += batchCount;
                }
            } else {
                BindSurfaceGpuSceneControl(ctx.binding, 0u, false);
                BindObjectDataIndex(ctx.binding, static_cast<uint32_t>(batchObjectStart));
                if (ctx.services.stats != nullptr) {
                    ++ctx.services.stats->surfaceRecordExecutorGpuSceneFallbackCount;
                }
            }
            BindMaterialDataIndex(ctx.binding, batchMaterialDataIndex);
            if (!BindSurfaceRecordMesh(ctx, mesh, activeMesh)) {
                return false;
            }

            // GPU-driven record path note.
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
            result.submittedRecordCount += batchCount;
            ++result.drawCallCount;
            result.maxInstanceCount = (std::max)(result.maxInstanceCount, batchCount);
            if (batchCount > 1) {
                ++result.instancedDrawCount;
                result.instancedRecordCount += batchCount;
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
            size_t recordCount) {

            if (ctx.services.stats == nullptr || commandCount == 0) {
                return;
            }

            MeshRendererDebugStats& stats = *ctx.services.stats;
            stats.surfaceRecordExecutorGpuSceneDrawCount += commandCount;
            stats.surfaceRecordExecutorGpuSceneRecordCount += recordCount;
            stats.surfaceIndirectExecutedDrawCount += commandCount;
            stats.surfaceIndirectExecutedRecordCount += recordCount;
            if (batchKey.pass == RENDER3D::RUNTIME::SurfaceDrawCommandPass::DepthAware) {
                stats.surfaceIndirectDepthAwareCommandCount += commandCount;
                stats.surfaceIndirectDepthAwareRecordCount += recordCount;
            } else if (batchKey.transparent) {
                stats.surfaceIndirectTransparentCommandCount += commandCount;
                stats.surfaceIndirectTransparentRecordCount += recordCount;
            } else {
                stats.surfaceIndirectOpaqueCommandCount += commandCount;
                stats.surfaceIndirectOpaqueRecordCount += recordCount;
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
                (ctx.passKind == MeshDrawPassKind::GeometryAux && !command.transparent);
            const bool supportedCommandPass =
                command.pass == RENDER3D::RUNTIME::SurfaceDrawCommandPass::Forward ||
                (ctx.passKind == MeshDrawPassKind::Forward &&
                    command.pass == RENDER3D::RUNTIME::SurfaceDrawCommandPass::DepthAware);
            return
                supportedPass &&
                supportedCommandPass &&
                command.backend == RENDER3D::RUNTIME::SurfaceDrawCommandBackend::GpuDriven &&
                command.recordCount > 0 &&
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
            if (ctx.passKind == MeshDrawPassKind::GeometryAux) {
                // GPU-driven record path note.
                return !lhs.transparent;
            }
            return
                lhs.psoKey != 0 &&
                lhs.psoKey == rhs.psoKey;
        }

        bool TryResolveSurfaceCommandRecordRange(
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            size_t executableRecordIndexCount,
            size_t& outBegin,
            size_t& outEnd) {

            const size_t begin = command.firstExecutableIndex;
            const size_t count = static_cast<size_t>(command.recordCount);
            if (count == 0 || begin >= executableRecordIndexCount) {
                return false;
            }

            const size_t end = begin + count;
            if (end < begin || end > executableRecordIndexCount) {
                return false;
            }

            outBegin = begin;
            outEnd = end;
            return true;
        }

        struct SurfaceIndirectPreparedCommand {
            SurfaceRecordBatchState state{};
            const Mesh* mesh = nullptr;
            UINT64 argumentOffset = 0;
            size_t recordCount = 0;
        };

        bool TryPrepareSurfaceIndirectCommand(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawBatchKey& rangeKey,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
            size_t recordCount,
            const uint32_t* executableRecordIndices,
            size_t executableRecordIndexCount,
            SurfaceIndirectPreparedCommand& outPrepared) {

            outPrepared = {};
            if (!CanStartSurfaceIndirectCommandRange(ctx, command) ||
                !IsSameSurfaceIndirectPipeline(ctx, rangeKey, command.batchKey) ||
                ctx.surfaceIndirectDrawBuffer == nullptr) {
                return false;
            }

            size_t commandBegin = 0;
            size_t commandEnd = 0;
            if (!TryResolveSurfaceCommandRecordRange(
                command,
                executableRecordIndexCount,
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

            const uint32_t firstRecordIndex = executableRecordIndices[commandBegin];
            if (firstRecordIndex >= recordCount) {
                return false;
            }

            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& firstRecord = records[firstRecordIndex];
            if (!PrepareSurfaceRecordBatch(ctx, firstRecord, command, outPrepared.state) ||
                !CanUseSurfaceGpuSceneCommand(ctx, outPrepared.state, command) ||
                !IsInstanceBatchCompatibleRecord(ctx, outPrepared.state, firstRecord, firstRecord)) {
                return false;
            }

            Mesh* mesh = ResolveSurfaceRecordStaticMesh(ctx, firstRecord);
            if (mesh == nullptr || !mesh->IsValid()) {
                return false;
            }

            outPrepared.mesh = mesh;
            size_t preparedRecordCount = 0;
            const size_t gpuSceneStart =
                ctx.surfaceGpuSceneBaseOffset +
                static_cast<size_t>(command.firstGpuSceneInstanceIndex);
            for (size_t executableIndex = commandBegin; executableIndex < commandEnd; ++executableIndex) {
                const uint32_t recordIndex = executableRecordIndices[executableIndex];
                if (recordIndex >= recordCount) {
                    return false;
                }

                const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record = records[recordIndex];
                if (!IsInstanceBatchCompatibleRecord(ctx, outPrepared.state, firstRecord, record)) {
                    return false;
                }
                if (!HasPreparedSurfaceGpuSceneMaterial(
                    ctx,
                    gpuSceneStart + preparedRecordCount)) {
                    return false;
                }
                ++preparedRecordCount;
            }

            outPrepared.recordCount = preparedRecordCount;
            return
                outPrepared.recordCount > 0 &&
                outPrepared.recordCount == static_cast<size_t>(command.drawArgs.instanceCount);
        }

        bool TryExecuteSurfaceRecordIndirectCommandRange(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawCommand* commands,
            size_t commandCount,
            size_t commandIndex,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
            size_t recordCount,
            const uint32_t* executableRecordIndices,
            size_t executableRecordIndexCount,
            const Mesh*& activeMesh,
            size_t& outNextCommandIndex,
            SurfaceRecordCommandDrawResult& result) {

            if (ctx.cmd == nullptr ||
                ctx.surfaceIndirectDrawBuffer == nullptr ||
                commands == nullptr ||
                records == nullptr ||
                executableRecordIndices == nullptr ||
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
                static_cast<UINT64>(sizeof(RENDER3D::GPUDRIVEN::SurfaceIndirectDrawArgument));
            UINT64 firstArgumentOffset = 0;
            size_t preparedCommandCount = 0;
            size_t preparedRecordCount = 0;
            size_t maxInstanceCount = 0;
            size_t instancedDrawCount = 0;
            size_t instancedRecordCount = 0;

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
                    records,
                    recordCount,
                    executableRecordIndices,
                    executableRecordIndexCount,
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
                preparedRecordCount += prepared.recordCount;
                maxInstanceCount = (std::max)(maxInstanceCount, prepared.recordCount);
                if (prepared.recordCount > 1) {
                    ++instancedDrawCount;
                    instancedRecordCount += prepared.recordCount;
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

            result.submittedRecordCount += preparedRecordCount;
            result.drawCallCount += preparedCommandCount;
            result.maxInstanceCount = (std::max)(result.maxInstanceCount, maxInstanceCount);
            result.instancedDrawCount += instancedDrawCount;
            result.instancedRecordCount += instancedRecordCount;
            RecordSurfaceIndirectSubmit(ctx, rangeKey, preparedCommandCount, preparedRecordCount);

            outNextCommandIndex = commandIndex + preparedCommandCount;
            return true;
        }

        bool TryExecuteSurfaceRecordIndirectCommand(
            const MeshDrawContext& ctx,
            const SurfaceRecordBatchState& state,
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
            size_t recordCount,
            const uint32_t* executableRecordIndices,
            size_t executableRecordIndexCount,
            const Mesh*& activeMesh,
            SurfaceRecordCommandDrawResult& result) {

            if (!CanStartSurfaceIndirectCommandRange(ctx, command)) {
                return false;
            }

            auto fail = [&]() {
                RecordSurfaceIndirectFallback(ctx, command);
                return false;
            };

            if (ctx.cmd == nullptr ||
                ctx.surfaceIndirectDrawBuffer == nullptr ||
                records == nullptr ||
                executableRecordIndices == nullptr ||
                command.recordCount == 0 ||
                command.firstExecutableIndex >= executableRecordIndexCount ||
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
                executableRecordIndexCount,
                commandBegin + static_cast<size_t>(command.recordCount));
            if (commandEnd != commandBegin + static_cast<size_t>(command.recordCount)) {
                return fail();
            }

            const uint32_t firstRecordIndex = executableRecordIndices[commandBegin];
            if (firstRecordIndex >= recordCount) {
                return fail();
            }
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& firstRecord = records[firstRecordIndex];
            if (!IsInstanceBatchCompatibleRecord(ctx, state, firstRecord, firstRecord)) {
                return fail();
            }

            size_t preparedCount = 0;
            const size_t gpuSceneStart =
                ctx.surfaceGpuSceneBaseOffset +
                static_cast<size_t>(command.firstGpuSceneInstanceIndex);
            for (size_t executableIndex = commandBegin; executableIndex < commandEnd; ++executableIndex) {
                const uint32_t recordIndex = executableRecordIndices[executableIndex];
                if (recordIndex >= recordCount) {
                    return fail();
                }

                const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record = records[recordIndex];
                if (!IsInstanceBatchCompatibleRecord(ctx, state, firstRecord, record)) {
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

            result.submittedRecordCount += preparedCount;
            ++result.drawCallCount;
            result.maxInstanceCount = (std::max)(result.maxInstanceCount, preparedCount);
            if (preparedCount > 1) {
                ++result.instancedDrawCount;
                result.instancedRecordCount += preparedCount;
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
                        runtimeMaterial ? nullptr : materialAsset,
                        &primitive);
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
                        // GPU-driven record path note.
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

            // GPU-driven record path note.
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

    void BindSurfaceRecordFrameResources(const MeshDrawContext& ctx) {
        // SurfaceRecord は frame 共通リソースめEplan 単位で束縛する、E
        BindSurfaceRecordFrameResourcesInternal(ctx);
    }

    bool PrepareSurfaceRecordGpuSceneMaterials(
        const MeshDrawContext& ctx,
        const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
        size_t recordCount,
        const uint32_t* executableRecordIndices,
        size_t executableRecordIndexCount,
        const RENDER3D::RUNTIME::SurfaceDrawCommand* commands,
        size_t commandCount) {

        if (ctx.surfaceGpuSceneFrameBuffer == nullptr ||
            records == nullptr ||
            executableRecordIndices == nullptr ||
            commands == nullptr) {
            return false;
        }

        bool patchedAny = false;
        for (size_t commandIndex = 0; commandIndex < commandCount; ++commandIndex) {
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command = commands[commandIndex];
            if (command.recordCount == 0 ||
                command.firstGpuSceneInstanceIndex == RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex) {
                continue;
            }

            size_t commandBegin = 0;
            size_t commandEnd = 0;
            if (!TryResolveSurfaceCommandRecordRange(
                command,
                executableRecordIndexCount,
                commandBegin,
                commandEnd)) {
                continue;
            }

            const uint32_t firstRecordIndex = executableRecordIndices[commandBegin];
            if (firstRecordIndex >= recordCount) {
                continue;
            }

            SurfaceRecordBatchState state{};
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& firstRecord = records[firstRecordIndex];
            // GPU-driven record path note.
            if (!ResolveSurfaceRecordBatchState(ctx, firstRecord, command, state) ||
                !CanUseSurfaceGpuSceneCommand(ctx, state, command) ||
                !IsInstanceBatchCompatibleRecord(ctx, state, firstRecord, firstRecord)) {
                continue;
            }

            size_t localIndex = 0;
            for (size_t executableIndex = commandBegin; executableIndex < commandEnd; ++executableIndex) {
                const uint32_t recordIndex = executableRecordIndices[executableIndex];
                if (recordIndex >= recordCount) {
                    break;
                }

                const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record = records[recordIndex];
                if (!IsInstanceBatchCompatibleRecord(ctx, state, firstRecord, record)) {
                    break;
                }

                SurfaceRecordPreparedObject prepared{};
                if (!PrepareSurfaceRecordObjectData(ctx, record, prepared)) {
                    break;
                }

                // Execute時ではなぁEframe 準備段階で material index めESurfaceGpuScene に確定する、E
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

    bool PrepareSurfaceGpuSceneInstanceMaterials(
        const MeshDrawContext& ctx,
        const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
        size_t recordCount,
        const RENDER3D::RUNTIME::SurfaceGpuSceneInstance* instances,
        size_t instanceCount) {

        if (ctx.surfaceGpuSceneFrameBuffer == nullptr ||
            records == nullptr ||
            instances == nullptr) {
            return false;
        }

        bool patchedAny = false;
        for (size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
            const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance =
                instances[instanceIndex];
            if (instance.sourceRecordIndex == RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex ||
                instance.sourceRecordIndex >= recordCount) {
                continue;
            }

            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record =
                records[instance.sourceRecordIndex];

            SurfaceRecordPreparedObject prepared{};
            if (!PrepareSurfaceRecordObjectData(ctx, record, prepared)) {
                continue;
            }

            // GPU-driven record path note.
            patchedAny =
                PatchSurfaceGpuSceneMaterialData(
                    ctx,
                    ctx.surfaceGpuSceneBaseOffset + instanceIndex,
                    prepared.materialDataIndex) ||
                patchedAny;
        }

        return patchedAny;
    }

    bool PrepareSurfaceGpuSceneMaterialSources(
        const MeshDrawContext& ctx,
        const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* sources,
        size_t sourceCount) {

        if (ctx.surfaceGpuSceneFrameBuffer == nullptr ||
            sources == nullptr) {
            return false;
        }

        bool patchedAny = false;
        for (size_t sourceIndex = 0; sourceIndex < sourceCount; ++sourceIndex) {
            SurfaceRecordPreparedObject prepared{};
            if (!PrepareGpuSceneMaterialSourceData(ctx, sources[sourceIndex], prepared)) {
                continue;
            }

            // GPU-driven record path note.
            patchedAny =
                PatchSurfaceGpuSceneMaterialData(
                    ctx,
                    ctx.surfaceGpuSceneBaseOffset + sourceIndex,
                    prepared.materialDataIndex) ||
                patchedAny;
        }

        return patchedAny;
    }

    bool PrepareSurfaceRecordIndirectDrawBindings(
        const MeshDrawContext& ctx,
        const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
        size_t recordCount,
        const uint32_t* executableRecordIndices,
        size_t executableRecordIndexCount,
        const RENDER3D::RUNTIME::SurfaceDrawCommand* commands,
        size_t commandCount,
        const std::vector<std::vector<MATH::Mat4>>* jointPalettes) {

        if (ctx.surfaceIndirectDrawBuffer == nullptr ||
            records == nullptr ||
            executableRecordIndices == nullptr ||
            commands == nullptr) {
            return false;
        }

        bool patchedAny = false;
        for (size_t commandIndex = 0; commandIndex < commandCount; ++commandIndex) {
            const RENDER3D::RUNTIME::SurfaceDrawCommand& command = commands[commandIndex];
            if (ctx.surfaceIndirectCommandFilter != nullptr &&
                !ctx.surfaceIndirectCommandFilter(command, ctx.surfaceIndirectCommandFilterUserData)) {
                continue;
            }
            if (!CanStartSurfaceIndirectCommandRange(ctx, command)) {
                continue;
            }

            size_t commandBegin = 0;
            size_t commandEnd = 0;
            if (!TryResolveSurfaceCommandRecordRange(
                command,
                executableRecordIndexCount,
                commandBegin,
                commandEnd)) {
                continue;
            }
            (void)commandEnd;

            const uint32_t firstRecordIndex = executableRecordIndices[commandBegin];
            if (firstRecordIndex >= recordCount) {
                continue;
            }

            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record =
                records[firstRecordIndex];
            const bool hasJointPalette =
                jointPalettes != nullptr &&
                command.firstRecordIndex != RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex &&
                command.firstRecordIndex < jointPalettes->size() &&
                !(*jointPalettes)[command.firstRecordIndex].empty();
            const bool skinnedCommand = record.skinned && hasJointPalette;

            Mesh* mesh = skinnedCommand
                ? ResolveSurfaceRecordSkinnedMesh(ctx, record)
                : ResolveSurfaceRecordStaticMesh(ctx, record);
            if (mesh == nullptr || !mesh->IsValid()) {
                continue;
            }

            if (skinnedCommand) {
                const size_t paletteSlot =
                    ctx.surfaceGpuSceneBaseOffset +
                    static_cast<size_t>(command.firstGpuSceneInstanceIndex);
                if (paletteSlot >= kMaxObjectCount || ctx.jointPaletteCB == nullptr) {
                    continue;
                }
                const std::vector<MATH::Mat4>& palette =
                    (*jointPalettes)[command.firstRecordIndex];
                const size_t uploadedJointCount =
                    UploadJointPalette(ctx.jointPaletteMapped, paletteSlot, palette);
                if (ctx.services.stats != nullptr) {
                    ctx.services.stats->uploadedJointCount += uploadedJointCount;
                    ctx.services.stats->maxJointCount =
                        std::max(ctx.services.stats->maxJointCount, palette.size());
                    const MeshPrimitive* primitive =
                        ResolveSurfaceRecordPrimitive(record);
                    ctx.services.stats->lastSkinnedVertexCount =
                        primitive != nullptr ? primitive->skinnedVertices.size() : 0u;
                }
                patchedAny =
                    ctx.surfaceIndirectDrawBuffer->PatchSkinnedDrawBinding(
                        command,
                        mesh->GetVBView(),
                        mesh->GetIBView(),
                        JointPaletteAddress(ctx, paletteSlot)) ||
                    patchedAny;
            } else {
                patchedAny =
                    ctx.surfaceIndirectDrawBuffer->PatchDrawBinding(
                        command,
                        mesh->GetVBView(),
                        mesh->GetIBView()) ||
                    patchedAny;
            }
        }

        return patchedAny;
    }

    SurfaceRecordCommandDrawResult DrawSurfaceRecordCommand(
        const MeshDrawContext& ctx,
        const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
        size_t recordCount,
        const uint32_t* executableRecordIndices,
        size_t executableRecordIndexCount,
        const RENDER3D::RUNTIME::SurfaceDrawCommand& command,
        size_t& objectIndex) {

        SurfaceRecordCommandDrawResult result{};
        if (records == nullptr ||
            executableRecordIndices == nullptr ||
            ctx.objectDataMapped == nullptr ||
            ctx.objectDataBuffer == nullptr ||
            ctx.objectDataSrv.ptr == 0 ||
            ctx.materialDataMapped == nullptr ||
            ctx.materialDataBuffer == nullptr ||
            ctx.materialDataSrv.ptr == 0 ||
            ctx.materialDataTable == nullptr ||
            command.recordCount == 0 ||
            command.firstExecutableIndex >= executableRecordIndexCount) {
            return result;
        }

        const size_t commandBegin = command.firstExecutableIndex;
        const size_t commandEnd = std::min(
            executableRecordIndexCount,
            commandBegin + static_cast<size_t>(command.recordCount));

        SurfaceRecordBatchState state{};
        size_t firstDrawableIndex = commandBegin;
        for (; firstDrawableIndex < commandEnd; ++firstDrawableIndex) {
            const uint32_t recordIndex = executableRecordIndices[firstDrawableIndex];
            if (recordIndex >= recordCount) {
                ++result.skippedRecordCount;
                continue;
            }
            if (PrepareSurfaceRecordBatch(ctx, records[recordIndex], command, state)) {
                break;
            }
            ++result.skippedRecordCount;
        }

        if (firstDrawableIndex >= commandEnd) {
            return result;
        }

        const Mesh* activeMesh = nullptr;
        if (firstDrawableIndex == commandBegin &&
            TryExecuteSurfaceRecordIndirectCommand(
                ctx,
                state,
                command,
                records,
                recordCount,
                executableRecordIndices,
                executableRecordIndexCount,
                activeMesh,
                result)) {
            return result;
        }

        for (size_t executableIndex = firstDrawableIndex; executableIndex < commandEnd; ++executableIndex) {
            const uint32_t recordIndex = executableRecordIndices[executableIndex];
            if (recordIndex >= recordCount) {
                ++result.skippedRecordCount;
                continue;
            }

            if (DrawSurfaceRecordInstanceBatch(
                ctx,
                state,
                command,
                records,
                recordCount,
                executableRecordIndices,
                executableRecordIndexCount,
                commandEnd,
                executableIndex,
                activeMesh,
                objectIndex,
                result)) {
                continue;
            }

            if (DrawPreparedSurfaceRecord(ctx, state, records[recordIndex], activeMesh, objectIndex)) {
                ++result.submittedRecordCount;
                ++result.drawCallCount;
                result.maxInstanceCount = (std::max)(result.maxInstanceCount, size_t{ 1 });
            } else {
                ++result.skippedRecordCount;
            }
        }

        return result;
    }

    SurfaceRecordCommandDrawResult DrawSurfaceRecordCommandRange(
        const MeshDrawContext& ctx,
        const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
        size_t recordCount,
        const uint32_t* executableRecordIndices,
        size_t executableRecordIndexCount,
        const RENDER3D::RUNTIME::SurfaceDrawCommand* commands,
        size_t commandCount,
        size_t& commandIndex,
        size_t& objectIndex) {

        SurfaceRecordCommandDrawResult result{};
        if (commands == nullptr || commandIndex >= commandCount) {
            return result;
        }

        const size_t currentCommandIndex = commandIndex;
        const Mesh* activeMesh = nullptr;
        size_t nextCommandIndex = currentCommandIndex;
        if (TryExecuteSurfaceRecordIndirectCommandRange(
            ctx,
            commands,
            commandCount,
            currentCommandIndex,
            records,
            recordCount,
            executableRecordIndices,
            executableRecordIndexCount,
            activeMesh,
            nextCommandIndex,
            result)) {
            commandIndex = nextCommandIndex;
            return result;
        }

        result = DrawSurfaceRecordCommand(
            ctx,
            records,
            recordCount,
            executableRecordIndices,
            executableRecordIndexCount,
            commands[currentCommandIndex],
            objectIndex);
        commandIndex = currentCommandIndex + 1;
        return result;
    }

    SurfaceRecordCommandDrawResult DrawSurfaceRecordIndirectCommandRange(
        const MeshDrawContext& ctx,
        const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
        size_t recordCount,
        const uint32_t* executableRecordIndices,
        size_t executableRecordIndexCount,
        const RENDER3D::RUNTIME::SurfaceDrawCommand* commands,
        size_t commandCount,
        size_t& commandIndex) {

        SurfaceRecordCommandDrawResult result{};
        if (commands == nullptr || commandIndex >= commandCount) {
            return result;
        }

        const size_t currentCommandIndex = commandIndex;
        const Mesh* activeMesh = nullptr;
        size_t nextCommandIndex = currentCommandIndex;
        if (TryExecuteSurfaceRecordIndirectCommandRange(
            ctx,
            commands,
            commandCount,
            currentCommandIndex,
            records,
            recordCount,
            executableRecordIndices,
            executableRecordIndexCount,
            activeMesh,
            nextCommandIndex,
            result)) {
            commandIndex = nextCommandIndex;
            return result;
        }

        commandIndex = currentCommandIndex + 1;
        return result;
    }

} // namespace HIKARI::MESHRENDERER
