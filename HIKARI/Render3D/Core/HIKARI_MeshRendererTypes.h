#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
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

    constexpr UINT AlignConstantBufferSize(size_t size) {
        return static_cast<UINT>((size + 255u) & ~255u);
    }

    struct JointPaletteCB {
        MATH::Mat4 jointMatrices[kMaxJointPaletteMatrices]{};
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
