#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <DirectXMath.h>
#include <Vfx/Common/HIKARI_FxTypes.h>

#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPlan.h"
#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"

namespace HIKARI::RENDER3D::RUNTIME {

    enum class SurfaceDrawPacketPassFlags : uint8_t {
        None = 0,
        Forward = 1u << 0,
        Shadow = 1u << 1,
    };

    struct SurfaceDrawPacketKey {
        uint32_t materialIndex = 0;
        uint32_t meshIndex = kInvalidRenderSurfaceIndex;
        uint32_t primitiveIndex = kInvalidRenderSurfaceIndex;
        uint32_t passMask = 0;
        bool hasMaterialOverride = false;
        bool skinned = false;
        SurfaceGeometryBackend geometryBackend = SurfaceGeometryBackend::TriangleMesh;

        uint64_t modelKey = 0;
        uint64_t geometryKey = 0;
        uint64_t clusterGeometryKey = 0;
        uint64_t materialKey = 0;
        uint64_t textureSetKey = 0;
        uint64_t shaderKey = 0;
        uint64_t psoKey = 0;
        uint64_t sortKey = 0;
        SurfaceResourceIds resources{};

        bool resourceKeyValid = false;
        bool objectDataCompatible = false;
        bool clusterMainlineEligible = false;
        bool materialFx = false;
        bool waterMaterialFx = false;
        bool depthAware = false;
        bool alphaMasked = false;
        bool transparent = false;
        bool doubleSided = false;
    };

    inline SurfaceDrawBatchKey BuildSurfaceDrawBatchKey(
        SurfaceDrawCommandPass pass,
        const SurfaceDrawPacketKey& key) {

        SurfaceDrawBatchKey batchKey{};
        batchKey.pass = pass;
        batchKey.geometryBackend = key.geometryBackend;
        batchKey.psoKey = key.psoKey;
        batchKey.geometryKey = key.geometryKey;
        batchKey.transparent = key.transparent;
        batchKey.clusterMainlineEligible = key.clusterMainlineEligible;
        return batchKey;
    }

    struct SurfaceDrawPacket {
        SceneRenderObjectId objectId{};
        uint64_t objectVersion = 0;
        uint32_t sourceSurfaceInstanceIndex = kInvalidRenderSurfaceIndex;

        const SceneSurfaceInstance* sourceSurface = nullptr;
        const ModelAsset* model = nullptr;
        const RenderModelAsset* renderModel = nullptr;
        const RenderSurfaceRecord* surface = nullptr;

        uint32_t surfaceIndex = kInvalidRenderSurfaceIndex;
        uint32_t nodeIndex = kInvalidRenderSurfaceIndex;
        uint32_t meshIndex = kInvalidRenderSurfaceIndex;
        uint32_t primitiveIndex = kInvalidRenderSurfaceIndex;
        uint32_t materialIndex = 0;

        Transform3D objectWorldTransform{};
        MATH::Mat4 drawWorldMatrix{};
        bool hasDrawWorldMatrix = false;
        Bounds worldBounds{};

        bool valid = false;
        bool visible = true;
        bool isStatic = false;
        bool hasRuntimeAnimation = false;
        std::string animationClipName{};
        float animationTimeSec = 0.0f;
        bool animationLoop = true;
        bool hasSpecialRenderDebug = false;
        bool skinned = false;
        bool castShadow = true;
        bool receiveShadow = true;
        bool forwardCandidate = false;
        bool shadowCandidate = false;
        std::string clusteredGeometryPath{};

        const Material* materialOverride = nullptr;
        std::string materialFxProfileId{};
        uint32_t postGroupMask = 0;
        DirectX::XMFLOAT4 materialFxParamValues[VFX::kMaterialFxUserCount]{};
        bool materialFxValuesInitialized = false;

        SurfaceDrawPacketKey key{};
    };

    struct SurfaceDrawPacketValidationResult {
        bool invalidSource = false;
        bool invalidModel = false;
        bool unsupportedGeometry = false;
        bool missingDrawMatrix = false;
        bool invalidBounds = false;
        bool invalidPrimitiveIndex = false;

