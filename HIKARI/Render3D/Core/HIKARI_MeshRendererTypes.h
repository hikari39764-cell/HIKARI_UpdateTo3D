#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <DirectXMath.h>
#include <d3d12.h>

#include "Render3D/HIKARI_ModelAsset.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_Transform3D.h"
#include "Vfx/Common/HIKARI_FxTypes.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI {
    class Material;
}

namespace HIKARI::MESHRENDERER {

    enum class MeshRenderDebugMode {
        Normal,
        WireOverlay,
        WireOnly,
    };

    struct CameraCB {
        MATH::Mat4 viewProj{};
        MATH::Mat4 invViewProj{};
        MATH::Vec4 cameraPos{};
        MATH::Vec4 timeParams{};
        MATH::Vec4 screenParams{};
    };

    struct ObjectCB {
        MATH::Mat4 world{};
        MATH::Mat4 normalMatrix{};
        MATH::Vec4 baseColor{};
        uint32_t hasBaseColorTexture = 0;
        uint32_t fxFlags = 0;
        uint32_t materialFlags = 0;
        float alphaCutoff = 0.5f;
        MATH::Vec4 emissiveFactor{};
        uint32_t hasNormalTexture = 0;
        float normalScale = 1.0f;
        float normalPadding[2]{};
        uint32_t receiveShadow = 1;
        float shadowObjectPadding[3]{};
        uint32_t hasEmissiveTexture = 0;
        float emissivePadding[3]{};
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        uint32_t hasMetallicRoughnessTexture = 0;
        uint32_t hasOcclusionTexture = 0;
        float occlusionStrength = 1.0f;
        float pbrPadding[3]{};
        MATH::Vec4 fxUser[VFX::kMaterialFxUserCount]{};
        MATH::Vec4 fxUser0{};
        MATH::Vec4 fxUser1{};
        MATH::Vec4 fxUser2{};
        MATH::Vec4 fxUser3{};
    };

    // StructuredBuffer 用。ObjectCB の互換フィールドは含めない。
    struct ObjectGpuData {
        MATH::Mat4 world{};
        MATH::Mat4 normalMatrix{};
        MATH::Vec4 baseColor{};
        uint32_t hasBaseColorTexture = 0;
        uint32_t fxFlags = 0;
        uint32_t materialFlags = 0;
        float alphaCutoff = 0.5f;
        MATH::Vec4 emissiveFactor{};
        uint32_t hasNormalTexture = 0;
        float normalScale = 1.0f;
        float normalPadding[2]{};
        uint32_t receiveShadow = 1;
        float shadowObjectPadding[3]{};
        uint32_t hasEmissiveTexture = 0;
        float emissivePadding[3]{};
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        uint32_t hasMetallicRoughnessTexture = 0;
        uint32_t hasOcclusionTexture = 0;
        float occlusionStrength = 1.0f;
        uint32_t materialDataIndex = 0xffffffffu;
        float pbrPadding[2]{};
        MATH::Vec4 fxUser[VFX::kMaterialFxUserCount]{};
    };

    static_assert(sizeof(ObjectGpuData) == 384u);

    constexpr uint32_t kInvalidMaterialDataIndex = 0xffffffffu;
    constexpr uint32_t kInvalidTextureDescriptorIndex = 0xffffffffu;

