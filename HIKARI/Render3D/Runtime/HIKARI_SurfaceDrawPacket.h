#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
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

        uint64_t modelKey = 0;
        uint64_t geometryKey = 0;
        uint64_t materialKey = 0;
        uint64_t textureSetKey = 0;
        uint64_t shaderKey = 0;
        uint64_t psoKey = 0;
        uint64_t sortKey = 0;

        bool resourceKeyValid = false;
        bool objectDataCompatible = false;
        bool depthAwareMaterialFx = false;
        bool alphaMasked = false;
        bool transparent = false;
    };

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
        bool hasSpecialRenderDebug = false;
        bool skinned = false;
        bool castShadow = true;
        bool receiveShadow = true;
        bool forwardCandidate = false;
        bool shadowCandidate = false;

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

    struct SurfaceDrawPacketPlanOptions {
        bool buildForwardPlan = false;
        bool bypassLegacyForward = false;
        bool buildShadowPlan = false;
        bool bypassLegacyShadow = false;

        MATH::Mat4 cameraViewProj{};
        MATH::Mat4 cameraView{};
        bool hasCameraViewProj = false;
        bool hasCameraView = false;
        bool enableFrustumCulling = true;
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
        uint32_t submittedForwardTransparentPacketCount = 0;
        uint32_t culledPacketCount = 0;
        uint32_t handledForwardPacketCount = 0;
        uint32_t submittedCommandCount = 0;
        uint32_t submittedSinglePacketCommandCount = 0;
        uint32_t submittedMaxCommandPacketCount = 0;
        uint32_t submittedOpaqueCommandCount = 0;
        uint32_t submittedOpaqueSinglePacketCommandCount = 0;
        uint32_t submittedOpaqueMaxCommandPacketCount = 0;
        uint32_t submittedTransparentCommandCount = 0;
        uint32_t submittedTransparentSinglePacketCommandCount = 0;
        uint32_t submittedTransparentMaxCommandPacketCount = 0;
        uint32_t submittedGpuSceneInstanceCount = 0;
        uint32_t submittedOpaqueGpuSceneInstanceCount = 0;
        uint32_t submittedTransparentGpuSceneInstanceCount = 0;
        uint32_t submittedMaxGpuSceneCommandInstanceCount = 0;
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
        uint32_t shadowMaxCommandPacketCount = 0;
        uint32_t shadowGpuSceneInstanceCount = 0;
        uint32_t shadowMaxGpuSceneCommandInstanceCount = 0;

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
        const std::vector<uint32_t>& GetExecutableForwardOpaquePacketIndices() const;
        const std::vector<SurfaceDrawCommand>& GetExecutableForwardOpaqueCommands() const;
        const std::vector<SurfaceGpuSceneInstance>& GetForwardOpaqueGpuSceneInstances() const;
        const std::vector<uint32_t>& GetExecutableForwardTransparentPacketIndices() const;
        const std::vector<SurfaceDrawCommand>& GetExecutableForwardTransparentCommands() const;
        const std::vector<SurfaceGpuSceneInstance>& GetForwardTransparentGpuSceneInstances() const;
        const std::vector<uint32_t>& GetExecutableShadowPacketIndices() const;
        const std::vector<SurfaceDrawCommand>& GetExecutableShadowCommands() const;
        const std::vector<SurfaceGpuSceneInstance>& GetShadowGpuSceneInstances() const;

    private:
        struct ObjectCoverage {
            uint32_t expectedForwardPacketCount = 0;
            uint32_t safeForwardPacketCount = 0;
            uint32_t expectedShadowPacketCount = 0;
            uint32_t safeShadowPacketCount = 0;
        };

        bool IsForwardSafePacket(const SurfaceDrawPacket& packet, SurfaceDrawPacketPlanStats* stats) const;
        bool IsShadowSafePacket(const SurfaceDrawPacket& packet, SurfaceDrawPacketPlanStats* stats) const;
        void BuildCoverage(const std::vector<SurfaceDrawPacket>& packets, SurfaceDrawPacketPlanStats& stats);
        std::unordered_map<uint64_t, ObjectCoverage> objectCoverage_{};
        std::vector<uint32_t> executableForwardOpaquePacketIndices_{};
        std::vector<SurfaceDrawCommand> executableForwardOpaqueCommands_{};
        std::vector<SurfaceGpuSceneInstance> forwardOpaqueGpuSceneInstances_{};
        std::vector<uint32_t> executableForwardTransparentPacketIndices_{};
        std::vector<SurfaceDrawCommand> executableForwardTransparentCommands_{};
        std::vector<SurfaceGpuSceneInstance> forwardTransparentGpuSceneInstances_{};
        std::vector<uint32_t> executableShadowPacketIndices_{};
        std::vector<SurfaceDrawCommand> executableShadowCommands_{};
        std::vector<SurfaceGpuSceneInstance> shadowGpuSceneInstances_{};
    };

} // namespace HIKARI::RENDER3D::RUNTIME