        bool IsValid() const {
            return
                !invalidSource &&
                !invalidModel &&
                !unsupportedGeometry &&
                !missingDrawMatrix &&
                !invalidBounds &&
                !invalidPrimitiveIndex;
        }
    };

    SurfaceDrawPacket BuildSurfaceDrawPacketFromSceneSurface(
        const SceneSurfaceInstance& surfaceInstance,
        uint32_t sourceSurfaceInstanceIndex);

    struct SurfaceDrawPacketPlanOptions {
        bool buildForwardPlan = false;
        bool bypassLegacyForward = false;
        bool buildShadowPlan = false;
        bool bypassLegacyShadow = false;

        MATH::Mat4 cameraViewProj{};
        MATH::Mat4 cameraView{};
        bool hasCameraViewProj = false;
        bool hasCameraView = false;
        bool enableCpuFrustumCulling = true;
    };

    struct SurfaceDrawRouteBucketStats {
        uint32_t mainRoutePacketCount = 0;
        uint32_t mainOpaquePacketCount = 0;
        uint32_t mainAlphaMaskPacketCount = 0;
        uint32_t mainTransparentPacketCount = 0;
        uint32_t noPassPacketCount = 0;
        uint32_t alphaMaskPacketCount = 0;
        uint32_t transparentPacketCount = 0;
        uint32_t depthAwarePacketCount = 0;
        uint32_t runtimeSpecialPacketCount = 0;
        uint32_t skinnedPacketCount = 0;
        uint32_t legacyShaderPacketCount = 0;
        uint32_t invalidPacketCount = 0;
    };

    struct SurfaceDrawPacketPlanStats {
        uint32_t sourcePacketCount = 0;
        uint32_t sortedPacketCount = 0;

        uint32_t candidateObjectCount = 0;
        uint32_t fullCoverageObjectCount = 0;
        uint32_t partialCoverageObjectCount = 0;
        uint32_t runtimeSpecialObjectCount = 0;
        uint32_t mainForwardBypassObjectCount = 0;

        uint32_t candidatePacketCount = 0;
        uint32_t submittedForwardPacketCount = 0;
        uint32_t submittedForwardOpaquePacketCount = 0;
        uint32_t submittedForwardDepthAwarePacketCount = 0;
        uint32_t submittedForwardTransparentPacketCount = 0;
        uint32_t culledPacketCount = 0;
        uint32_t handledForwardPacketCount = 0;
        uint32_t submittedCommandCount = 0;
        uint32_t submittedSinglePacketCommandCount = 0;
        uint32_t submittedMergedCommandCount = 0;
        uint32_t submittedSavedCommandCount = 0;
        uint32_t submittedIndirectReadyCommandCount = 0;
        uint32_t submittedMissingDrawArgsCommandCount = 0;
        uint32_t submittedMaxCommandPacketCount = 0;
        uint32_t submittedOpaqueCommandCount = 0;
        uint32_t submittedOpaqueSinglePacketCommandCount = 0;
        uint32_t submittedOpaqueMergedCommandCount = 0;
        uint32_t submittedOpaqueSavedCommandCount = 0;
        uint32_t submittedOpaqueMaxCommandPacketCount = 0;
        uint32_t submittedDepthAwareCommandCount = 0;
        uint32_t submittedDepthAwareSinglePacketCommandCount = 0;
        uint32_t submittedDepthAwareMergedCommandCount = 0;
        uint32_t submittedDepthAwareSavedCommandCount = 0;
        uint32_t submittedDepthAwareMaxCommandPacketCount = 0;
        uint32_t submittedTransparentCommandCount = 0;
        uint32_t submittedTransparentSinglePacketCommandCount = 0;
        uint32_t submittedTransparentMergedCommandCount = 0;
        uint32_t submittedTransparentSavedCommandCount = 0;
        uint32_t submittedTransparentMaxCommandPacketCount = 0;
        uint32_t submittedGpuSceneInstanceCount = 0;
        uint32_t submittedOpaqueGpuSceneInstanceCount = 0;
        uint32_t submittedDepthAwareGpuSceneInstanceCount = 0;
        uint32_t submittedTransparentGpuSceneInstanceCount = 0;
        uint32_t submittedMaxGpuSceneCommandInstanceCount = 0;
        uint32_t submittedGpuSceneResourceInstanceCount = 0;
        uint32_t submittedGpuSceneMissingResourceInstanceCount = 0;
        uint32_t submittedGpuSceneClusterResourceInstanceCount = 0;
        uint32_t submittedGpuSceneClusterShaderVisibleInstanceCount = 0;
        uint32_t submittedGpuSceneClusterSurfaceRangeInstanceCount = 0;
        uint32_t submittedGpuSceneClusterMissingSurfaceRangeInstanceCount = 0;
        uint32_t transparentDepthSortCandidateCount = 0;
        uint32_t transparentDepthSortedPacketCount = 0;
        uint32_t transparentDepthReorderedPacketCount = 0;
        uint32_t transparentDepthSortFallbackPacketCount = 0;