    // MaterialData 用。texture handle は次段の bindless 化に残す。
    struct MaterialGpuData {
        MATH::Vec4 baseColor{};
        MATH::Vec4 emissiveFactor{};
        MATH::Vec4 pbrParams{}; // x: metallic, y: roughness, z: occlusion, w: alpha cutoff
        uint32_t materialFlags = 0;
        uint32_t hasBaseColorTexture = 0;
        uint32_t hasNormalTexture = 0;
        uint32_t hasEmissiveTexture = 0;
        uint32_t hasMetallicRoughnessTexture = 0;
        uint32_t hasOcclusionTexture = 0;
        float normalScale = 1.0f;
        float materialPadding0 = 0.0f;
        int32_t baseColorTextureHandle = -1;
        int32_t normalTextureHandle = -1;
        int32_t emissiveTextureHandle = -1;
        int32_t metallicRoughnessTextureHandle = -1;
        int32_t occlusionTextureHandle = -1;
        uint32_t baseColorTextureDescriptorIndex = kInvalidTextureDescriptorIndex;
        uint32_t normalTextureDescriptorIndex = kInvalidTextureDescriptorIndex;
        uint32_t emissiveTextureDescriptorIndex = kInvalidTextureDescriptorIndex;
        uint32_t metallicRoughnessTextureDescriptorIndex = kInvalidTextureDescriptorIndex;
        uint32_t occlusionTextureDescriptorIndex = kInvalidTextureDescriptorIndex;
        uint32_t materialPadding1[2]{};
    };

    static_assert(sizeof(MaterialGpuData) == 128u);

    struct LightCB {
        MATH::Vec4 directionalDir{};
        MATH::Vec4 directionalColor{};
        MATH::Vec4 ambientColor{};
        MATH::Vec4 specularParams{};
        MATH::Vec4 pointLightPosRange[8]{};
        MATH::Vec4 pointLightColorIntensity[8]{};
        float directionalIntensity = 1.0f;
        float ambientIntensity = 0.25f;
        uint32_t pointLightCount = 0;
        float lightPadding = 0.0f;
        MATH::Vec4 fogColorDensity{};
        MATH::Vec4 fogParams{};
        uint32_t debugView = 0;
        float debugPadding[3]{};
    };

    struct SkyEnvironmentCB {
        MATH::Vec4 skyZenithExposure{};
        MATH::Vec4 skyHorizonReflection{};
        MATH::Vec4 skyGroundAmbient{};
        MATH::Vec4 skyParams{};
        MATH::Vec4 iblParams{};
        MATH::Vec4 reflectionProbePositionRadius{};
        MATH::Vec4 reflectionProbeParams{};
        MATH::Vec4 reflectionProbeIntensity{};
        MATH::Vec4 reflectionProbeInfluenceBoxMin{};
        MATH::Vec4 reflectionProbeInfluenceBoxMax{};
        MATH::Vec4 reflectionProbeProjectionBoxMin{};
        MATH::Vec4 reflectionProbeProjectionBoxMax{};
        MATH::Vec4 reflectionProbeShapeParams{};
        MATH::Vec4 aoParams{};
        MATH::Vec4 lightProbeVolumeOrigin{};
        MATH::Vec4 lightProbeVolumeSpacing{};
        MATH::Vec4 lightProbeVolumeCounts{};
    };

    struct ShadowCB {
        MATH::Mat4 lightViewProj{};
        uint32_t enabled = 0;
        float depthBias = 0.001f;
        float normalBias = 0.02f;
        float strength = 0.75f;
        uint32_t pcfEnabled = 1;
        float pcfRadius = 1.0f;
        float texelSizeX = 1.0f / 2048.0f;
        float texelSizeY = 1.0f / 2048.0f;
    };

    constexpr size_t kMaxJointPaletteMatrices = 128u;
    constexpr UINT kMaxObjectCount = 2048u;
    constexpr UINT kMaxMaterialDataCount = 4096u;

    constexpr UINT AlignConstantBufferSize(size_t size) {
        return static_cast<UINT>((size + 255u) & ~255u);
    }

    struct JointPaletteCB {
        MATH::Mat4 jointMatrices[kMaxJointPaletteMatrices]{};
    };

    struct MaterialDataFrameTable {
        std::unordered_map<uint64_t, uint32_t> indexByKey{};
        std::unordered_set<uint32_t> textureDescriptorIndices{};
        uint32_t count = 0;

        void Clear() {
            indexByKey.clear();
            textureDescriptorIndices.clear();
            count = 0;
        }
    };

