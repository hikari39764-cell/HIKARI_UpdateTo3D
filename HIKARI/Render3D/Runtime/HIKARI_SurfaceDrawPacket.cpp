#include "Render3D/Runtime/HIKARI_SurfaceDrawPacket.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Runtime/HIKARI_RenderSurfaceResolver.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawCommandBuilder.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawRoute.h"
#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"

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

        uint64_t BuildStableStringKey(std::string_view tag, const std::string& value) {
            if (value.empty()) {
                return 0;
            }
            uint64_t key = HashString(tag);
            key = HashAppend(key, HashString(value));
            return key;
        }

        uint64_t BuildModelKey(const SurfaceDrawPacket& packet) {
            if (packet.renderModel != nullptr) {
                if (const uint64_t sourceNameKey = BuildStableStringKey("render-model-name", packet.renderModel->sourceName);
                    sourceNameKey != 0) {
                    return sourceNameKey;
                }
                if (const uint64_t sourcePathKey = BuildStableStringKey("render-model-path", packet.renderModel->sourcePath);
                    sourcePathKey != 0) {
                    return sourcePathKey;
                }
            }

            const ModelAsset* model = packet.model;
            if (model == nullptr) {
                return 0;
            }
            if (const uint64_t modelNameKey = BuildStableStringKey("model-name", model->id.value);
                modelNameKey != 0) {
                return modelNameKey;
            }
            if (const uint64_t modelPathKey = BuildStableStringKey("model-path", model->sourcePath);
                modelPathKey != 0) {
                return modelPathKey;
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
            const uint64_t modelKey = BuildModelKey(packet);

            const SurfaceDrawShaderRoute shaderRoute = ResolveSurfaceDrawShaderRoute(
                packet.materialOverride,
                materialAsset,
                packet.materialFxProfileId);
            const std::string& shaderProfile = shaderRoute.shaderProfileId;
            const uint32_t featureBits = shaderRoute.featureBits;
            const bool doubleSided = shaderRoute.doubleSided;
            const AlphaMode alphaMode = shaderRoute.alphaMode;

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
            key.sortKey = HashAppend(key.sortKey, key.geometryKey);
            key.sortKey = HashAppend(key.sortKey, key.materialKey);
            key.sortKey = HashAppend(key.sortKey, key.textureSetKey);

            key.alphaMasked = alphaMode == AlphaMode::Mask;
            key.transparent = alphaMode == AlphaMode::Blend;
            key.resourceKeyValid = modelKey != 0 && key.geometryKey != 0 && key.materialKey != 0;
            key.objectDataCompatible = shaderRoute.objectDataCompatible;
            key.depthAwareMaterialFx = shaderRoute.depthAwareMaterialFx;
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
            if (lhs.geometryKey != rhs.geometryKey) {
                return lhs.geometryKey < rhs.geometryKey;
            }
            if (lhs.materialKey != rhs.materialKey) {
                return lhs.materialKey < rhs.materialKey;
            }
            if (lhs.textureSetKey != rhs.textureSetKey) {
                return lhs.textureSetKey < rhs.textureSetKey;
            }
            return lhs.sortKey < rhs.sortKey;
        }

        bool IsSortCandidate(const SurfaceDrawPacket& packet) {
            return
                packet.valid &&
                packet.key.resourceKeyValid &&
                !packet.key.transparent &&
                (packet.forwardCandidate || packet.shadowCandidate);
        }

        enum class RunKeyKind {
            Pass,
            Pso,
            Material,
            TextureSet,
            Geometry,
        };

        uint64_t SelectRunKey(const SurfaceDrawPacket& packet, RunKeyKind kind) {
            switch (kind) {
            case RunKeyKind::Pass:
                return packet.key.passMask;
            case RunKeyKind::Pso:
                return packet.key.psoKey;
            case RunKeyKind::Material:
                return packet.key.materialKey;
            case RunKeyKind::TextureSet:
                return packet.key.textureSetKey;
            case RunKeyKind::Geometry:
                return packet.key.geometryKey;
            default:
                return 0;
            }
        }

        uint32_t CountRawRuns(const std::vector<SurfaceDrawPacket>& packets, RunKeyKind kind) {
            bool hasPrevious = false;
            uint64_t previous = 0;
            uint32_t runs = 0;

            for (const SurfaceDrawPacket& packet : packets) {
                if (!IsSortCandidate(packet)) {
                    continue;
                }

                const uint64_t current = SelectRunKey(packet, kind);
                if (!hasPrevious || current != previous) {
                    ++runs;
                    previous = current;
                    hasPrevious = true;
                }
            }
            return runs;
        }

        uint32_t CountSortedRuns(
            const std::vector<SurfaceDrawPacket>& packets,
            const std::vector<uint32_t>& sortedIndices,
            RunKeyKind kind) {

            bool hasPrevious = false;
            uint64_t previous = 0;
            uint32_t runs = 0;

            for (uint32_t packetIndex : sortedIndices) {
                if (packetIndex >= packets.size()) {
                    continue;
                }
                const SurfaceDrawPacket& packet = packets[packetIndex];
                if (!IsSortCandidate(packet)) {
                    continue;
                }

                const uint64_t current = SelectRunKey(packet, kind);
                if (!hasPrevious || current != previous) {
                    ++runs;
                    previous = current;
                    hasPrevious = true;
                }
            }
            return runs;
        }

        uint32_t CountSortedBreaks(
            const std::vector<SurfaceDrawPacket>& packets,
            const std::vector<uint32_t>& sortedIndices) {

            uint32_t breakCount = 0;
            const SurfaceDrawPacketKey* previousKey = nullptr;
            for (uint32_t packetIndex : sortedIndices) {
                if (packetIndex >= packets.size()) {
                    continue;
                }
                const SurfaceDrawPacket& packet = packets[packetIndex];
                if (!IsSortCandidate(packet)) {
                    continue;
                }

                if (previousKey != nullptr && ComesBeforeForBatching(packet.key, *previousKey)) {
                    ++breakCount;
                }
                previousKey = &packet.key;
            }
            return breakCount;
        }

        bool HasValidSubmitPrimitiveTarget(const SurfaceDrawPacket& packet) {
            return
                packet.model != nullptr &&
                packet.surface != nullptr &&
                packet.hasDrawWorldMatrix &&
                BOUNDS::IsUsable(packet.worldBounds) &&
                HasValidRenderSurfacePrimitive(packet.model, *packet.surface);
        }

        bool IsPacketCulledByCamera(
            const SurfaceDrawPacket& packet,
            const SurfaceDrawPacketPlanOptions& options) {

            if (!options.enableFrustumCulling || !options.hasCameraViewProj) {
                return false;
            }
            // world bounds は既に world 空間なので viewProjection だけで判定する。
            return !BOUNDS::IntersectsClipFrustum(packet.worldBounds, options.cameraViewProj);
        }

        MATH::Vec3 GetBoundsCenter(const Bounds& bounds) {
            return {
                (bounds.min.x + bounds.max.x) * 0.5f,
                (bounds.min.y + bounds.max.y) * 0.5f,
                (bounds.min.z + bounds.max.z) * 0.5f,
            };
        }

        float ResolveTransparentViewDepth(
            const SurfaceDrawPacket& packet,
            const SurfaceDrawPacketPlanOptions& options) {

            if (!options.hasCameraView || !BOUNDS::IsUsable(packet.worldBounds)) {
                return 0.0f;
            }

            const MATH::Vec3 center = GetBoundsCenter(packet.worldBounds);
            const MATH::Vec4 viewCenter =
                options.cameraView.TransformPoint({ center.x, center.y, center.z, 1.0f });
            return std::isfinite(viewCenter.z) ? viewCenter.z : 0.0f;
        }

        struct TransparentDepthSortEntry {
            uint32_t packetIndex = 0;
            uint32_t originalOrdinal = 0;
            uint32_t sourceSurfaceInstanceIndex = 0;
            float viewDepth = 0.0f;
        };

        void SortTransparentDepthEntries(
            std::vector<TransparentDepthSortEntry>& entries,
            const SurfaceDrawPacketPlanOptions& options,
            SurfaceDrawPacketPlanStats& stats) {

            stats.transparentDepthSortCandidateCount = ClampToUint32(entries.size());
            if (!options.hasCameraView) {
                stats.transparentDepthSortFallbackPacketCount = ClampToUint32(entries.size());
                return;
            }

            // HIKARI は右手系 view 空間なので、より小さい z を遠方として先に描画する。
            std::stable_sort(
                entries.begin(),
                entries.end(),
                [](const TransparentDepthSortEntry& lhs, const TransparentDepthSortEntry& rhs) {
                    if (lhs.viewDepth != rhs.viewDepth) {
                        return lhs.viewDepth < rhs.viewDepth;
                    }
                    return lhs.sourceSurfaceInstanceIndex < rhs.sourceSurfaceInstanceIndex;
                });

            stats.transparentDepthSortedPacketCount = ClampToUint32(entries.size());
            for (size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
                if (entries[entryIndex].originalOrdinal != entryIndex) {
                    ++stats.transparentDepthReorderedPacketCount;
                }
            }
        }

        void RecordForwardRejectReason(
            SurfaceDrawRouteRejectReason reason,
            SurfaceDrawPacketPlanStats* stats) {

            if (stats == nullptr) {
                return;
            }

            switch (reason) {
            case SurfaceDrawRouteRejectReason::NoForward:
                ++stats->skippedNoForwardPacketCount;
                break;
            case SurfaceDrawRouteRejectReason::InvalidPacket:
                ++stats->skippedInvalidPacketCount;
                break;
            case SurfaceDrawRouteRejectReason::InvalidResourceKey:
                ++stats->skippedInvalidResourceKeyCount;
                break;
            case SurfaceDrawRouteRejectReason::LegacyShader:
                ++stats->skippedLegacyShaderPacketCount;
                break;
            case SurfaceDrawRouteRejectReason::DepthAwareMaterialFx:
                ++stats->skippedDepthAwarePacketCount;
                break;
            case SurfaceDrawRouteRejectReason::RuntimeAnimation:
                ++stats->skippedRuntimeAnimationPacketCount;
                break;
            case SurfaceDrawRouteRejectReason::SpecialDebug:
                ++stats->skippedSpecialDebugPacketCount;
                break;
            case SurfaceDrawRouteRejectReason::Skinned:
                ++stats->skippedSkinnedPacketCount;
                break;
            case SurfaceDrawRouteRejectReason::Transparent:
                ++stats->skippedTransparentPacketCount;
                break;
            case SurfaceDrawRouteRejectReason::AlphaMasked:
                ++stats->skippedAlphaMaskedPacketCount;
                break;
            case SurfaceDrawRouteRejectReason::InvalidPrimitive:
                ++stats->skippedInvalidPrimitiveCount;
                break;
            case SurfaceDrawRouteRejectReason::None:
            case SurfaceDrawRouteRejectReason::NoShadow:
            default:
                break;
            }
        }

        void RecordRouteBucket(
            SurfaceDrawRouteRejectReason reason,
            const SurfaceDrawPacket& packet,
            SurfaceDrawRouteBucketStats& stats) {

            const SurfaceDrawRouteBucket bucket = GetSurfaceDrawRouteBucket(reason);
            switch (bucket) {
            case SurfaceDrawRouteBucket::MainRoute:
                ++stats.mainRoutePacketCount;
                if (packet.key.transparent) {
                    ++stats.mainTransparentPacketCount;
                } else if (packet.key.alphaMasked) {
                    ++stats.mainAlphaMaskPacketCount;
                } else {
                    ++stats.mainOpaquePacketCount;
                }
                break;
            case SurfaceDrawRouteBucket::NoPass:
                ++stats.noPassPacketCount;
                break;
            case SurfaceDrawRouteBucket::AlphaMask:
                ++stats.alphaMaskPacketCount;
                break;
            case SurfaceDrawRouteBucket::Transparent:
                ++stats.transparentPacketCount;
                break;
            case SurfaceDrawRouteBucket::DepthAwareMaterialFx:
                ++stats.depthAwarePacketCount;
                break;
            case SurfaceDrawRouteBucket::RuntimeSpecial:
                ++stats.runtimeSpecialPacketCount;
                break;
            case SurfaceDrawRouteBucket::Skinned:
                ++stats.skinnedPacketCount;
                break;
            case SurfaceDrawRouteBucket::LegacyShader:
                ++stats.legacyShaderPacketCount;
                break;
            case SurfaceDrawRouteBucket::Invalid:
            default:
                ++stats.invalidPacketCount;
                break;
            }
        }

        void RecordForwardRouteClassification(
            SurfaceDrawRouteRejectReason reason,
            const SurfaceDrawPacket& packet,
            SurfaceDrawPacketPlanStats& stats) {

            RecordForwardRejectReason(reason, &stats);
            RecordRouteBucket(reason, packet, stats.forwardRouteBuckets);
        }

        void RecordShadowRejectReason(
            SurfaceDrawRouteRejectReason reason,
            SurfaceDrawPacketPlanStats* stats) {

            if (stats == nullptr) {
                return;
            }

            switch (reason) {
            case SurfaceDrawRouteRejectReason::NoShadow:
                ++stats->shadowSkippedNoShadowPacketCount;
                break;
            case SurfaceDrawRouteRejectReason::InvalidPacket:
                ++stats->shadowSkippedInvalidPacketCount;
                break;
            case SurfaceDrawRouteRejectReason::InvalidResourceKey:
                ++stats->shadowSkippedInvalidResourceKeyCount;
                break;
            case SurfaceDrawRouteRejectReason::RuntimeAnimation:
                ++stats->shadowSkippedRuntimeAnimationPacketCount;
                break;
            case SurfaceDrawRouteRejectReason::SpecialDebug:
                ++stats->shadowSkippedSpecialDebugPacketCount;
                break;
            case SurfaceDrawRouteRejectReason::Skinned:
                ++stats->shadowSkippedSkinnedPacketCount;
                break;
            case SurfaceDrawRouteRejectReason::Transparent:
                ++stats->shadowSkippedTransparentPacketCount;
                break;
            case SurfaceDrawRouteRejectReason::InvalidPrimitive:
                ++stats->shadowSkippedInvalidPrimitiveCount;
                break;
            case SurfaceDrawRouteRejectReason::None:
            case SurfaceDrawRouteRejectReason::NoForward:
            case SurfaceDrawRouteRejectReason::LegacyShader:
            case SurfaceDrawRouteRejectReason::DepthAwareMaterialFx:
            case SurfaceDrawRouteRejectReason::AlphaMasked:
            default:
                break;
            }
        }

        void RecordShadowRouteClassification(
            SurfaceDrawRouteRejectReason reason,
            const SurfaceDrawPacket& packet,
            SurfaceDrawPacketPlanStats& stats) {

            RecordShadowRejectReason(reason, &stats);
            RecordRouteBucket(reason, packet, stats.shadowRouteBuckets);
        }

        void CopyMaterialFxValues(const SceneSurfaceInstance& source, SurfaceDrawPacket& packet) {
            for (int i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                packet.materialFxParamValues[i] = source.materialFxParamValues[i];
            }
        }
    }

    void SurfaceDrawPacketBuilder::Clear() {
        packets_.clear();
        sortedPacketIndices_.clear();
        stats_ = {};
    }

    void SurfaceDrawPacketBuilder::BuildFromSceneRenderCache(const SceneRenderCache& sceneCache) {
        packets_.clear();
        sortedPacketIndices_.clear();
        packets_.reserve(sceneCache.GetSurfaceInstances().size());

        uint32_t sourceSurfaceInstanceIndex = 0;
        for (const SceneSurfaceInstance& surfaceInstance : sceneCache.GetSurfaceInstances()) {
            AppendPacket(surfaceInstance, sourceSurfaceInstanceIndex);
            ++sourceSurfaceInstanceIndex;
        }

        RebuildSortedPacketIndices();
        RefreshStats();
    }

    const std::vector<SurfaceDrawPacket>& SurfaceDrawPacketBuilder::GetPackets() const {
        return packets_;
    }

    const std::vector<uint32_t>& SurfaceDrawPacketBuilder::GetSortedPacketIndices() const {
        return sortedPacketIndices_;
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
        packet.hasRuntimeAnimation = surfaceInstance.hasRuntimeAnimation;
        packet.hasSpecialRenderDebug = surfaceInstance.hasSpecialRenderDebug;
        packet.skinned = surfaceInstance.skinned;
        packet.castShadow = surfaceInstance.castShadow;
        packet.receiveShadow = surfaceInstance.receiveShadow;
        packet.materialOverride = surfaceInstance.materialOverride;
        packet.materialFxProfileId = surfaceInstance.materialFxProfileId;
        packet.postGroupMask = surfaceInstance.postGroupMask;
        packet.materialFxValuesInitialized = surfaceInstance.materialFxValuesInitialized;
        CopyMaterialFxValues(surfaceInstance, packet);

        // 縺薙％縺ｧ縺ｯ荳ｦ縺ｳ譖ｿ縺医★縲∝ｾ梧ｮｵ縺御ｽｿ縺・key 縺縺代ｒ菴懊ｋ縲・
        packet.forwardCandidate = packet.visible;
        packet.shadowCandidate = packet.visible && packet.castShadow;
        packet.key.materialIndex = packet.materialIndex;
        packet.key.meshIndex = packet.meshIndex;
        packet.key.primitiveIndex = packet.primitiveIndex;
        packet.key.hasMaterialOverride = packet.materialOverride != nullptr;
        packet.key.skinned = packet.skinned;
        packet.key.passMask = BuildPassMask(packet);
        // 蠕梧ｮｵ sorter 逕ｨ縺ｮ隲也炊繧ｭ繝ｼ縺縺代ｒ菴懊ｋ縲・
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

    void SurfaceDrawPacketBuilder::RebuildSortedPacketIndices() {
        sortedPacketIndices_.clear();
        sortedPacketIndices_.reserve(packets_.size());

        for (size_t packetIndex = 0; packetIndex < packets_.size(); ++packetIndex) {
            const SurfaceDrawPacket& packet = packets_[packetIndex];
            if (IsSortCandidate(packet)) {
                sortedPacketIndices_.push_back(ClampToUint32(packetIndex));
            }
        }

        // 螳滓緒逕ｻ縺ｯ螟峨∴縺壹∝ｾ梧ｮｵ batch 逕ｨ縺ｮ view 縺縺代ｒ螳牙ｮ壹た繝ｼ繝医☆繧九・
        std::stable_sort(
            sortedPacketIndices_.begin(),
            sortedPacketIndices_.end(),
            [this](uint32_t lhsIndex, uint32_t rhsIndex) {
                if (lhsIndex >= packets_.size() || rhsIndex >= packets_.size()) {
                    return lhsIndex < rhsIndex;
                }

                const SurfaceDrawPacket& lhs = packets_[lhsIndex];
                const SurfaceDrawPacket& rhs = packets_[rhsIndex];
                if (ComesBeforeForBatching(lhs.key, rhs.key)) {
                    return true;
                }
                if (ComesBeforeForBatching(rhs.key, lhs.key)) {
                    return false;
                }
                return lhs.sourceSurfaceInstanceIndex < rhs.sourceSurfaceInstanceIndex;
            });
    }

    void SurfaceDrawPacketBuilder::RefreshStats() {
        Stats stats{};
        stats.packetCount = ClampToUint32(packets_.size());
        stats.sortedPacketCount = ClampToUint32(sortedPacketIndices_.size());
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
            if (IsSortCandidate(packet)) {
                ++stats.sortEligiblePacketCount;
            } else if (
                packet.valid &&
                packet.key.resourceKeyValid &&
                packet.key.transparent &&
                (packet.forwardCandidate || packet.shadowCandidate)) {
                ++stats.transparentResourceSortExcludedCount;
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
        stats.sortedSortOrderBreakCount = CountSortedBreaks(packets_, sortedPacketIndices_);

        stats.rawPassRunCount = CountRawRuns(packets_, RunKeyKind::Pass);
        stats.sortedPassRunCount = CountSortedRuns(packets_, sortedPacketIndices_, RunKeyKind::Pass);
        stats.rawPsoRunCount = CountRawRuns(packets_, RunKeyKind::Pso);
        stats.sortedPsoRunCount = CountSortedRuns(packets_, sortedPacketIndices_, RunKeyKind::Pso);
        stats.rawMaterialRunCount = CountRawRuns(packets_, RunKeyKind::Material);
        stats.sortedMaterialRunCount = CountSortedRuns(packets_, sortedPacketIndices_, RunKeyKind::Material);
        stats.rawTextureSetRunCount = CountRawRuns(packets_, RunKeyKind::TextureSet);
        stats.sortedTextureSetRunCount = CountSortedRuns(packets_, sortedPacketIndices_, RunKeyKind::TextureSet);
        stats.rawGeometryRunCount = CountRawRuns(packets_, RunKeyKind::Geometry);
        stats.sortedGeometryRunCount = CountSortedRuns(packets_, sortedPacketIndices_, RunKeyKind::Geometry);

        uint32_t rawCandidateOrdinal = 0;
        for (size_t packetIndex = 0; packetIndex < packets_.size(); ++packetIndex) {
            if (!IsSortCandidate(packets_[packetIndex])) {
                continue;
            }
            if (rawCandidateOrdinal < sortedPacketIndices_.size() &&
                sortedPacketIndices_[rawCandidateOrdinal] != packetIndex) {
                ++stats.reorderedPacketCount;
            }
            ++rawCandidateOrdinal;
        }

        stats_ = stats;
    }

    void SurfaceDrawPacketPlanner::Build(
        const SurfaceDrawPacketBuilder& builder,
        const SurfaceDrawPacketPlanOptions& options,
        SurfaceDrawPacketPlanStats& outStats) {

        outStats = {};
        objectCoverage_.clear();

        const std::vector<SurfaceDrawPacket>& packets = builder.GetPackets();
        const std::vector<uint32_t>& sortedIndices = builder.GetSortedPacketIndices();
        SurfaceDrawCommandBuilder forwardOpaqueCommandBuilder(
            SurfaceDrawCommandPass::Forward,
            packets,
            executableForwardOpaquePacketIndices_,
            executableForwardOpaqueCommands_);
        SurfaceDrawCommandBuilder forwardTransparentCommandBuilder(
            SurfaceDrawCommandPass::Forward,
            packets,
            executableForwardTransparentPacketIndices_,
            executableForwardTransparentCommands_);
        SurfaceDrawCommandBuilder shadowCommandBuilder(
            SurfaceDrawCommandPass::Shadow,
            packets,
            executableShadowPacketIndices_,
            executableShadowCommands_);
        forwardOpaqueCommandBuilder.ClearOutput();
        forwardTransparentCommandBuilder.ClearOutput();
        shadowCommandBuilder.ClearOutput();
        forwardOpaqueGpuSceneInstances_.clear();
        forwardTransparentGpuSceneInstances_.clear();
        shadowGpuSceneInstances_.clear();

        outStats.sourcePacketCount = ClampToUint32(packets.size());
        outStats.sortedPacketCount = ClampToUint32(sortedIndices.size());

        if (!options.buildForwardPlan && !options.buildShadowPlan) {
            return;
        }

        BuildCoverage(packets, outStats);

        if (options.buildForwardPlan && options.bypassLegacyForward) {
            for (uint32_t packetIndex : sortedIndices) {
                if (packetIndex >= packets.size()) {
                    forwardOpaqueCommandBuilder.Flush();
                    continue;
                }

                const SurfaceDrawPacket& packet = packets[packetIndex];
                if (packet.key.transparent) {
                    forwardOpaqueCommandBuilder.Flush();
                    continue;
                }
                const bool fullCoverage = HasFullForwardCoverageForObject(packet.objectId);
                if (!fullCoverage) {
                    if (packet.forwardCandidate) {
                        ++outStats.skippedPartialCoveragePacketCount;
                    }
                    forwardOpaqueCommandBuilder.Flush();
                    continue;
                }
                if (!IsForwardSafePacket(packet, nullptr)) {
                    forwardOpaqueCommandBuilder.Flush();
                    continue;
                }

                ++outStats.candidatePacketCount;
                if (IsPacketCulledByCamera(packet, options)) {
                    ++outStats.culledPacketCount;
                    ++outStats.handledForwardPacketCount;
                    forwardOpaqueCommandBuilder.Flush();
                    continue;
                }

                // executor が直接描画する packet だけを登録する。
                forwardOpaqueCommandBuilder.AppendPacket(packetIndex);
                ++outStats.submittedForwardPacketCount;
                ++outStats.submittedForwardOpaquePacketCount;
                ++outStats.handledForwardPacketCount;
            }
            forwardOpaqueCommandBuilder.Flush();

            std::vector<TransparentDepthSortEntry> transparentDepthEntries{};
            transparentDepthEntries.reserve(packets.size());
            uint32_t transparentOrdinal = 0;
            for (size_t rawPacketIndex = 0; rawPacketIndex < packets.size(); ++rawPacketIndex) {
                if (rawPacketIndex > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
                    break;
                }

                const uint32_t packetIndex = static_cast<uint32_t>(rawPacketIndex);
                const SurfaceDrawPacket& packet = packets[packetIndex];
                if (!packet.forwardCandidate || !packet.key.transparent) {
                    continue;
                }

                const bool fullCoverage = HasFullForwardCoverageForObject(packet.objectId);
                if (!fullCoverage) {
                    ++outStats.skippedPartialCoveragePacketCount;
                    forwardTransparentCommandBuilder.Flush();
                    continue;
                }
                if (!IsForwardSafePacket(packet, nullptr)) {
                    forwardTransparentCommandBuilder.Flush();
                    continue;
                }

                ++outStats.candidatePacketCount;
                if (IsPacketCulledByCamera(packet, options)) {
                    ++outStats.culledPacketCount;
                    ++outStats.handledForwardPacketCount;
                    continue;
                }

                transparentDepthEntries.push_back({
                    packetIndex,
                    transparentOrdinal,
                    packet.sourceSurfaceInstanceIndex,
                    ResolveTransparentViewDepth(packet, options),
                });
                ++transparentOrdinal;
            }

            SortTransparentDepthEntries(transparentDepthEntries, options, outStats);
            for (const TransparentDepthSortEntry& entry : transparentDepthEntries) {
                // Transparent は深度順を優先し、その上で隣接する同一 geometry だけを batch 化する。
                forwardTransparentCommandBuilder.AppendPacket(entry.packetIndex);
                ++outStats.submittedForwardPacketCount;
                ++outStats.submittedForwardTransparentPacketCount;
                ++outStats.handledForwardPacketCount;
            }
            forwardTransparentCommandBuilder.Flush();
            const SurfaceDrawCommandBuildStats& opaqueCommandStats = forwardOpaqueCommandBuilder.GetStats();
            const SurfaceDrawCommandBuildStats& transparentCommandStats = forwardTransparentCommandBuilder.GetStats();
            outStats.submittedOpaqueCommandCount = opaqueCommandStats.commandCount;
            outStats.submittedOpaqueSinglePacketCommandCount = opaqueCommandStats.singlePacketCommandCount;
            outStats.submittedOpaqueMaxCommandPacketCount = opaqueCommandStats.maxCommandPacketCount;
            outStats.submittedTransparentCommandCount = transparentCommandStats.commandCount;
            outStats.submittedTransparentSinglePacketCommandCount = transparentCommandStats.singlePacketCommandCount;
            outStats.submittedTransparentMaxCommandPacketCount = transparentCommandStats.maxCommandPacketCount;
            outStats.submittedCommandCount =
                opaqueCommandStats.commandCount + transparentCommandStats.commandCount;
            outStats.submittedSinglePacketCommandCount =
                opaqueCommandStats.singlePacketCommandCount + transparentCommandStats.singlePacketCommandCount;
            outStats.submittedMaxCommandPacketCount =
                (std::max)(opaqueCommandStats.maxCommandPacketCount, transparentCommandStats.maxCommandPacketCount);

            const SurfaceGpuSceneBuildStats opaqueGpuSceneStats =
                SurfaceGpuSceneWriter::BuildCommandRanges(
                    packets,
                    executableForwardOpaquePacketIndices_,
                    executableForwardOpaqueCommands_,
                    forwardOpaqueGpuSceneInstances_);
            const SurfaceGpuSceneBuildStats transparentGpuSceneStats =
                SurfaceGpuSceneWriter::BuildCommandRanges(
                    packets,
                    executableForwardTransparentPacketIndices_,
                    executableForwardTransparentCommands_,
                    forwardTransparentGpuSceneInstances_);
            outStats.submittedOpaqueGpuSceneInstanceCount = opaqueGpuSceneStats.instanceCount;
            outStats.submittedTransparentGpuSceneInstanceCount = transparentGpuSceneStats.instanceCount;
            outStats.submittedGpuSceneInstanceCount =
                opaqueGpuSceneStats.instanceCount + transparentGpuSceneStats.instanceCount;
            outStats.submittedMaxGpuSceneCommandInstanceCount =
                (std::max)(opaqueGpuSceneStats.maxCommandInstanceCount, transparentGpuSceneStats.maxCommandInstanceCount);
        }


        if (options.buildShadowPlan && options.bypassLegacyShadow) {
            for (uint32_t packetIndex : sortedIndices) {
                if (packetIndex >= packets.size()) {
                    shadowCommandBuilder.Flush();
                    continue;
                }

                const SurfaceDrawPacket& packet = packets[packetIndex];
                const bool fullCoverage = HasFullShadowCoverageForObject(packet.objectId);
                if (!fullCoverage) {
                    if (packet.shadowCandidate) {
                        ++outStats.shadowSkippedPartialCoveragePacketCount;
                    }
                    shadowCommandBuilder.Flush();
                    continue;
                }
                if (!IsShadowSafePacket(packet, nullptr)) {
                    shadowCommandBuilder.Flush();
                    continue;
                }

                ++outStats.shadowCandidatePacketCount;
                // shadow pass も submit queue を経由せず、plan から直接実行する。
                shadowCommandBuilder.AppendPacket(packetIndex);
                ++outStats.plannedShadowPacketCount;
                ++outStats.handledShadowPacketCount;
            }
            shadowCommandBuilder.Flush();
            const SurfaceDrawCommandBuildStats& commandStats = shadowCommandBuilder.GetStats();
            outStats.shadowCommandCount = commandStats.commandCount;
            outStats.shadowSinglePacketCommandCount = commandStats.singlePacketCommandCount;
            outStats.shadowMaxCommandPacketCount = commandStats.maxCommandPacketCount;

            const SurfaceGpuSceneBuildStats shadowGpuSceneStats =
                SurfaceGpuSceneWriter::BuildCommandRanges(
                    packets,
                    executableShadowPacketIndices_,
                    executableShadowCommands_,
                    shadowGpuSceneInstances_);
            outStats.shadowGpuSceneInstanceCount = shadowGpuSceneStats.instanceCount;
            outStats.shadowMaxGpuSceneCommandInstanceCount =
                shadowGpuSceneStats.maxCommandInstanceCount;
        }
    }

    bool SurfaceDrawPacketPlanner::HasFullForwardCoverageForObject(SceneRenderObjectId objectId) const {
        if (!objectId.IsValid()) {
            return false;
        }
        const auto found = objectCoverage_.find(objectId.value);
        if (found == objectCoverage_.end()) {
            return false;
        }

        const ObjectCoverage& coverage = found->second;
        return
            coverage.expectedForwardPacketCount > 0 &&
            coverage.safeForwardPacketCount == coverage.expectedForwardPacketCount;
    }

    const std::vector<uint32_t>& SurfaceDrawPacketPlanner::GetExecutableForwardOpaquePacketIndices() const {
        return executableForwardOpaquePacketIndices_;
    }

    const std::vector<SurfaceDrawCommand>& SurfaceDrawPacketPlanner::GetExecutableForwardOpaqueCommands() const {
        return executableForwardOpaqueCommands_;
    }

    const std::vector<SurfaceGpuSceneInstance>& SurfaceDrawPacketPlanner::GetForwardOpaqueGpuSceneInstances() const {
        return forwardOpaqueGpuSceneInstances_;
    }

    const std::vector<uint32_t>& SurfaceDrawPacketPlanner::GetExecutableForwardTransparentPacketIndices() const {
        return executableForwardTransparentPacketIndices_;
    }

    const std::vector<SurfaceDrawCommand>& SurfaceDrawPacketPlanner::GetExecutableForwardTransparentCommands() const {
        return executableForwardTransparentCommands_;
    }

    const std::vector<SurfaceGpuSceneInstance>& SurfaceDrawPacketPlanner::GetForwardTransparentGpuSceneInstances() const {
        return forwardTransparentGpuSceneInstances_;
    }

    bool SurfaceDrawPacketPlanner::HasFullShadowCoverageForObject(SceneRenderObjectId objectId) const {
        if (!objectId.IsValid()) {
            return false;
        }
        const auto found = objectCoverage_.find(objectId.value);
        if (found == objectCoverage_.end()) {
            return false;
        }

        const ObjectCoverage& coverage = found->second;
        return
            coverage.expectedShadowPacketCount > 0 &&
            coverage.safeShadowPacketCount == coverage.expectedShadowPacketCount;
    }

    const std::vector<uint32_t>& SurfaceDrawPacketPlanner::GetExecutableShadowPacketIndices() const {
        return executableShadowPacketIndices_;
    }

    const std::vector<SurfaceDrawCommand>& SurfaceDrawPacketPlanner::GetExecutableShadowCommands() const {
        return executableShadowCommands_;
    }

    const std::vector<SurfaceGpuSceneInstance>& SurfaceDrawPacketPlanner::GetShadowGpuSceneInstances() const {
        return shadowGpuSceneInstances_;
    }

    bool SurfaceDrawPacketPlanner::IsForwardSafePacket(
        const SurfaceDrawPacket& packet,
        SurfaceDrawPacketPlanStats* stats) const {

        const SurfaceDrawRouteRejectReason reason = ClassifyForwardSurfaceDrawRoute(packet);
        RecordForwardRejectReason(reason, stats);
        return IsSurfaceDrawRouteAccepted(reason);
    }

    bool SurfaceDrawPacketPlanner::IsShadowSafePacket(
        const SurfaceDrawPacket& packet,
        SurfaceDrawPacketPlanStats* stats) const {

        const SurfaceDrawRouteRejectReason reason = ClassifyShadowSurfaceDrawRoute(packet);
        RecordShadowRejectReason(reason, stats);
        return IsSurfaceDrawRouteAccepted(reason);
    }

    void SurfaceDrawPacketPlanner::BuildCoverage(
        const std::vector<SurfaceDrawPacket>& packets,
        SurfaceDrawPacketPlanStats& stats) {

        for (const SurfaceDrawPacket& packet : packets) {
            const SurfaceDrawRouteRejectReason forwardReason = ClassifyForwardSurfaceDrawRoute(packet);
            RecordForwardRouteClassification(forwardReason, packet, stats);
            if (packet.forwardCandidate && packet.objectId.IsValid()) {
                ObjectCoverage& coverage = objectCoverage_[packet.objectId.value];
                ++coverage.expectedForwardPacketCount;
                if (IsSurfaceDrawRouteAccepted(forwardReason)) {
                    ++coverage.safeForwardPacketCount;
                }
            }

            const SurfaceDrawRouteRejectReason shadowReason = ClassifyShadowSurfaceDrawRoute(packet);
            RecordShadowRouteClassification(shadowReason, packet, stats);
            if (packet.shadowCandidate && packet.objectId.IsValid()) {
                ObjectCoverage& coverage = objectCoverage_[packet.objectId.value];
                ++coverage.expectedShadowPacketCount;
                if (IsSurfaceDrawRouteAccepted(shadowReason)) {
                    ++coverage.safeShadowPacketCount;
                }
            }
        }

        for (const auto& [objectId, coverage] : objectCoverage_) {
            (void)objectId;
            if (coverage.expectedForwardPacketCount > 0) {
                ++stats.candidateObjectCount;
                if (coverage.safeForwardPacketCount == coverage.expectedForwardPacketCount) {
                    ++stats.fullCoverageObjectCount;
                } else if (coverage.safeForwardPacketCount > 0) {
                    ++stats.partialCoverageObjectCount;
                } else {
                    ++stats.runtimeSpecialObjectCount;
                }
            }

            if (coverage.expectedShadowPacketCount > 0) {
                ++stats.shadowCandidateObjectCount;
                if (coverage.safeShadowPacketCount == coverage.expectedShadowPacketCount) {
                    ++stats.shadowFullCoverageObjectCount;
                } else if (coverage.safeShadowPacketCount > 0) {
                    ++stats.shadowPartialCoverageObjectCount;
                } else {
                    ++stats.shadowRuntimeSpecialObjectCount;
                }
            }
        }
    }

} // namespace HIKARI::RENDER3D::RUNTIME