        uint32_t skippedNoForwardPacketCount = 0;
        uint32_t skippedInvalidPacketCount = 0;
        uint32_t skippedInvalidResourceKeyCount = 0;
        uint32_t skippedLegacyShaderPacketCount = 0;
        uint32_t skippedDepthAwarePacketCount = 0;
        uint32_t skippedRuntimeAnimationPacketCount = 0;
        uint32_t skippedSpecialDebugPacketCount = 0;
        uint32_t skippedSkinnedPacketCount = 0;
        uint32_t skippedTransparentPacketCount = 0;
        uint32_t skippedAlphaMaskedPacketCount = 0;
        uint32_t skippedInvalidPrimitiveCount = 0;
        uint32_t skippedPartialCoveragePacketCount = 0;
        SurfaceDrawRouteBucketStats forwardRouteBuckets{};

        uint32_t shadowCandidateObjectCount = 0;
        uint32_t shadowFullCoverageObjectCount = 0;
        uint32_t shadowPartialCoverageObjectCount = 0;
        uint32_t shadowRuntimeSpecialObjectCount = 0;
        uint32_t mainShadowBypassObjectCount = 0;

        uint32_t shadowCandidatePacketCount = 0;
        uint32_t plannedShadowPacketCount = 0;
        uint32_t handledShadowPacketCount = 0;
        uint32_t shadowCommandCount = 0;
        uint32_t shadowSinglePacketCommandCount = 0;
        uint32_t shadowMergedCommandCount = 0;
        uint32_t shadowSavedCommandCount = 0;
        uint32_t shadowIndirectReadyCommandCount = 0;
        uint32_t shadowMissingDrawArgsCommandCount = 0;
        uint32_t shadowMaxCommandPacketCount = 0;
        uint32_t shadowGpuSceneInstanceCount = 0;
        uint32_t shadowMaxGpuSceneCommandInstanceCount = 0;
        uint32_t shadowGpuSceneResourceInstanceCount = 0;
        uint32_t shadowGpuSceneMissingResourceInstanceCount = 0;
        uint32_t shadowGpuSceneClusterResourceInstanceCount = 0;
        uint32_t shadowGpuSceneClusterShaderVisibleInstanceCount = 0;
        uint32_t shadowGpuSceneClusterSurfaceRangeInstanceCount = 0;
        uint32_t shadowGpuSceneClusterMissingSurfaceRangeInstanceCount = 0;

        uint32_t shadowSkippedNoShadowPacketCount = 0;
        uint32_t shadowSkippedInvalidPacketCount = 0;
        uint32_t shadowSkippedInvalidResourceKeyCount = 0;
        uint32_t shadowSkippedRuntimeAnimationPacketCount = 0;
        uint32_t shadowSkippedSpecialDebugPacketCount = 0;
        uint32_t shadowSkippedSkinnedPacketCount = 0;
        uint32_t shadowSkippedTransparentPacketCount = 0;
        uint32_t shadowSkippedInvalidPrimitiveCount = 0;
        uint32_t shadowSkippedPartialCoveragePacketCount = 0;
        SurfaceDrawRouteBucketStats shadowRouteBuckets{};
    };

