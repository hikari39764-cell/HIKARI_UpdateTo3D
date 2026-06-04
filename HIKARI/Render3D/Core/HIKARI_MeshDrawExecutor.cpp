#include "Render3D/Core/HIKARI_MeshDrawExecutor.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "HIKARI_DxTexture.h"
#include "Render3D/HIKARI_Mesh.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshMaterialResolver.h"
#include "Render3D/Core/HIKARI_MeshPrimitiveCache.h"
#include "Render3D/Core/HIKARI_MeshRendererPso.h"
#include "Render3D/Core/HIKARI_MeshRendererRootParams.h"
#include "Render3D/Core/HIKARI_MeshRendererUpload.h"
#include "Render3D/Core/HIKARI_MeshVariantResolver.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPacket.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::MESHRENDERER {

    namespace {
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
        }

        ObjectGpuData BuildObjectGpuData(const ObjectCB& obj) {
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
            data.pbrPadding[0] = obj.pbrPadding[0];
            data.pbrPadding[1] = obj.pbrPadding[1];
            data.pbrPadding[2] = obj.pbrPadding[2];
            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                data.fxUser[i] = obj.fxUser[i];
            }
            return data;
        }

        void CopyObjectData(const MeshDrawContext& ctx, const ObjectCB& obj, size_t objectIndex) {
            if (ctx.objectDataMapped == nullptr || objectIndex >= kMaxObjectCount) {
                return;
            }
            ctx.objectDataMapped[objectIndex] = BuildObjectGpuData(obj);
            if (ctx.services.stats != nullptr) {
                ++ctx.services.stats->objectDataWriteCount;
            }
        }

        uint32_t ResolveTextureDescriptorIndex(int textureHandle) {
            const UINT descriptorIndex =
                DXTEX::DxTextureManager::GetSrvDescriptorIndex(textureHandle);
            return descriptorIndex == DXTEX::kInvalidSrvDescriptorIndex
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
            if (bindLegacyObjectCB) {
                BindObjectConstantBuffer(ctx.binding, objectAddress);
            }
        }

        bool IsObjectDataVertexShader(const std::string& vertexShaderId) {
            return vertexShaderId.empty() || vertexShaderId == "Render3D_StaticVS";
        }

        bool IsObjectDataPixelShader(const std::string& shaderId, const std::string& pixelShaderId) {
            const std::string& id = !pixelShaderId.empty() ? pixelShaderId : shaderId;
            return id.empty() ||
                id == "PBR" ||
                id == "StaticLit" ||
                id == "StaticFx" ||
                id == "MaterialFx";
        }

        bool StaticDrawCanSkipLegacyObjectCB(
            MeshDrawPassKind passKind,
            const VFX::VariantKey& variant) {
            if (passKind == MeshDrawPassKind::GeometryBuffer) {
                return IsObjectDataVertexShader(variant.vertexShaderId);
            }
            return
                IsObjectDataVertexShader(variant.vertexShaderId) &&
                IsObjectDataPixelShader(variant.shaderId, variant.pixelShaderId);
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

        struct SurfacePacketRunState {
            const ModelAsset* model = nullptr;
            const MaterialAsset* materialAsset = nullptr;
            const Material* runtimeMaterial = nullptr;
            ResolvedMaterialTextures textures{};
            VFX::VariantKey variant{};
            std::array<MATH::Vec4, VFX::kMaterialFxUserCount> defaultFxValues{};
            uint32_t fxFlags = 0;
            uint64_t psoKey = 0;
            uint64_t materialKey = 0;
            uint64_t textureSetKey = 0;
            MaterialGpuData materialData{};
            uint32_t materialDataIndex = kInvalidMaterialDataIndex;
            bool needsLegacyObjectCB = true;
        };

        Transform3D BuildPacketDrawTransform(const RENDER3D::RUNTIME::SurfaceDrawPacket& packet) {
            Transform3D transform = packet.objectWorldTransform;
            transform.useExplicitMatrix = true;
            transform.explicitMatrix = packet.drawWorldMatrix;
            return transform;
        }

        DrawItem BuildRunVariantAdapter(const RENDER3D::RUNTIME::SurfaceDrawPacket& packet) {
            DrawItem item{};
            item.asset = packet.model;
            item.materialOverride = packet.materialOverride;
            item.materialFxProfileId = packet.materialFxProfileId;
            item.postGroupMask = packet.postGroupMask;
            // run の既定値だけを解決し、個別 override は packet 側で反映する。
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

        void BindRunStaticResources(
            const MeshDrawContext& ctx,
            const SurfacePacketRunState& state) {

            BindMaterialTextureSet(ctx.binding, ToMaterialTextureHandles(state.textures));
            BindSkyCube(ctx.binding);
            BindSceneDepth(ctx.binding);
            BindSceneColor(ctx.binding);
            BindIblResources(ctx.binding);
            BindReflectionProbeResources(ctx.binding);
            BindSsao(ctx.binding);
            BindLightProbeResources(ctx.binding);
        }

        bool PrepareSurfacePacketRun(
            const MeshDrawContext& ctx,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& firstPacket,
            SurfacePacketRunState& outState) {

            if (ctx.cmd == nullptr ||
                ctx.services.device == nullptr ||
                ctx.services.pipelines == nullptr ||
                ctx.services.stats == nullptr ||
                firstPacket.model == nullptr ||
                firstPacket.meshIndex >= firstPacket.model->meshes.size()) {
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
            outState.textures = ResolvePacketTextures(ctx, firstPacket, outState.materialAsset);
            outState.psoKey = firstPacket.key.psoKey;
            outState.materialKey = firstPacket.key.materialKey;
            outState.textureSetKey = firstPacket.key.textureSetKey;

            DrawItem variantItem = BuildRunVariantAdapter(firstPacket);
            outState.defaultFxValues = variantItem.fxValues;
            outState.fxFlags = variantItem.fxFlags;
            outState.variant = ResolvePrimitiveVariant(
                variantItem,
                outState.runtimeMaterial != nullptr ? nullptr : outState.materialAsset);
            outState.needsLegacyObjectCB =
                !StaticDrawCanSkipLegacyObjectCB(ctx.passKind, outState.variant);

            ObjectCB materialObj{};
            FillMaterialValues(
                materialObj,
                outState.materialAsset,
                outState.textures.normal,
                outState.textures.emissive,
                outState.textures.metallicRoughness,
                outState.textures.occlusion,
                ctx.materialFill);
            if (outState.runtimeMaterial != nullptr) {
                FillRuntimeMaterialValues(materialObj, *outState.runtimeMaterial);
            }
            materialObj.hasBaseColorTexture =
                (outState.textures.baseColor >= 0 && outState.textures.baseColor != ctx.binding.fallbackTextureHandle) ? 1u : 0u;
            outState.materialData = BuildMaterialGpuData(
                materialObj,
                ToMaterialTextureHandles(outState.textures));
            RecordMaterialTexturePoolStats(ctx, outState.materialData);
            outState.materialDataIndex = UploadMaterialData(
                ctx,
                BuildMaterialDataKey(firstPacket.key.materialKey, outState.materialData),
                outState.materialData);

            BindFrameCommonResources(
                ctx.binding,
                ctx.staticRootSig,
                ctx.cameraAddress,
                ctx.lightAddress,
                ctx.shadowAddress,
                ctx.skyEnvironmentAddress);
            BindObjectDataBuffer(ctx.binding, ctx.objectDataSrv);
            BindMaterialDataBuffer(ctx.binding, ctx.materialDataSrv);
            BindMaterialDataIndex(
                ctx.binding,
                outState.materialDataIndex == kInvalidMaterialDataIndex ? 0u : outState.materialDataIndex);
            BindRunStaticResources(ctx, outState);

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

        bool IsRunCompatiblePacket(
            const SurfacePacketRunState& state,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& packet) {

            return
                packet.model == state.model &&
                packet.key.psoKey == state.psoKey &&
                packet.key.materialKey == state.materialKey &&
                packet.key.textureSetKey == state.textureSetKey;
        }

        void FillPacketFxValues(
            ObjectCB& obj,
            const SurfacePacketRunState& state,
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

        bool DrawPreparedSurfacePacket(
            const MeshDrawContext& ctx,
            const SurfacePacketRunState& state,
            const RENDER3D::RUNTIME::SurfaceDrawPacket& packet,
            const Mesh*& activeMesh,
            size_t& objectIndex) {

            if (objectIndex >= kMaxObjectCount ||
                !packet.hasDrawWorldMatrix ||
                packet.model == nullptr ||
                packet.meshIndex >= packet.model->meshes.size() ||
                !IsRunCompatiblePacket(state, packet)) {
                return false;
            }

            const MeshAsset& meshAsset = packet.model->meshes[packet.meshIndex];
            if (packet.primitiveIndex >= meshAsset.primitives.size()) {
                return false;
            }

            const MeshPrimitive& primitive = meshAsset.primitives[packet.primitiveIndex];
            MeshPrimitiveCache* primitiveCache = ctx.services.primitiveCache;
            Mesh* mesh = primitiveCache != nullptr
                ? primitiveCache->GetOrCreateStatic(ctx.services.device, primitive, ctx.services.stats)
                : nullptr;
            if (mesh == nullptr || !mesh->IsValid()) {
                return false;
            }

            const Transform3D drawTransform = BuildPacketDrawTransform(packet);
            ObjectCB obj{};
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
            CopyObjectCB(ctx, obj, objectIndex);
            CopyObjectData(ctx, obj, objectIndex);

            BindObjectDataIndex(ctx.binding, static_cast<uint32_t>(objectIndex));
            BindMaterialDataIndex(
                ctx.binding,
                state.materialDataIndex == kInvalidMaterialDataIndex ? 0u : state.materialDataIndex);
            if (state.needsLegacyObjectCB) {
                BindObjectConstantBuffer(ctx.binding, ObjectAddress(ctx, objectIndex));
            }

            if (activeMesh != mesh) {
                D3D12_VERTEX_BUFFER_VIEW vb = mesh->GetVBView();
                D3D12_INDEX_BUFFER_VIEW ib = mesh->GetIBView();
                ctx.cmd->IASetVertexBuffers(0, 1, &vb);
                ctx.cmd->IASetIndexBuffer(&ib);
                activeMesh = mesh;
            }

            ctx.cmd->DrawIndexedInstanced(mesh->GetIndexCount(), 1, 0, 0, 0);
            ++objectIndex;
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
                    CopyObjectCB(ctx, obj, objectIndex);
                    CopyObjectData(ctx, obj, objectIndex);
                    const MaterialTextureHandles materialTextureHandles = ToMaterialTextureHandles(textures);
                    const MaterialGpuData materialData = BuildMaterialGpuData(obj, materialTextureHandles);
                    RecordMaterialTexturePoolStats(ctx, materialData);
                    const uint32_t materialDataIndex = UploadMaterialData(
                        ctx,
                        BuildMaterialDataKey(0u, materialData),
                        materialData);

                    const VFX::VariantKey primitiveVariant = ResolvePrimitiveVariant(
                        item,
                        runtimeMaterial ? nullptr : materialAsset);

                    const D3D12_GPU_VIRTUAL_ADDRESS objectAddress = ObjectAddress(ctx, objectIndex);
                    const bool bindLegacyObjectCB = drawingSkinned ||
                        !StaticDrawCanSkipLegacyObjectCB(ctx.passKind, primitiveVariant);
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

                    BindMaterialTextureSet(ctx.binding, materialTextureHandles);
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
            CopyObjectCB(ctx, obj, objectIndex);
            CopyObjectData(ctx, obj, objectIndex);

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

            const D3D12_GPU_VIRTUAL_ADDRESS objectAddress = ObjectAddress(ctx, objectIndex);
            const bool bindLegacyObjectCB =
                !StaticDrawCanSkipLegacyObjectCB(ctx.passKind, item.variant);
            BindPerDrawCommon(
                ctx,
                ctx.staticRootSig,
                objectAddress,
                static_cast<uint32_t>(objectIndex),
                materialDataIndex,
                bindLegacyObjectCB);

            // Legacy mesh でも runtime Material の PBR slot を同じ root table へ流す。
            BindMaterialTextureSet(ctx.binding, textureHandles);
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

    SurfacePacketRunDrawResult DrawSurfacePacketRun(
        const MeshDrawContext& ctx,
        const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
        size_t packetCount,
        const uint32_t* executablePacketIndices,
        size_t executablePacketIndexCount,
        const RENDER3D::RUNTIME::SurfaceDrawPacketRun& run,
        size_t& objectIndex) {

        SurfacePacketRunDrawResult result{};
        if (packets == nullptr ||
            executablePacketIndices == nullptr ||
            ctx.objectDataMapped == nullptr ||
            ctx.objectDataBuffer == nullptr ||
            ctx.objectDataSrv.ptr == 0 ||
            ctx.materialDataMapped == nullptr ||
            ctx.materialDataBuffer == nullptr ||
            ctx.materialDataSrv.ptr == 0 ||
            ctx.materialDataTable == nullptr ||
            run.packetCount == 0 ||
            run.firstExecutableIndex >= executablePacketIndexCount) {
            return result;
        }

        const size_t runBegin = run.firstExecutableIndex;
        const size_t runEnd = std::min(
            executablePacketIndexCount,
            runBegin + static_cast<size_t>(run.packetCount));

        SurfacePacketRunState state{};
        size_t firstDrawableIndex = runBegin;
        for (; firstDrawableIndex < runEnd; ++firstDrawableIndex) {
            const uint32_t packetIndex = executablePacketIndices[firstDrawableIndex];
            if (packetIndex >= packetCount) {
                ++result.skippedPacketCount;
                continue;
            }
            if (PrepareSurfacePacketRun(ctx, packets[packetIndex], state)) {
                break;
            }
            ++result.skippedPacketCount;
        }

        if (firstDrawableIndex >= runEnd) {
            return result;
        }

        const Mesh* activeMesh = nullptr;
        for (size_t executableIndex = firstDrawableIndex; executableIndex < runEnd; ++executableIndex) {
            const uint32_t packetIndex = executablePacketIndices[executableIndex];
            if (packetIndex >= packetCount) {
                ++result.skippedPacketCount;
                continue;
            }

            if (DrawPreparedSurfacePacket(ctx, state, packets[packetIndex], activeMesh, objectIndex)) {
                ++result.submittedPacketCount;
            } else {
                ++result.skippedPacketCount;
            }
        }

        return result;
    }

} // namespace HIKARI::MESHRENDERER
