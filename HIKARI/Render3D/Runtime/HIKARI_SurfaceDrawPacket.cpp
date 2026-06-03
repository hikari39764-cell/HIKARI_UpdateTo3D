#include "Render3D/Runtime/HIKARI_SurfaceDrawPacket.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Runtime/HIKARI_RenderSurfaceResolver.h"

namespace HIKARI::RENDER3D::RUNTIME {

    namespace {
        uint32_t ClampToUint32(size_t value) {
            return static_cast<uint32_t>(
                (std::min)(value, static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
        }

        uint32_t BuildPassMask(const SurfaceDrawPacket& packet) {
            uint32_t mask = 0;
            if (packet.forwardCandidate) {
                mask |= static_cast<uint32_t>(SurfaceDrawPacketPassFlags::Forward);
            }
            if (packet.shadowCandidate) {
                mask |= static_cast<uint32_t>(SurfaceDrawPacketPassFlags::Shadow);
            }
            return mask;
        }

        uint64_t HashAppend(uint64_t seed, uint64_t value) {
            constexpr uint64_t kMul = 1099511628211ull;
            seed ^= value;
            seed *= kMul;
            return seed;
        }

        uint64_t HashString(std::string_view value) {
            uint64_t hash = 1469598103934665603ull;
            for (const char c : value) {
                hash = HashAppend(hash, static_cast<uint64_t>(static_cast<unsigned char>(c)));
            }
            return hash;
        }

        uint64_t HashPointer(const void* value) {
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
        }

        uint64_t HashIntSlot(int value) {
            return static_cast<uint64_t>(static_cast<int64_t>(value) + 0x100000000ll);
        }

        uint64_t BuildModelKey(const ModelAsset* model) {
            if (model == nullptr) {
                return 0;
            }
            if (!model->id.value.empty()) {
                return HashString(model->id.value);
            }
            if (!model->sourcePath.empty()) {
                return HashString(model->sourcePath);
            }
            return HashPointer(model);
        }

        const MaterialAsset* ResolveMaterialAsset(const SurfaceDrawPacket& packet) {
            if (packet.model == nullptr || packet.materialIndex >= packet.model->materials.size()) {
                return nullptr;
            }
            return &packet.model->materials[packet.materialIndex];
        }

        int ResolveRuntimeTextureSlot(const Material& material, ModelTextureUsage usage) {
            return material.HasTextureSlot(usage) ? material.GetTextureSlot(usage).handle : -1;
        }

        uint64_t BuildTextureSetKey(const SurfaceDrawPacket& packet, const MaterialAsset* materialAsset) {
            uint64_t hash = HashString("texture-set");
            if (packet.materialOverride != nullptr) {
                hash = HashAppend(hash, HashIntSlot(packet.materialOverride->GetBaseColorTextureHandle()));
                hash = HashAppend(hash, HashIntSlot(ResolveRuntimeTextureSlot(*packet.materialOverride, ModelTextureUsage::Normal)));
                hash = HashAppend(hash, HashIntSlot(ResolveRuntimeTextureSlot(*packet.materialOverride, ModelTextureUsage::MetallicRoughness)));
                hash = HashAppend(hash, HashIntSlot(ResolveRuntimeTextureSlot(*packet.materialOverride, ModelTextureUsage::Occlusion)));
                hash = HashAppend(hash, HashIntSlot(ResolveRuntimeTextureSlot(*packet.materialOverride, ModelTextureUsage::Emissive)));
                return hash;
            }
            if (materialAsset == nullptr) {
                return HashAppend(hash, HashIntSlot(-1));
            }

            hash = HashAppend(hash, HashIntSlot(materialAsset->baseColorTexture.textureIndex));
            hash = HashAppend(hash, HashIntSlot(materialAsset->normalTexture.textureIndex));
            hash = HashAppend(hash, HashIntSlot(materialAsset->metallicRoughnessTexture.textureIndex));
            hash = HashAppend(hash, HashIntSlot(materialAsset->occlusionTexture.textureIndex));
            hash = HashAppend(hash, HashIntSlot(materialAsset->emissiveTexture.textureIndex));
            return hash;
        }

        SurfaceDrawPacketKey BuildPacketKey(const SurfaceDrawPacket& packet) {
            SurfaceDrawPacketKey key{};
            key.materialIndex = packet.materialIndex;
            key.meshIndex = packet.meshIndex;
            key.primitiveIndex = packet.primitiveIndex;
            key.passMask = BuildPassMask(packet);
            key.hasMaterialOverride = packet.materialOverride != nullptr;
            key.skinned = packet.skinned;

            if (packet.model == nullptr || packet.surface == nullptr || !packet.surface->HasMeshPrimitive()) {
                return key;
            }

            const MaterialAsset* materialAsset = ResolveMaterialAsset(packet);
            const uint64_t modelKey = BuildModelKey(packet.model);

            std::string_view shaderProfile = "PBR";
            uint32_t featureBits = 0;
            bool doubleSided = false;
            AlphaMode alphaMode = AlphaMode::Opaque;
            if (packet.materialOverride != nullptr) {
                shaderProfile = packet.materialOverride->GetShaderProfileId();
                if (shaderProfile.empty()) {
                    shaderProfile = "PBR";
                }
                featureBits = packet.materialOverride->GetFeatureBits();
            } else if (materialAsset != nullptr) {
                shaderProfile = materialAsset->shaderProfileId.empty() ? std::string_view("PBR") : std::string_view(materialAsset->shaderProfileId);
                featureBits = materialAsset->featureBits;
                doubleSided = materialAsset->doubleSided;
                alphaMode = materialAsset->alphaMode;
            }

            key.modelKey = modelKey;
            key.geometryKey = HashString("geometry");
            key.geometryKey = HashAppend(key.geometryKey, modelKey);
            key.geometryKey = HashAppend(key.geometryKey, packet.meshIndex);
            key.geometryKey = HashAppend(key.geometryKey, packet.primitiveIndex);
            key.geometryKey = HashAppend(key.geometryKey, packet.skinned ? 1u : 0u);

            key.textureSetKey = BuildTextureSetKey(packet, materialAsset);

            key.shaderKey = HashString(shaderProfile);
            key.shaderKey = HashAppend(key.shaderKey, HashString(packet.materialFxProfileId));

            key.materialKey = HashString(packet.materialOverride != nullptr ? "runtime-material" : "asset-material");
            key.materialKey = HashAppend(key.materialKey, modelKey);
            key.materialKey = HashAppend(key.materialKey, packet.materialOverride != nullptr ? HashPointer(packet.materialOverride) : packet.materialIndex);
            key.materialKey = HashAppend(key.materialKey, key.textureSetKey);
            key.materialKey = HashAppend(key.materialKey, key.shaderKey);
            key.materialKey = HashAppend(key.materialKey, featureBits);
            key.materialKey = HashAppend(key.materialKey, static_cast<uint64_t>(alphaMode));
            key.materialKey = HashAppend(key.materialKey, doubleSided ? 1u : 0u);

            key.psoKey = HashString("pso");
            key.psoKey = HashAppend(key.psoKey, key.shaderKey);
            key.psoKey = HashAppend(key.psoKey, featureBits);
            key.psoKey = HashAppend(key.psoKey, packet.skinned ? 1u : 0u);
            key.psoKey = HashAppend(key.psoKey, static_cast<uint64_t>(alphaMode));
            key.psoKey = HashAppend(key.psoKey, doubleSided ? 1u : 0u);

            key.sortKey = HashString("sort");
            key.sortKey = HashAppend(key.sortKey, key.passMask);
            key.sortKey = HashAppend(key.sortKey, key.psoKey);
            key.sortKey = HashAppend(key.sortKey, key.materialKey);
            key.sortKey = HashAppend(key.sortKey, key.textureSetKey);
            key.sortKey = HashAppend(key.sortKey, key.geometryKey);

            key.alphaMasked = alphaMode == AlphaMode::Mask;
            key.transparent = alphaMode == AlphaMode::Blend;
            key.resourceKeyValid = modelKey != 0 && key.geometryKey != 0 && key.materialKey != 0;
            return key;
        }

        bool ComesBeforeForBatching(const SurfaceDrawPacketKey& lhs, const SurfaceDrawPacketKey& rhs) {
            if (lhs.passMask != rhs.passMask) {
                return lhs.passMask < rhs.passMask;
            }
            if (lhs.transparent != rhs.transparent) {
                return !lhs.transparent && rhs.transparent;
            }
            if (lhs.psoKey != rhs.psoKey) {
                return lhs.psoKey < rhs.psoKey;
            }
            if (lhs.materialKey != rhs.materialKey) {
                return lhs.materialKey < rhs.materialKey;
            }
            if (lhs.textureSetKey != rhs.textureSetKey) {
                return lhs.textureSetKey < rhs.textureSetKey;
            }
            if (lhs.geometryKey != rhs.geometryKey) {
                return lhs.geometryKey < rhs.geometryKey;
            }
            return lhs.sortKey < rhs.sortKey;
        }

        void CopyMaterialFxValues(const SceneSurfaceInstance& source, SurfaceDrawPacket& packet) {
            for (int i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                packet.materialFxParamValues[i] = source.materialFxParamValues[i];
            }
        }
    }

    void SurfaceDrawPacketBuilder::Clear() {
        packets_.clear();
        stats_ = {};
    }

    void SurfaceDrawPacketBuilder::BuildFromSceneRenderCache(const SceneRenderCache& sceneCache) {
        packets_.clear();
        packets_.reserve(sceneCache.GetSurfaceInstances().size());

        uint32_t sourceSurfaceInstanceIndex = 0;
        for (const SceneSurfaceInstance& surfaceInstance : sceneCache.GetSurfaceInstances()) {
            AppendPacket(surfaceInstance, sourceSurfaceInstanceIndex);
            ++sourceSurfaceInstanceIndex;
        }

        RefreshStats();
    }

    const std::vector<SurfaceDrawPacket>& SurfaceDrawPacketBuilder::GetPackets() const {
        return packets_;
    }

    const SurfaceDrawPacketBuilder::Stats& SurfaceDrawPacketBuilder::GetStats() const {
        return stats_;
    }

    SurfaceDrawPacketValidationResult SurfaceDrawPacketBuilder::ValidatePacket(const SurfaceDrawPacket& packet) {
        SurfaceDrawPacketValidationResult result{};
        if (packet.sourceSurface == nullptr || !packet.sourceSurface->valid || !packet.objectId.IsValid()) {
            result.invalidSource = true;
        }
        if (packet.model == nullptr || packet.renderModel == nullptr || !packet.renderModel->valid) {
            result.invalidModel = true;
        }
        if (packet.surface == nullptr || !packet.surface->HasMeshPrimitive()) {
            result.unsupportedGeometry = true;
        }
        if (!packet.hasDrawWorldMatrix) {
            result.missingDrawMatrix = true;
        }
        if (!BOUNDS::IsUsable(packet.worldBounds)) {
            result.invalidBounds = true;
        }
        if (!HasValidRenderSurfacePrimitive(packet.model, packet.surface != nullptr ? *packet.surface : RenderSurfaceRecord{})) {
            result.invalidPrimitiveIndex = true;
        }
        return result;
    }

    void SurfaceDrawPacketBuilder::AppendPacket(
        const SceneSurfaceInstance& surfaceInstance,
        uint32_t sourceSurfaceInstanceIndex) {

        SurfaceDrawPacket packet{};
        packet.objectId = surfaceInstance.objectId;
        packet.objectVersion = surfaceInstance.objectVersion;
        packet.sourceSurfaceInstanceIndex = sourceSurfaceInstanceIndex;
        packet.sourceSurface = &surfaceInstance;
        packet.model = surfaceInstance.model;
        packet.renderModel = surfaceInstance.renderModel;
        packet.surface = surfaceInstance.surface;
        packet.surfaceIndex = surfaceInstance.surfaceIndex;
        packet.nodeIndex = surfaceInstance.nodeIndex;
        packet.meshIndex = surfaceInstance.meshIndex;
        packet.primitiveIndex = surfaceInstance.primitiveIndex;
        packet.materialIndex = surfaceInstance.materialIndex;
        packet.objectWorldTransform = surfaceInstance.objectWorldTransform;
        packet.drawWorldMatrix = surfaceInstance.drawWorldMatrix;
        packet.hasDrawWorldMatrix = surfaceInstance.hasDrawWorldMatrix;
        packet.worldBounds = surfaceInstance.worldBounds;
        packet.visible = surfaceInstance.visible;
        packet.isStatic = surfaceInstance.isStatic;
        packet.skinned = surfaceInstance.skinned;
        packet.castShadow = surfaceInstance.castShadow;
        packet.receiveShadow = surfaceInstance.receiveShadow;
        packet.materialOverride = surfaceInstance.materialOverride;
        packet.materialFxProfileId = surfaceInstance.materialFxProfileId;
        packet.postGroupMask = surfaceInstance.postGroupMask;
        packet.materialFxValuesInitialized = surfaceInstance.materialFxValuesInitialized;
        CopyMaterialFxValues(surfaceInstance, packet);

        // ここでは並び替えず、後段が使う key だけを作る。
        packet.forwardCandidate = packet.visible;
        packet.shadowCandidate = packet.visible && packet.castShadow;
        packet.key.materialIndex = packet.materialIndex;
        packet.key.meshIndex = packet.meshIndex;
        packet.key.primitiveIndex = packet.primitiveIndex;
        packet.key.hasMaterialOverride = packet.materialOverride != nullptr;
        packet.key.skinned = packet.skinned;
        packet.key.passMask = BuildPassMask(packet);
        // 後段 sorter 用の論理キーだけを作る。
        packet.key = BuildPacketKey(packet);

        const SurfaceDrawPacketValidationResult validation = ValidatePacket(packet);
        packet.valid = validation.IsValid();
        if (!packet.valid) {
            packet.forwardCandidate = false;
            packet.shadowCandidate = false;
            packet.key.passMask = BuildPassMask(packet);
            packet.key = BuildPacketKey(packet);
        }

        packets_.push_back(std::move(packet));
    }

    void SurfaceDrawPacketBuilder::RefreshStats() {
        Stats stats{};
        stats.packetCount = ClampToUint32(packets_.size());
        std::unordered_set<uint64_t> modelBuckets{};
        std::unordered_set<uint64_t> geometryBuckets{};
        std::unordered_set<uint64_t> materialBuckets{};
        std::unordered_set<uint64_t> textureSetBuckets{};
        std::unordered_set<uint64_t> shaderBuckets{};
        std::unordered_set<uint64_t> psoBuckets{};
        const SurfaceDrawPacketKey* previousResourceKey = nullptr;

        for (const SurfaceDrawPacket& packet : packets_) {
            if (packet.valid) {
                ++stats.validPacketCount;
            } else {
                ++stats.invalidPacketCount;
            }
            if (packet.visible) {
                ++stats.visiblePacketCount;
            } else {
                ++stats.hiddenPacketCount;
            }
            if (packet.isStatic) {
                ++stats.staticPacketCount;
            } else {
                ++stats.dynamicPacketCount;
            }
            if (packet.skinned) {
                ++stats.skinnedPacketCount;
            } else {
                ++stats.staticGeometryPacketCount;
            }
            if (packet.forwardCandidate) {
                ++stats.forwardCandidateCount;
            }
            if (packet.shadowCandidate) {
                ++stats.shadowCandidateCount;
            }

            if (packet.key.hasMaterialOverride) {
                ++stats.materialOverridePacketCount;
            } else {
                ++stats.materialAssetPacketCount;
            }
            if (packet.key.transparent) {
                ++stats.transparentPacketCount;
            } else if (packet.key.alphaMasked) {
                ++stats.alphaMaskedPacketCount;
            } else {
                ++stats.opaquePacketCount;
            }
            if (packet.key.resourceKeyValid) {
                modelBuckets.insert(packet.key.modelKey);
                geometryBuckets.insert(packet.key.geometryKey);
                materialBuckets.insert(packet.key.materialKey);
                textureSetBuckets.insert(packet.key.textureSetKey);
                shaderBuckets.insert(packet.key.shaderKey);
                psoBuckets.insert(packet.key.psoKey);
                if (previousResourceKey != nullptr && ComesBeforeForBatching(packet.key, *previousResourceKey)) {
                    ++stats.sortOrderBreakCount;
                }
                previousResourceKey = &packet.key;
            } else {
                ++stats.invalidResourceKeyCount;
            }

            const SurfaceDrawPacketValidationResult validation = ValidatePacket(packet);
            if (validation.invalidSource) {
                ++stats.invalidSourceCount;
            }
            if (validation.invalidModel) {
                ++stats.invalidModelCount;
            }
            if (validation.unsupportedGeometry) {
                ++stats.unsupportedGeometryCount;
            }
            if (validation.missingDrawMatrix) {
                ++stats.missingDrawMatrixCount;
            }
            if (validation.invalidBounds) {
                ++stats.invalidBoundsCount;
            }
            if (validation.invalidPrimitiveIndex) {
                ++stats.invalidPrimitiveIndexCount;
            }
        }

        stats.modelBucketCount = ClampToUint32(modelBuckets.size());
        stats.geometryBucketCount = ClampToUint32(geometryBuckets.size());
        stats.materialBucketCount = ClampToUint32(materialBuckets.size());
        stats.textureSetBucketCount = ClampToUint32(textureSetBuckets.size());
        stats.shaderBucketCount = ClampToUint32(shaderBuckets.size());
        stats.psoBucketCount = ClampToUint32(psoBuckets.size());
        stats_ = stats;
    }

} // namespace HIKARI::RENDER3D::RUNTIME