    class SurfaceDrawPacketBuilder {
    public:
        struct Stats {
            uint32_t packetCount = 0;
            uint32_t validPacketCount = 0;
            uint32_t invalidPacketCount = 0;

            uint32_t visiblePacketCount = 0;
            uint32_t hiddenPacketCount = 0;
            uint32_t staticPacketCount = 0;
            uint32_t dynamicPacketCount = 0;
            uint32_t staticGeometryPacketCount = 0;
            uint32_t skinnedPacketCount = 0;

            uint32_t forwardCandidateCount = 0;
            uint32_t shadowCandidateCount = 0;

            uint32_t materialAssetPacketCount = 0;
            uint32_t materialOverridePacketCount = 0;
            uint32_t opaquePacketCount = 0;
            uint32_t alphaMaskedPacketCount = 0;
            uint32_t transparentPacketCount = 0;

            uint32_t modelBucketCount = 0;
            uint32_t geometryBucketCount = 0;
            uint32_t materialBucketCount = 0;
            uint32_t textureSetBucketCount = 0;
            uint32_t shaderBucketCount = 0;
            uint32_t psoBucketCount = 0;
            uint32_t sortOrderBreakCount = 0;
            uint32_t invalidResourceKeyCount = 0;
            uint32_t resourceIdentityPacketCount = 0;
            uint32_t resourcePoolHandlePacketCount = 0;
            uint32_t resourcePoolMissingPacketCount = 0;
            uint32_t triangleGeometryBackendPacketCount = 0;
            uint32_t clusterGeometryBackendPacketCount = 0;
            uint32_t clusterGeometryResourcePacketCount = 0;

            uint32_t sortEligiblePacketCount = 0;
            uint32_t sortedPacketCount = 0;
            uint32_t reorderedPacketCount = 0;
            uint32_t transparentResourceSortExcludedCount = 0;
            uint32_t sortedSortOrderBreakCount = 0;

            uint32_t rawPassRunCount = 0;
            uint32_t sortedPassRunCount = 0;
            uint32_t rawPsoRunCount = 0;
            uint32_t sortedPsoRunCount = 0;
            uint32_t rawMaterialRunCount = 0;
            uint32_t sortedMaterialRunCount = 0;
            uint32_t rawTextureSetRunCount = 0;
            uint32_t sortedTextureSetRunCount = 0;
            uint32_t rawGeometryRunCount = 0;
            uint32_t sortedGeometryRunCount = 0;

            uint32_t invalidSourceCount = 0;
            uint32_t invalidModelCount = 0;
            uint32_t unsupportedGeometryCount = 0;
            uint32_t missingDrawMatrixCount = 0;
            uint32_t invalidBoundsCount = 0;
            uint32_t invalidPrimitiveIndexCount = 0;
        };

        void Clear();
        void BuildFromSceneRenderCache(const SceneRenderCache& sceneCache);

        const std::vector<SurfaceDrawPacket>& GetPackets() const;
        const std::vector<uint32_t>& GetSortedPacketIndices() const;
        const Stats& GetStats() const;

        static SurfaceDrawPacketValidationResult ValidatePacket(const SurfaceDrawPacket& packet);

    private:
        void AppendPacket(const SceneSurfaceInstance& surfaceInstance, uint32_t sourceSurfaceInstanceIndex);
        void RebuildSortedPacketIndices();
        void RefreshStats();

        std::vector<SurfaceDrawPacket> packets_{};
        std::vector<uint32_t> sortedPacketIndices_{};
        Stats stats_{};
    };

    class SurfaceDrawPacketPlanner {
    public:
        void Build(
            const SurfaceDrawPacketBuilder& builder,
            const SurfaceDrawPacketPlanOptions& options,
            SurfaceDrawPacketPlanStats& outStats);

