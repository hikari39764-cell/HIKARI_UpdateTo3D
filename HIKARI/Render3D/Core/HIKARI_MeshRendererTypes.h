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
        float pbrPadding[3]{};
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
            item.resolvedMaterialFxProfile.renderPhase == MaterialFxRenderPhase::SceneDepth;
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
        size_t surfacePacketExecutorGeometryDrawCount = 0;
        size_t surfacePacketExecutorSkippedPacketCount = 0;
        size_t surfacePacketExecutorRunCount = 0;
        size_t surfacePacketExecutorSinglePacketRunCount = 0;
        size_t surfacePacketExecutorMaxRunPacketCount = 0;
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
