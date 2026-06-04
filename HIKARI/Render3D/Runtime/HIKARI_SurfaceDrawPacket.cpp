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

        bool IsSameSubmitRun(const SurfaceDrawPacketKey& lhs, const SurfaceDrawPacketKey& rhs) {
            return
                lhs.passMask == rhs.passMask &&
                lhs.psoKey == rhs.psoKey &&
                lhs.materialKey == rhs.materialKey &&
                lhs.textureSetKey == rhs.textureSetKey;
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
            const SurfaceDrawPacketSubmitOptions& options) {

            if (!options.enableFrustumCulling || !options.hasCameraViewProj) {
                return false;
            }
            // world bounds は既に world 空間なので viewProjection だけで判定する。
            return !BOUNDS::IntersectsClipFrustum(packet.worldBounds, options.cameraViewProj);
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

    void SurfaceDrawPacketBuilder::RebuildSortedPacketIndices() {
        sortedPacketIndices_.clear();
        sortedPacketIndices_.reserve(packets_.size());

        for (size_t packetIndex = 0; packetIndex < packets_.size(); ++packetIndex) {
            const SurfaceDrawPacket& packet = packets_[packetIndex];
            if (IsSortCandidate(packet)) {
                sortedPacketIndices_.push_back(ClampToUint32(packetIndex));
            }
        }

        // 実描画は変えず、後段 batch 用の view だけを安定ソートする。
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
                ++stats.transparentSortExcludedCount;
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

    void SurfaceDrawPacketSubmitter::Submit(
        const SurfaceDrawPacketBuilder& builder,
        const SurfaceDrawPacketSubmitOptions& options,
        SurfaceDrawPacketSubmitStats& outStats) {

        outStats = {};
        objectCoverage_.clear();
        handledForwardPrimitivesByObject_.clear();
        executableForwardPacketIndices_.clear();
        executableForwardRuns_.clear();

        const std::vector<SurfaceDrawPacket>& packets = builder.GetPackets();
        const std::vector<uint32_t>& sortedIndices = builder.GetSortedPacketIndices();
        outStats.sourcePacketCount = ClampToUint32(packets.size());
        outStats.sortedPacketCount = ClampToUint32(sortedIndices.size());

        if (!options.useSortedForward || !options.skipOldStaticForwardSubmit) {
            return;
        }

        BuildCoverage(packets, outStats);

        const SurfaceDrawPacketKey* currentSubmittedRunKey = nullptr;
        uint32_t currentSubmittedRunStart = 0;
        uint32_t currentSubmittedRunLength = 0;
        auto flushSubmittedRun = [&]() {
            if (currentSubmittedRunLength == 0) {
                return;
            }
            SurfaceDrawPacketRun run{};
            run.firstExecutableIndex = currentSubmittedRunStart;
            run.packetCount = currentSubmittedRunLength;
            executableForwardRuns_.push_back(run);

            ++outStats.submittedRunCount;
            if (currentSubmittedRunLength == 1) {
                ++outStats.submittedSinglePacketRunCount;
            }
            outStats.submittedMaxRunPacketCount =
                (std::max)(outStats.submittedMaxRunPacketCount, currentSubmittedRunLength);
            currentSubmittedRunKey = nullptr;
            currentSubmittedRunStart = 0;
            currentSubmittedRunLength = 0;
        };

        for (uint32_t packetIndex : sortedIndices) {
            if (packetIndex >= packets.size()) {
                flushSubmittedRun();
                continue;
            }

            const SurfaceDrawPacket& packet = packets[packetIndex];
            const bool fullCoverage = HasFullForwardCoverageForObject(packet.objectId);
            const bool primitiveFallback = !fullCoverage && CanUsePrimitiveFallback(packet);
            if (!fullCoverage && !primitiveFallback) {
                if (packet.forwardCandidate && packet.isStatic) {
                    ++outStats.skippedPartialCoveragePacketCount;
                }
                flushSubmittedRun();
                continue;
            }
            if (!IsSubmitSafePacket(packet, nullptr)) {
                flushSubmittedRun();
                continue;
            }

            ++outStats.candidatePacketCount;
            if (primitiveFallback) {
                ++outStats.partialTakeoverPacketCount;
            }
            if (IsPacketCulledByCamera(packet, options)) {
                ++outStats.culledPacketCount;
                ++outStats.handledForwardPacketCount;
                if (primitiveFallback) {
                    RecordHandledForwardPrimitive(packet, outStats);
                }
                flushSubmittedRun();
                continue;
            }

            if (currentSubmittedRunKey == nullptr ||
                !IsSameSubmitRun(packet.key, *currentSubmittedRunKey)) {
                flushSubmittedRun();
                currentSubmittedRunKey = &packet.key;
                currentSubmittedRunStart = ClampToUint32(executableForwardPacketIndices_.size());
            }
            ++currentSubmittedRunLength;

            // executor で直接描画する packet だけを記録する。
            executableForwardPacketIndices_.push_back(packetIndex);
            ++outStats.submittedForwardPacketCount;
            ++outStats.handledForwardPacketCount;
            if (primitiveFallback) {
                RecordHandledForwardPrimitive(packet, outStats);
            }
        }
        flushSubmittedRun();
        outStats.handledPrimitiveObjectCount =
            ClampToUint32(handledForwardPrimitivesByObject_.size());
    }

    bool SurfaceDrawPacketSubmitter::HasFullForwardCoverageForObject(SceneRenderObjectId objectId) const {
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

    const std::vector<SurfaceDrawPacketHandledPrimitive>* SurfaceDrawPacketSubmitter::GetHandledForwardPrimitivesForObject(
        SceneRenderObjectId objectId) const {

        if (!objectId.IsValid()) {
            return nullptr;
        }
        const auto found = handledForwardPrimitivesByObject_.find(objectId.value);
        if (found == handledForwardPrimitivesByObject_.end() || found->second.empty()) {
            return nullptr;
        }
        return &found->second;
    }

    const std::vector<uint32_t>& SurfaceDrawPacketSubmitter::GetExecutableForwardPacketIndices() const {
        return executableForwardPacketIndices_;
    }

    const std::vector<SurfaceDrawPacketRun>& SurfaceDrawPacketSubmitter::GetExecutableForwardRuns() const {
        return executableForwardRuns_;
    }

    bool SurfaceDrawPacketSubmitter::IsSubmitSafePacket(
        const SurfaceDrawPacket& packet,
        SurfaceDrawPacketSubmitStats* stats) const {

        if (!packet.forwardCandidate) {
            if (stats != nullptr) {
                ++stats->skippedNoForwardPacketCount;
            }
            return false;
        }
        if (!packet.isStatic) {
            if (stats != nullptr) {
                ++stats->skippedDynamicPacketCount;
            }
            return false;
        }
        if (!packet.valid) {
            if (stats != nullptr) {
                ++stats->skippedInvalidPacketCount;
            }
            return false;
        }
        if (!packet.key.resourceKeyValid) {
            if (stats != nullptr) {
                ++stats->skippedInvalidResourceKeyCount;
            }
            return false;
        }
        if (packet.skinned) {
            if (stats != nullptr) {
                ++stats->skippedSkinnedPacketCount;
            }
            return false;
        }
        if (packet.key.transparent) {
            if (stats != nullptr) {
                ++stats->skippedTransparentPacketCount;
            }
            return false;
        }
        if (packet.key.alphaMasked) {
            if (stats != nullptr) {
                ++stats->skippedAlphaMaskedPacketCount;
            }
            return false;
        }
        if (!HasValidSubmitPrimitiveTarget(packet)) {
            if (stats != nullptr) {
                ++stats->skippedInvalidPrimitiveCount;
            }
            return false;
        }
        return true;
    }

    bool SurfaceDrawPacketSubmitter::CanUsePrimitiveFallback(const SurfaceDrawPacket& packet) const {
        if (!packet.objectId.IsValid() ||
            packet.model == nullptr ||
            packet.nodeIndex == kInvalidRenderSurfaceIndex ||
            packet.meshIndex == kInvalidRenderSurfaceIndex ||
            packet.primitiveIndex == kInvalidRenderSurfaceIndex ||
            packet.nodeIndex >= packet.model->nodes.size() ||
            packet.meshIndex >= packet.model->meshes.size()) {
            return false;
        }

        const ModelNode& node = packet.model->nodes[packet.nodeIndex];
        if (node.skinIndex >= 0 || node.meshIndex < 0 ||
            static_cast<uint32_t>(node.meshIndex) != packet.meshIndex) {
            return false;
        }

        const MeshAsset& mesh = packet.model->meshes[packet.meshIndex];
        return packet.primitiveIndex < mesh.primitives.size();
    }

    void SurfaceDrawPacketSubmitter::RecordHandledForwardPrimitive(
        const SurfaceDrawPacket& packet,
        SurfaceDrawPacketSubmitStats& stats) {

        if (!CanUsePrimitiveFallback(packet)) {
            return;
        }

        SurfaceDrawPacketHandledPrimitive handled{};
        handled.nodeIndex = packet.nodeIndex;
        handled.meshIndex = packet.meshIndex;
        handled.primitiveIndex = packet.primitiveIndex;

        std::vector<SurfaceDrawPacketHandledPrimitive>& primitives =
            handledForwardPrimitivesByObject_[packet.objectId.value];
        const auto duplicate = std::find_if(
            primitives.begin(),
            primitives.end(),
            [&handled](const SurfaceDrawPacketHandledPrimitive& existing) {
                return
                    existing.nodeIndex == handled.nodeIndex &&
                    existing.meshIndex == handled.meshIndex &&
                    existing.primitiveIndex == handled.primitiveIndex;
            });
        if (duplicate != primitives.end()) {
            return;
        }

        primitives.push_back(handled);
        ++stats.handledPrimitiveCount;
    }

    void SurfaceDrawPacketSubmitter::BuildCoverage(
        const std::vector<SurfaceDrawPacket>& packets,
        SurfaceDrawPacketSubmitStats& stats) {

        for (const SurfaceDrawPacket& packet : packets) {
            if (!packet.forwardCandidate) {
                ++stats.skippedNoForwardPacketCount;
                continue;
            }
            if (!packet.isStatic) {
                ++stats.skippedDynamicPacketCount;
                continue;
            }
            if (!packet.objectId.IsValid()) {
                ++stats.skippedInvalidPacketCount;
                continue;
            }

            ObjectCoverage& coverage = objectCoverage_[packet.objectId.value];
            ++coverage.expectedForwardPacketCount;
            if (IsSubmitSafePacket(packet, &stats)) {
                ++coverage.safeForwardPacketCount;
            }
        }

        stats.candidateObjectCount = ClampToUint32(objectCoverage_.size());
        for (const auto& [objectId, coverage] : objectCoverage_) {
            (void)objectId;
            if (coverage.expectedForwardPacketCount == 0) {
                continue;
            }
            if (coverage.safeForwardPacketCount == coverage.expectedForwardPacketCount) {
                ++stats.fullCoverageObjectCount;
            } else if (coverage.safeForwardPacketCount > 0) {
                ++stats.partialCoverageObjectCount;
            } else {
                ++stats.fallbackObjectCount;
            }
        }
    }

} // namespace HIKARI::RENDER3D::RUNTIME