        bool HasFullForwardCoverageForObject(SceneRenderObjectId objectId) const;
        bool HasFullShadowCoverageForObject(SceneRenderObjectId objectId) const;
        bool HasForwardCoverageForObject(SceneRenderObjectId objectId) const;
        bool HasShadowCoverageForObject(SceneRenderObjectId objectId) const;
        bool ShouldBypassLegacyForwardSurface(SceneRenderObjectId objectId, uint32_t nodeIndex, uint32_t meshIndex, uint32_t primitiveIndex) const;
        bool ShouldBypassLegacyShadowSurface(SceneRenderObjectId objectId, uint32_t nodeIndex, uint32_t meshIndex, uint32_t primitiveIndex) const;
        const std::vector<uint32_t>& GetExecutableForwardOpaquePacketIndices() const;
        std::vector<SurfaceDrawCommand>& GetExecutableForwardOpaqueCommands();
        const std::vector<SurfaceDrawCommand>& GetExecutableForwardOpaqueCommands() const;
        const std::vector<uint32_t>& GetExecutableForwardDepthAwarePacketIndices() const;
        std::vector<SurfaceDrawCommand>& GetExecutableForwardDepthAwareCommands();
        const std::vector<SurfaceDrawCommand>& GetExecutableForwardDepthAwareCommands() const;
        const std::vector<uint32_t>& GetExecutableForwardTransparentPacketIndices() const;
        std::vector<SurfaceDrawCommand>& GetExecutableForwardTransparentCommands();
        const std::vector<SurfaceDrawCommand>& GetExecutableForwardTransparentCommands() const;
        const std::vector<uint32_t>& GetExecutableShadowPacketIndices() const;
        const std::vector<SurfaceDrawCommand>& GetExecutableShadowCommands() const;
        const std::vector<SurfaceGpuSceneInstance>& GetShadowGpuSceneInstances() const;

    private:
        struct ObjectCoverage {
            uint32_t expectedForwardPacketCount = 0;
            uint32_t safeForwardPacketCount = 0;
            uint32_t handledForwardPacketCount = 0;
            uint32_t expectedShadowPacketCount = 0;
            uint32_t safeShadowPacketCount = 0;
            uint32_t handledShadowPacketCount = 0;
            std::unordered_set<uint64_t> forwardBypassSurfaceKeys{};
            std::unordered_set<uint64_t> shadowBypassSurfaceKeys{};
        };

        bool IsForwardSafePacket(const SurfaceDrawPacket& packet, SurfaceDrawPacketPlanStats* stats) const;
        bool IsShadowSafePacket(const SurfaceDrawPacket& packet, SurfaceDrawPacketPlanStats* stats) const;
        void BuildCoverage(const std::vector<SurfaceDrawPacket>& packets, SurfaceDrawPacketPlanStats& stats);
        void RecordHandledForwardPacket(const SurfaceDrawPacket& packet);
        void RecordHandledShadowPacket(const SurfaceDrawPacket& packet);
        std::unordered_map<uint64_t, ObjectCoverage> objectCoverage_{};
        std::vector<uint32_t> executableForwardOpaquePacketIndices_{};
        std::vector<SurfaceDrawCommand> executableForwardOpaqueCommands_{};
        std::vector<uint32_t> executableForwardDepthAwarePacketIndices_{};
        std::vector<SurfaceDrawCommand> executableForwardDepthAwareCommands_{};
        std::vector<uint32_t> executableForwardTransparentPacketIndices_{};
        std::vector<SurfaceDrawCommand> executableForwardTransparentCommands_{};
        std::vector<uint32_t> executableShadowPacketIndices_{};
        std::vector<SurfaceDrawCommand> executableShadowCommands_{};
        std::vector<SurfaceGpuSceneInstance> shadowGpuSceneInstances_{};
    };

} // namespace HIKARI::RENDER3D::RUNTIME
