#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "HIKARI_DxTexture.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::REFLECTION {

    namespace {
        ReflectionProbeRuntimeData gData{};

        struct ReflectionProbeStateKey {
            bool enabled = false;
            bool valid = false;
            bool hasPrefiltered = false;
            bool hasBrdfLut = false;
            int prefilteredHandle = -1;
            int brdfLutHandle = -1;
            uint32_t prefilteredMipCount = 1;
            uint32_t prefilteredActualMipCount = 0;
            uint32_t brdfLutMipCount = 0;
            DXGI_FORMAT prefilteredFormat = DXGI_FORMAT_UNKNOWN;
            DXGI_FORMAT brdfLutFormat = DXGI_FORMAT_UNKNOWN;
            bool prefilteredMipMismatch = false;
            std::string sourceAssetId{};
            std::string prefilteredPath{};
            std::string brdfLutPath{};
            MATH::Vec3 position{};
            float radius = 0.0f;
            float intensity = 0.0f;
        };

        bool gHasLastKey = false;
        ReflectionProbeStateKey gLastKey{};
        uint32_t gRequestedMipCount = 1;
        int gSamplingSuppressDepth = 0;

        bool operator==(const ReflectionProbeStateKey& lhs, const ReflectionProbeStateKey& rhs) {
            return lhs.enabled == rhs.enabled &&
                lhs.valid == rhs.valid &&
                lhs.hasPrefiltered == rhs.hasPrefiltered &&
                lhs.hasBrdfLut == rhs.hasBrdfLut &&
                lhs.prefilteredHandle == rhs.prefilteredHandle &&
                lhs.brdfLutHandle == rhs.brdfLutHandle &&
                lhs.prefilteredMipCount == rhs.prefilteredMipCount &&
                lhs.prefilteredActualMipCount == rhs.prefilteredActualMipCount &&
                lhs.brdfLutMipCount == rhs.brdfLutMipCount &&
                lhs.prefilteredFormat == rhs.prefilteredFormat &&
                lhs.brdfLutFormat == rhs.brdfLutFormat &&
                lhs.prefilteredMipMismatch == rhs.prefilteredMipMismatch &&
                lhs.sourceAssetId == rhs.sourceAssetId &&
                lhs.prefilteredPath == rhs.prefilteredPath &&
                lhs.brdfLutPath == rhs.brdfLutPath &&
                lhs.position.x == rhs.position.x &&
                lhs.position.y == rhs.position.y &&
                lhs.position.z == rhs.position.z &&
                lhs.radius == rhs.radius &&
                lhs.intensity == rhs.intensity;
        }

        const char* BoolText(bool value) {
            return value ? "true" : "false";
        }

        bool IsValidSrv(D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            return handle.ptr != 0;
        }

        ReflectionProbeStateKey MakeStateKey() {
            ReflectionProbeStateKey key{};
            key.enabled = gData.enabled;
            key.valid = gData.valid;
            key.hasPrefiltered = gData.hasPrefiltered;
            key.hasBrdfLut = gData.hasBrdfLut;
            key.prefilteredHandle = gData.prefilteredHandle;
            key.brdfLutHandle = gData.brdfLutHandle;
            key.prefilteredMipCount = gData.prefilteredMipCount;
            key.prefilteredActualMipCount = gData.prefilteredActualMipCount;
            key.brdfLutMipCount = gData.brdfLutMipCount;
            key.prefilteredFormat = gData.prefilteredFormat;
            key.brdfLutFormat = gData.brdfLutFormat;
            key.prefilteredMipMismatch = gData.prefilteredMipMismatch;
            key.sourceAssetId = gData.sourceAssetId;
            key.prefilteredPath = gData.prefilteredPath;
            key.brdfLutPath = gData.brdfLutPath;
            key.position = gData.position;
            key.radius = gData.radius;
            key.intensity = gData.intensity;
            return key;
        }

        void RefreshResolvedHandles() {
            gData.prefilteredSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(gData.prefilteredHandle);
            gData.brdfLutSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(gData.brdfLutHandle);

            gData.prefilteredActualMipCount = DXTEX::DxTextureManager::GetTextureMipCount(gData.prefilteredHandle);
            gData.brdfLutMipCount = DXTEX::DxTextureManager::GetTextureMipCount(gData.brdfLutHandle);
            gData.prefilteredFormat = DXTEX::DxTextureManager::GetTextureFormat(gData.prefilteredHandle);
            gData.brdfLutFormat = DXTEX::DxTextureManager::GetTextureFormat(gData.brdfLutHandle);

            // Probe は prefiltered cubemap かつ mip 2 以上の時だけ有効にする。
            gData.hasPrefiltered = IsValidSrv(gData.prefilteredSrv) &&
                DXTEX::DxTextureManager::GetTextureDimension(gData.prefilteredHandle) == DXTEX::TextureDimension::TextureCube &&
                gData.prefilteredActualMipCount >= 2;
            gData.hasBrdfLut = IsValidSrv(gData.brdfLutSrv) &&
                DXTEX::DxTextureManager::GetTextureDimension(gData.brdfLutHandle) == DXTEX::TextureDimension::Texture2D &&
                gData.brdfLutMipCount >= 1;

            const uint32_t requestedMipCount = std::max<uint32_t>(1u, gRequestedMipCount);
            if (gData.hasPrefiltered) {
                gData.prefilteredMipCount = std::max<uint32_t>(
                    1u,
                    std::min<uint32_t>(requestedMipCount, gData.prefilteredActualMipCount));
                gData.prefilteredMipMismatch = requestedMipCount != gData.prefilteredActualMipCount;
            }
            else {
                gData.prefilteredMipCount = 1;
                gData.prefilteredMipMismatch = false;
            }

            gData.valid = gData.enabled && gData.hasPrefiltered && gData.radius > 0.001f && gData.intensity > 0.0f;
        }

        void LogStateIfChanged() {
            const ReflectionProbeStateKey key = MakeStateKey();
            if (gHasLastKey && key == gLastKey) {
                return;
            }

            gLastKey = key;
            gHasLastKey = true;

            std::ostringstream oss;
            oss << "[Environment][ReflectionProbe] state changed"
                << " enabled=" << BoolText(gData.enabled)
                << " valid=" << BoolText(gData.valid)
                << " prefiltered=" << BoolText(gData.hasPrefiltered)
                << " brdf=" << BoolText(gData.hasBrdfLut)
                << " handles=" << gData.prefilteredHandle << "/" << gData.brdfLutHandle
                << " mips=" << gData.prefilteredMipCount
                << " actualMips=" << gData.prefilteredActualMipCount << "/" << gData.brdfLutMipCount
                << " formats=" << GFX::FormatToString(gData.prefilteredFormat) << "/"
                << GFX::FormatToString(gData.brdfLutFormat)
                << " position=" << gData.position.x << "," << gData.position.y << "," << gData.position.z
                << " radius=" << gData.radius
                << " intensity=" << gData.intensity
                << " source=" << gData.sourceAssetId
                << " prefilteredPath=" << gData.prefilteredPath
                << " brdfPath=" << gData.brdfLutPath;
            HIKARI_LOG_INFO(oss.str());

            if (gData.enabled && !gData.hasPrefiltered) {
                HIKARI_LOG_WARN("[Environment][ReflectionProbe] prefiltered cubemap is invalid or missing.");
            }
            if (gData.prefilteredMipMismatch) {
                HIKARI_LOG_WARN(
                    "[Environment][ReflectionProbe] prefiltered mip count mismatch. requested=" +
                    std::to_string(std::max<uint32_t>(1u, gRequestedMipCount)) +
                    " actual=" + std::to_string(gData.prefilteredActualMipCount));
            }
        }
    }

    void Reset() {
        gData = {};
        gRequestedMipCount = 1;
        RefreshResolvedHandles();
        LogStateIfChanged();
    }

    void SetActiveProbe(
        bool enabled,
        int prefilteredHandle,
        int brdfLutHandle,
        uint32_t prefilteredMipCount,
        const MATH::Vec3& position,
        float radius,
        float intensity,
        std::string sourceAssetId,
        std::string prefilteredPath,
        std::string brdfLutPath) {

        gData.enabled = enabled;
        gData.prefilteredHandle = prefilteredHandle;
        gData.brdfLutHandle = brdfLutHandle;
        gRequestedMipCount = std::max<uint32_t>(1u, prefilteredMipCount);
        gData.position = position;
        gData.radius = std::max(0.0f, radius);
        gData.intensity = std::max(0.0f, intensity);
        gData.sourceAssetId = std::move(sourceAssetId);
        gData.prefilteredPath = std::move(prefilteredPath);
        gData.brdfLutPath = std::move(brdfLutPath);

        RefreshResolvedHandles();
        LogStateIfChanged();
    }

    const ReflectionProbeRuntimeData& GetActiveProbe() {
        RefreshResolvedHandles();
        LogStateIfChanged();
        return gData;
    }

    bool IsReflectionProbeSamplingSuppressed() {
        return gSamplingSuppressDepth > 0;
    }

    ScopedReflectionProbeSamplingSuppress::ScopedReflectionProbeSamplingSuppress() {
        ++gSamplingSuppressDepth;
    }

    ScopedReflectionProbeSamplingSuppress::~ScopedReflectionProbeSamplingSuppress() {
        gSamplingSuppressDepth = std::max(0, gSamplingSuppressDepth - 1);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetPrefilteredSrv() {
        if (IsReflectionProbeSamplingSuppressed()) {
            return {};
        }
        RefreshResolvedHandles();
        LogStateIfChanged();
        return gData.prefilteredSrv;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetBrdfLutSrv() {
        if (IsReflectionProbeSamplingSuppressed()) {
            return {};
        }
        RefreshResolvedHandles();
        LogStateIfChanged();
        return gData.brdfLutSrv;
    }

} // namespace HIKARI::REFLECTION