    struct MaterialTextureDescriptorIndices {
        uint32_t baseColor = kInvalidTextureDescriptorIndex;
        uint32_t normal = kInvalidTextureDescriptorIndex;
        uint32_t emissive = kInvalidTextureDescriptorIndex;
        uint32_t metallicRoughness = kInvalidTextureDescriptorIndex;
        uint32_t occlusion = kInvalidTextureDescriptorIndex;
    };

    struct DrawItem {
        const ModelAsset* asset = nullptr;
        const Material* materialOverride = nullptr;
        Transform3D transform{};
        std::vector<MATH::Mat4> jointPalette{};
        std::string materialFxProfileId{};
        uint32_t postGroupMask = 0;
        VFX::VariantKey variant{};
        std::array<MATH::Vec4, VFX::kMaterialFxUserCount> fxValues{};
        uint32_t fxFlags = 0;
        std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount> materialFxParamValues{};
        bool materialFxValuesInitialized = false;
        bool hasResolvedMaterialFxProfile = false;
        MaterialFxProfile resolvedMaterialFxProfile{};
        bool usePrimitiveFilter = false;
        uint32_t meshIndexFilter = 0;
        uint32_t primitiveIndexFilter = 0;
        bool receiveShadow = true;
        MeshRenderDebugMode renderDebugMode = MeshRenderDebugMode::Normal;
    };

    inline bool IsDepthAwarePhaseItem(const DrawItem& item) {
        return item.hasResolvedMaterialFxProfile &&
            item.resolvedMaterialFxProfile.renderPhase == MaterialFxRenderPhase::DepthAware;
    }

