#include "Render3D/Core/HIKARI_MeshDrawExecutor.h"

#include <array>
#include <string>

#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshMaterialResolver.h"
#include "Render3D/Core/HIKARI_MeshRendererRootParams.h"
#include "Render3D/Core/HIKARI_MeshRendererUpload.h"
#include "Render3D/Core/HIKARI_MeshVariantResolver.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
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

        bool IsBatchCompatibleRecord(
            const SurfaceRecordBatchState& state,
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record) {

            return RENDER3D::RUNTIME::IsSameSurfaceDrawBatchKey(
                RENDER3D::RUNTIME::BuildSurfaceDrawBatchKey(state.batchKey.pass, record.key),
                state.batchKey);
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

    } // namespace

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

} // namespace HIKARI::MESHRENDERER
