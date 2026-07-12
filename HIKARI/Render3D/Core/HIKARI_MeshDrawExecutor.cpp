#include "Render3D/Core/HIKARI_MeshDrawExecutor.h"

#include <algorithm>
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
            indices.specular = ResolveTextureDescriptorIndex(textures.specular);
            indices.specularColor = ResolveTextureDescriptorIndex(textures.specularColor);
            return indices;
        }

        MATH::Vec4 ResolveSpecularParams(
            const MaterialAsset* materialAsset,
            const Material* runtimeMaterial) {

            MATH::Vec3 specularColor{ 1.0f, 1.0f, 1.0f };
            float specularFactor = 1.0f;
            if (materialAsset != nullptr) {
                specularColor = materialAsset->specularColorFactor;
                specularFactor = materialAsset->specularFactor;
            }
            if (runtimeMaterial != nullptr) {
                specularColor = runtimeMaterial->GetSpecularColorFactor();
                specularFactor = runtimeMaterial->GetSpecularFactor();
            }
            return {
                specularColor.x,
                specularColor.y,
                specularColor.z,
                specularFactor
            };
        }

        MATH::Vec4 DefaultUvTransform() {
            return { 1.0f, 1.0f, 0.0f, 0.0f };
        }

        MATH::Vec4 ToUvTransform(const TextureSlot& slot) {
            return {
                slot.uvScale.x,
                slot.uvScale.y,
                slot.uvOffset.x,
                slot.uvOffset.y
            };
        }

        MATH::Vec4 ToUvTransform(const RuntimeTextureSlot& slot) {
            return {
                slot.uvScale.x,
                slot.uvScale.y,
                slot.uvOffset.x,
                slot.uvOffset.y
            };
        }

        uint32_t ToUvSet(int texCoord) {
            return static_cast<uint32_t>(std::clamp(texCoord, 0, 1));
        }

        MATH::Vec4 ResolveSlotUvTransform(
            const MaterialAsset* materialAsset,
            const Material* runtimeMaterial,
            ModelTextureUsage usage) {

            if (runtimeMaterial != nullptr) {
                return ToUvTransform(runtimeMaterial->GetTextureSlot(usage));
            }
            if (materialAsset == nullptr) {
                return DefaultUvTransform();
            }
            switch (usage) {
            case ModelTextureUsage::BaseColor: return ToUvTransform(materialAsset->baseColorTexture);
            case ModelTextureUsage::Normal: return ToUvTransform(materialAsset->normalTexture);
            case ModelTextureUsage::MetallicRoughness: return ToUvTransform(materialAsset->metallicRoughnessTexture);
            case ModelTextureUsage::Occlusion: return ToUvTransform(materialAsset->occlusionTexture);
            case ModelTextureUsage::Emissive: return ToUvTransform(materialAsset->emissiveTexture);
            case ModelTextureUsage::Specular: return ToUvTransform(materialAsset->specularTexture);
            case ModelTextureUsage::SpecularColor: return ToUvTransform(materialAsset->specularColorTexture);
            default: return DefaultUvTransform();
            }
        }

        float ResolveSlotUvRotation(
            const MaterialAsset* materialAsset,
            const Material* runtimeMaterial,
            ModelTextureUsage usage) {

            if (runtimeMaterial != nullptr) {
                return runtimeMaterial->GetTextureSlot(usage).uvRotation;
            }
            if (materialAsset == nullptr) {
                return 0.0f;
            }
            switch (usage) {
            case ModelTextureUsage::BaseColor: return materialAsset->baseColorTexture.uvRotation;
            case ModelTextureUsage::Normal: return materialAsset->normalTexture.uvRotation;
            case ModelTextureUsage::MetallicRoughness: return materialAsset->metallicRoughnessTexture.uvRotation;
            case ModelTextureUsage::Occlusion: return materialAsset->occlusionTexture.uvRotation;
            case ModelTextureUsage::Emissive: return materialAsset->emissiveTexture.uvRotation;
            case ModelTextureUsage::Specular: return materialAsset->specularTexture.uvRotation;
            case ModelTextureUsage::SpecularColor: return materialAsset->specularColorTexture.uvRotation;
            default: return 0.0f;
            }
        }

        uint32_t ResolveSlotUvSet(
            const MaterialAsset* materialAsset,
            const Material* runtimeMaterial,
            ModelTextureUsage usage) {

            if (runtimeMaterial != nullptr) {
                return ToUvSet(runtimeMaterial->GetTextureSlot(usage).texCoord);
            }
            if (materialAsset == nullptr) {
                return 0u;
            }
            switch (usage) {
            case ModelTextureUsage::BaseColor: return ToUvSet(materialAsset->baseColorTexture.texCoord);
            case ModelTextureUsage::Normal: return ToUvSet(materialAsset->normalTexture.texCoord);
            case ModelTextureUsage::MetallicRoughness: return ToUvSet(materialAsset->metallicRoughnessTexture.texCoord);
            case ModelTextureUsage::Occlusion: return ToUvSet(materialAsset->occlusionTexture.texCoord);
            case ModelTextureUsage::Emissive: return ToUvSet(materialAsset->emissiveTexture.texCoord);
            case ModelTextureUsage::Specular: return ToUvSet(materialAsset->specularTexture.texCoord);
            case ModelTextureUsage::SpecularColor: return ToUvSet(materialAsset->specularColorTexture.texCoord);
            default: return 0u;
            }
        }

        bool HasSpecularTextureSlot(
            const MaterialAsset* materialAsset,
            const Material* runtimeMaterial,
            ModelTextureUsage usage) {

            if (runtimeMaterial != nullptr) {
                return runtimeMaterial->HasTextureSlot(usage);
            }
            if (materialAsset == nullptr) {
                return false;
            }
            switch (usage) {
            case ModelTextureUsage::Specular:
                return materialAsset->specularTexture.textureIndex >= 0;
            case ModelTextureUsage::SpecularColor:
                return materialAsset->specularColorTexture.textureIndex >= 0;
            default:
                return false;
            }
        }

        MaterialGpuData BuildMaterialGpuData(
            const ObjectCB& obj,
            const MaterialTextureHandles& textures,
            const MaterialAsset* materialAsset,
            const Material* runtimeMaterial) {

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
            data.specularParams = ResolveSpecularParams(materialAsset, runtimeMaterial);
            data.materialFlags = obj.materialFlags;
            data.hasBaseColorTexture = obj.hasBaseColorTexture;
            data.hasNormalTexture = obj.hasNormalTexture;
            data.hasEmissiveTexture = obj.hasEmissiveTexture;
            data.hasMetallicRoughnessTexture = obj.hasMetallicRoughnessTexture;
            data.hasOcclusionTexture = obj.hasOcclusionTexture;
            data.hasSpecularTexture =
                HasSpecularTextureSlot(materialAsset, runtimeMaterial, ModelTextureUsage::Specular) ? 1u : 0u;
            data.hasSpecularColorTexture =
                HasSpecularTextureSlot(materialAsset, runtimeMaterial, ModelTextureUsage::SpecularColor) ? 1u : 0u;
            data.normalScale = obj.normalScale;
            data.baseColorTextureHandle = textures.baseColor;
            data.normalTextureHandle = textures.normal;
            data.emissiveTextureHandle = textures.emissive;
            data.metallicRoughnessTextureHandle = textures.metallicRoughness;
            data.occlusionTextureHandle = textures.occlusion;
            data.specularTextureHandle = textures.specular;
            data.specularColorTextureHandle = textures.specularColor;
            data.baseColorTextureDescriptorIndex = textureIndices.baseColor;
            data.normalTextureDescriptorIndex = textureIndices.normal;
            data.emissiveTextureDescriptorIndex = textureIndices.emissive;
            data.metallicRoughnessTextureDescriptorIndex = textureIndices.metallicRoughness;
            data.occlusionTextureDescriptorIndex = textureIndices.occlusion;
            data.specularTextureDescriptorIndex = textureIndices.specular;
            data.specularColorTextureDescriptorIndex = textureIndices.specularColor;
            data.baseColorUvTransform = ResolveSlotUvTransform(materialAsset, runtimeMaterial, ModelTextureUsage::BaseColor);
            data.normalUvTransform = ResolveSlotUvTransform(materialAsset, runtimeMaterial, ModelTextureUsage::Normal);
            data.emissiveUvTransform = ResolveSlotUvTransform(materialAsset, runtimeMaterial, ModelTextureUsage::Emissive);
            data.metallicRoughnessUvTransform = ResolveSlotUvTransform(materialAsset, runtimeMaterial, ModelTextureUsage::MetallicRoughness);
            data.occlusionUvTransform = ResolveSlotUvTransform(materialAsset, runtimeMaterial, ModelTextureUsage::Occlusion);
            data.specularUvTransform = ResolveSlotUvTransform(materialAsset, runtimeMaterial, ModelTextureUsage::Specular);
            data.specularColorUvTransform = ResolveSlotUvTransform(materialAsset, runtimeMaterial, ModelTextureUsage::SpecularColor);
            data.uvRotation0 = {
                ResolveSlotUvRotation(materialAsset, runtimeMaterial, ModelTextureUsage::BaseColor),
                ResolveSlotUvRotation(materialAsset, runtimeMaterial, ModelTextureUsage::Normal),
                ResolveSlotUvRotation(materialAsset, runtimeMaterial, ModelTextureUsage::Emissive),
                ResolveSlotUvRotation(materialAsset, runtimeMaterial, ModelTextureUsage::MetallicRoughness)
            };
            data.uvRotation1 = {
                ResolveSlotUvRotation(materialAsset, runtimeMaterial, ModelTextureUsage::Occlusion),
                ResolveSlotUvRotation(materialAsset, runtimeMaterial, ModelTextureUsage::Specular),
                ResolveSlotUvRotation(materialAsset, runtimeMaterial, ModelTextureUsage::SpecularColor),
                0.0f
            };
            data.uvSet0[0] = ResolveSlotUvSet(materialAsset, runtimeMaterial, ModelTextureUsage::BaseColor);
            data.uvSet0[1] = ResolveSlotUvSet(materialAsset, runtimeMaterial, ModelTextureUsage::Normal);
            data.uvSet0[2] = ResolveSlotUvSet(materialAsset, runtimeMaterial, ModelTextureUsage::Emissive);
            data.uvSet0[3] = ResolveSlotUvSet(materialAsset, runtimeMaterial, ModelTextureUsage::MetallicRoughness);
            data.uvSet1[0] = ResolveSlotUvSet(materialAsset, runtimeMaterial, ModelTextureUsage::Occlusion);
            data.uvSet1[1] = ResolveSlotUvSet(materialAsset, runtimeMaterial, ModelTextureUsage::Specular);
            data.uvSet1[2] = ResolveSlotUvSet(materialAsset, runtimeMaterial, ModelTextureUsage::SpecularColor);
            return data;
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
                data.specularTextureDescriptorIndex,
                data.specularColorTextureDescriptorIndex,
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
            textureHandles.specular = binding.fallbackTextureHandle;
            textureHandles.specularColor = binding.fallbackTextureHandle;

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
            if (material->HasTextureSlot(ModelTextureUsage::Specular)) {
                textureHandles.specular = material->GetTextureSlot(ModelTextureUsage::Specular).handle;
            }
            if (material->HasTextureSlot(ModelTextureUsage::SpecularColor)) {
                textureHandles.specularColor = material->GetTextureSlot(ModelTextureUsage::SpecularColor).handle;
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
                textures.specular = handles.specular;
                textures.specularColor = handles.specularColor;
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
            textures.specular = ctx.binding.fallbackTextureHandle;
            textures.specularColor = ctx.binding.fallbackTextureHandle;
            return textures;
        }

        MaterialTextureHandles ToMaterialTextureHandles(const ResolvedMaterialTextures& textures) {
            return {
                textures.baseColor,
                textures.normal,
                textures.emissive,
                textures.metallicRoughness,
                textures.occlusion,
                textures.specular,
                textures.specularColor
            };
        }

        void BindSurfaceRecordFrameResourcesInternal(const MeshDrawContext& ctx) {

            BindFrameCommonResources(
                ctx.binding,
                ctx.staticRootSig,
                ctx.cameraAddress,
                ctx.cullingCameraAddress,
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

        bool BuildGpuSceneMaterialSourceData(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source,
            MaterialGpuData& outData,
            bool& outFinalized) {

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
            outData = BuildMaterialGpuData(
                obj,
                textureHandles,
                materialAsset,
                source.materialOverride);
            RecordMaterialTexturePoolStats(ctx, outData);
            outFinalized = textures.complete;
            return true;
        }

    } // namespace

    void BindSurfaceRecordFrameResources(const MeshDrawContext& ctx) {
        // SurfaceRecord は frame 共通リソースめEplan 単位で束縛する、E
        BindSurfaceRecordFrameResourcesInternal(ctx);
    }

    bool PrepareSurfaceGpuSceneMaterialSources(
        const MeshDrawContext& ctx,
        const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* sources,
        size_t sourceCount) {

        if (ctx.gpuMaterialRegistry == nullptr || sources == nullptr) {
            return false;
        }

        bool resolvedAny = false;
        for (size_t sourceIndex = 0; sourceIndex < sourceCount; ++sourceIndex) {
            const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source =
                sources[sourceIndex];
            const bool hasSourceRecord =
                source.sourceRecordIndex !=
                RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex;
            if (!hasSourceRecord) {
                continue;
            }
            const RENDER3D::MATERIAL::GpuMaterialSourceKey sourceKey{
                source.materialResource,
                source.materialKey,
                reinterpret_cast<uintptr_t>(source.model),
                reinterpret_cast<uintptr_t>(source.materialOverride),
                source.materialRevision,
                source.materialIndex
            };

            uint32_t materialSlot = kInvalidMaterialDataIndex;
            if (ctx.gpuMaterialRegistry->TryReuseSourceBinding(
                    source.sourceRecordIndex,
                    sourceKey,
                    materialSlot)) {
                resolvedAny = true;
                continue;
            }

            MaterialGpuData materialData{};
            bool finalized = true;
            if (!BuildGpuSceneMaterialSourceData(
                    ctx,
                    source,
                    materialData,
                    finalized)) {
                continue;
            }

            materialSlot =
                ctx.gpuMaterialRegistry->ResolveAndBindSource(
                    source.sourceRecordIndex,
                    sourceKey,
                    materialData,
                    finalized);
            resolvedAny =
                materialSlot != kInvalidMaterialDataIndex || resolvedAny;
        }

        return resolvedAny;
    }

} // namespace HIKARI::MESHRENDERER