    struct MeshRendererDebugStats {
        size_t skinnedGpuDrawCount = 0;
        size_t skinnedFallbackCount = 0;
        size_t uploadedJointCount = 0;
        size_t maxJointCount = 0;
        size_t lastSkinnedVertexCount = 0;
        size_t staticDrawItemCount = 0;
        size_t skinnedDrawItemCount = 0;
        size_t wireDrawItemCount = 0;
        size_t wireGpuDrawCount = 0;
        size_t primitiveMeshCacheHitCount = 0;
        size_t primitiveMeshCacheMissCount = 0;
        size_t primitiveSkinnedMeshCacheHitCount = 0;
        size_t primitiveSkinnedMeshCacheMissCount = 0;
        size_t materialTextureCacheHitCount = 0;
        size_t materialTextureCacheMissCount = 0;
        size_t psoCacheHitCount = 0;
        size_t psoCacheMissCount = 0;
        size_t materialFxProfileCacheHitCount = 0;
        size_t materialFxProfileCacheMissCount = 0;
        size_t materialFxProfileCacheFailCount = 0;
        size_t normalTextureCacheHitCount = 0;
        size_t normalTextureCacheMissCount = 0;
        size_t normalMappedPrimitiveCount = 0;
        size_t normalMapFallbackCount = 0;
        size_t emissiveTextureCacheHitCount = 0;
        size_t emissiveTextureCacheMissCount = 0;
        size_t emissiveMappedPrimitiveCount = 0;
        size_t emissiveMapFallbackCount = 0;
        size_t pbrPrimitiveCount = 0;
        size_t unlitPrimitiveCount = 0;
        size_t metallicRoughnessTextureCacheHitCount = 0;
        size_t metallicRoughnessTextureCacheMissCount = 0;
        size_t metallicRoughnessMappedPrimitiveCount = 0;
        size_t metallicRoughnessFallbackCount = 0;
        size_t occlusionTextureCacheHitCount = 0;
        size_t occlusionTextureCacheMissCount = 0;
        size_t occlusionMappedPrimitiveCount = 0;
        size_t occlusionFallbackCount = 0;
        size_t rootSignatureBindCount = 0;
        size_t rootSignatureSkipCount = 0;
        size_t frameResourceBindCount = 0;
        size_t frameResourceSkipCount = 0;
        size_t objectResourceBindCount = 0;
        size_t objectResourceSkipCount = 0;
        size_t legacyObjectCbWriteCount = 0;
        size_t objectDataWriteCount = 0;
        size_t objectDataBufferBindCount = 0;
        size_t objectDataBufferSkipCount = 0;
        size_t objectIndexBindCount = 0;
        size_t objectIndexSkipCount = 0;
        size_t materialDataWriteCount = 0;
        size_t materialDataCacheHitCount = 0;
        size_t materialDataCacheMissCount = 0;
        size_t materialDataOverflowCount = 0;
        size_t materialDataCachedCount = 0;
        size_t materialDataBufferBindCount = 0;
        size_t materialDataBufferSkipCount = 0;
        size_t materialIndexBindCount = 0;
        size_t materialIndexSkipCount = 0;
        size_t materialTexturePoolSlotCount = 0;
        size_t materialTexturePoolResolvedSlotCount = 0;
        size_t materialTexturePoolInvalidSlotCount = 0;
        size_t materialTexturePoolUniqueDescriptorCount = 0;
        size_t descriptorTableBindCount = 0;
        size_t descriptorTableSkipCount = 0;
        size_t pipelineStateBindCount = 0;
        size_t pipelineStateSkipCount = 0;
        size_t surfacePacketExecutorPacketCount = 0;
        size_t surfacePacketExecutorForwardDrawCount = 0;
        size_t surfacePacketExecutorOpaqueDrawCount = 0;
        size_t surfacePacketExecutorDepthAwareDrawCount = 0;
        size_t surfacePacketExecutorTransparentDrawCount = 0;
        size_t surfacePacketExecutorGeometryDrawCount = 0;
        size_t surfacePacketExecutorSkippedPacketCount = 0;
        size_t surfacePacketExecutorCommandCount = 0;
        size_t surfacePacketExecutorOpaqueCommandCount = 0;
        size_t surfacePacketExecutorDepthAwareCommandCount = 0;
        size_t surfacePacketExecutorTransparentCommandCount = 0;
        size_t surfacePacketExecutorSinglePacketCommandCount = 0;
        size_t surfacePacketExecutorMergedCommandCount = 0;
        size_t surfacePacketExecutorSavedCommandCount = 0;
        size_t surfacePacketExecutorIndirectReadyCommandCount = 0;
        size_t surfacePacketExecutorMissingDrawArgsCommandCount = 0;
        size_t surfacePacketExecutorMaxCommandPacketCount = 0;
        size_t surfacePacketExecutorInstancedDrawCount = 0;
        size_t surfacePacketExecutorInstancedPacketCount = 0;
        size_t surfacePacketExecutorMaxInstanceCount = 0;
        size_t surfacePacketExecutorGpuSceneDrawCount = 0;
        size_t surfacePacketExecutorGpuScenePacketCount = 0;
        size_t surfacePacketExecutorGpuSceneFallbackCount = 0;
        size_t surfaceGpuSceneCapacity = 0;
        size_t surfaceIndirectCommandCapacity = 0;
        size_t surfaceIndirectRequestedCommandCount = 0;
        size_t surfaceIndirectUploadedCommandCount = 0;
        size_t surfaceIndirectOverflowCommandCount = 0;
        size_t surfaceIndirectFilteredCommandCount = 0;
        size_t surfaceIndirectCpuDirectCommandCount = 0;
        size_t surfaceIndirectMissingDrawArgsCommandCount = 0;
        size_t surfaceIndirectDrawBindingPatchCount = 0;
        size_t surfaceIndirectUploadCallCount = 0;
        size_t surfaceIndirectCommandStride = 0;
        size_t surfaceIndirectExecutedDrawCount = 0;
        size_t surfaceIndirectExecutedPacketCount = 0;
        size_t surfaceIndirectOpaqueCommandCount = 0;
        size_t surfaceIndirectOpaquePacketCount = 0;
        size_t surfaceIndirectDepthAwareCommandCount = 0;
        size_t surfaceIndirectDepthAwarePacketCount = 0;
        size_t surfaceIndirectTransparentCommandCount = 0;
        size_t surfaceIndirectTransparentPacketCount = 0;
        size_t surfaceIndirectFallbackCommandCount = 0;
        size_t surfaceIndirectBatchSubmitCount = 0;
        size_t surfaceIndirectBatchedCommandCount = 0;
        size_t surfaceIndirectSavedSubmitCount = 0;
        size_t surfaceIndirectMaxBatchCommandCount = 0;
        size_t surfaceGpuSceneOpaqueInstanceCount = 0;
        size_t surfaceGpuSceneDepthAwareInstanceCount = 0;
        size_t surfaceGpuSceneTransparentInstanceCount = 0;
        size_t surfaceGpuSceneRequestedInstanceCount = 0;
        size_t surfaceGpuSceneUploadedInstanceCount = 0;
        size_t surfaceGpuSceneOverflowInstanceCount = 0;
        size_t surfaceGpuSceneUploadCallCount = 0;
        size_t surfaceGpuSceneMaterialPatchCount = 0;
        size_t surfaceGpuSceneMaterialPatchFailCount = 0;
        size_t surfaceGpuSceneBufferBindCount = 0;
        size_t surfaceGpuSceneBufferSkipCount = 0;
        size_t clusterGpuCullSourceInstanceCount = 0;
        size_t clusterGpuCullSourceSingleSidedInstanceCount = 0;
        size_t clusterGpuCullSourceDoubleSidedInstanceCount = 0;
        size_t clusterGpuCullCandidateInstanceCount = 0;
        size_t clusterGpuCullSubmittedInstanceCount = 0;
        size_t clusterGpuCullSourcePageTaskCount = 0;
        size_t clusterGpuCullSubmittedPageTaskCount = 0;
        size_t clusterGpuCullOverflowInstanceCount = 0;
        size_t clusterGpuCullGpuInputCount = 0;
        size_t clusterGpuCullGpuPageTaskCount = 0;
        size_t clusterGpuCullGpuPageTaskOverflowCount = 0;
        size_t clusterGpuCullGpuVisibleRangeCount = 0;
        size_t clusterGpuCullGpuVisibleClusterCount = 0;
        size_t clusterGpuCullGpuCulledInstanceCount = 0;
        size_t clusterGpuCullGpuOverflowCount = 0;
        size_t clusterGpuCullGpuInputFrustumCulledCount = 0;
        size_t clusterGpuCullGpuPageTestedCount = 0;
        size_t clusterGpuCullGpuPageFrustumCulledCount = 0;
        size_t clusterGpuCullGpuClusterTestedCount = 0;
        size_t clusterGpuCullGpuClusterFrustumCulledCount = 0;
        size_t clusterGpuCullGpuClusterConeCulledCount = 0;
        size_t clusterGpuCullGpuClusterConeTestedCount = 0;
        size_t clusterGpuCullGpuDoubleSidedClusterCount = 0;
        size_t clusterGpuCullGpuDrawCommandCount = 0;
        size_t clusterGpuCullGpuBackFaceDrawCommandCount = 0;
        size_t clusterGpuCullGpuDoubleSidedDrawCommandCount = 0;
        size_t clusterGpuCullGpuDrawCommandOverflowCount = 0;
        size_t clusterGpuCullGpuBackFaceDrawCommandOverflowCount = 0;
        size_t clusterGpuCullGpuDoubleSidedDrawCommandOverflowCount = 0;
        size_t clusterGpuCullGpuMergedGapCount = 0;
        size_t clusterGpuCullGpuMergedGapIndexCount = 0;
        size_t clusterGpuCullDispatchCount = 0;
        size_t clusterGpuCullWorkgroupCount = 0;
        size_t clusterGpuCullInputCapacity = 0;
        size_t clusterGpuCullVisibleRangeCapacity = 0;
        size_t clusterGpuCullDrawArgumentCapacity = 0;
        size_t clusterGpuCullDrawSeedCount = 0;
        size_t clusterDrawEligibleCommandCount = 0;
        size_t clusterDrawRejectContextCommandCount = 0;
        size_t clusterDrawRejectMainlineCommandCount = 0;
        size_t clusterDrawRejectBackendCommandCount = 0;
        size_t clusterDrawRejectTransparentCommandCount = 0;
        size_t clusterDrawRejectMaterialFxCommandCount = 0;
        size_t clusterDrawRejectRangeCommandCount = 0;
        size_t clusterDrawRejectInstanceResourceCommandCount = 0;
        size_t clusterDrawRejectInstanceFlagCommandCount = 0;
        size_t clusterDrawRejectMaterialPatchCommandCount = 0;
        size_t clusterDrawRequestedCount = 0;
        size_t clusterDrawSubmittedCount = 0;
        size_t clusterDrawSkippedCount = 0;
        size_t clusterDrawSkippedBucketCount = 0;
        size_t clusterDrawSubmitCallCount = 0;
        size_t clusterDrawForwardSubmittedCount = 0;
        size_t clusterDrawGeometryBufferSubmittedCount = 0;
        size_t clusterDrawForwardSubmitCallCount = 0;
        size_t clusterDrawGeometryBufferSubmitCallCount = 0;
        size_t clusterDrawBackFaceSubmitCallCount = 0;
        size_t clusterDrawDoubleSidedSubmitCallCount = 0;
        size_t clusterDrawBypassedLegacyCommandCount = 0;
        size_t clusterDrawBypassedLegacyPacketCount = 0;
        size_t meshletBackendPipelineCreateRequestCount = 0;
        size_t meshletBackendPipelineCreateReadyCount = 0;
        uint32_t meshletBackendMeshShaderTier = 0;
        size_t clusterMainlineOwnedCommandCount = 0;
        size_t clusterMainlineOwnedPacketCount = 0;
        size_t clusterMainlineGeometryAuxCommandCount = 0;
        size_t clusterMainlineGeometryAuxPacketCount = 0;
        size_t clusterMainlineLegacyCommandCount = 0;
        size_t clusterMainlineLegacyPacketCount = 0;
        bool clusterMainlineReady = false;
        bool clusterMainlineForwardReady = false;
        bool clusterMainlineGeometryBufferReady = false;
        bool clusterMainlineHasDrawSeeds = false;
        bool clusterMainlineOverflowBlocked = false;
        bool surfaceGpuSceneSrvValid = false;
        bool surfaceGpuSceneBufferReady = false;
        bool surfaceIndirectArgumentBufferReady = false;
        bool surfaceIndirectCommandSignatureReady = false;
        bool clusterGpuCullReady = false;
        bool clusterGpuCullDrawArgsReady = false;
        bool clusterGpuCullCommandSignatureReady = false;
        bool clusterGpuCullCounterReadbackReady = false;
        bool clusterGpuCullCounterReadbackValid = false;
        bool clusterGpuCullDebugCountersEnabled = false;
        bool clusterDrawPipelineReady = false;
        bool clusterDrawForwardPipelineReady = false;
        bool clusterDrawGeometryBufferPipelineReady = false;
        bool clusterDrawArgumentBufferReady = false;
        bool clusterDrawCommandSignatureReady = false;
        bool meshletBackendInitialized = false;
        bool meshletBackendShaderModel65Supported = false;
        bool meshletBackendMeshShaderSupported = false;
        bool meshletBackendPipelineStatsSupported = false;
        bool meshletBackendShaderCompileReady = false;
        bool meshletBackendForwardPipelineReady = false;
        bool meshletBackendGeometryBufferPipelineReady = false;
        bool meshletBackendPipelineReady = false;
        bool directionalEnabled = false;
        float directionalIntensity = 0.0f;
        float ambientIntensity = 0.0f;
        size_t pointLightTotalCount = 0;
        size_t pointLightUploadedCount = 0;
        size_t pointLightClampedCount = 0;
        float specularIntensity = 0.0f;
        float specularPower = 0.0f;
    };

} // namespace HIKARI::MESHRENDERER
