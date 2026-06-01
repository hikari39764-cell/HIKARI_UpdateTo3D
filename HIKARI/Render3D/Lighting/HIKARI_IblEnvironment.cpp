#include "Render3D/Lighting/HIKARI_IblEnvironment.h"

#include <algorithm>
#include <sstream>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "HIKARI_DxTexture.h"

namespace HIKARI::IBL {

    namespace {
        IblEnvironmentData gData{};
        struct IblStateKey {
            bool valid = false;
            bool hasIrradiance = false;
            bool hasPrefiltered = false;
            bool hasBrdfLut = false;
            int irradianceHandle = -1;
            int prefilteredHandle = -1;
            int brdfLutHandle = -1;
            uint32_t prefilteredMipCount = 1;
            uint32_t irradianceMipCount = 0;
            uint32_t prefilteredActualMipCount = 0;
            uint32_t brdfLutMipCount = 0;
            DXGI_FORMAT irradianceFormat = DXGI_FORMAT_UNKNOWN;
            DXGI_FORMAT prefilteredFormat = DXGI_FORMAT_UNKNOWN;
            DXGI_FORMAT brdfLutFormat = DXGI_FORMAT_UNKNOWN;
            bool prefilteredMipMismatch = false;
        };

        bool gHasLastIblKey = false;
        IblStateKey gLastIblKey{};
        uint32_t gRequestedPrefilteredMipCount = 1;

        bool operator==(const IblStateKey& lhs, const IblStateKey& rhs) {
            return lhs.valid == rhs.valid &&
                lhs.hasIrradiance == rhs.hasIrradiance &&
                lhs.hasPrefiltered == rhs.hasPrefiltered &&
                lhs.hasBrdfLut == rhs.hasBrdfLut &&
                lhs.irradianceHandle == rhs.irradianceHandle &&
                lhs.prefilteredHandle == rhs.prefilteredHandle &&
                lhs.brdfLutHandle == rhs.brdfLutHandle &&
                lhs.prefilteredMipCount == rhs.prefilteredMipCount &&
                lhs.irradianceMipCount == rhs.irradianceMipCount &&
                lhs.prefilteredActualMipCount == rhs.prefilteredActualMipCount &&
                lhs.brdfLutMipCount == rhs.brdfLutMipCount &&
                lhs.irradianceFormat == rhs.irradianceFormat &&
                lhs.prefilteredFormat == rhs.prefilteredFormat &&
                lhs.brdfLutFormat == rhs.brdfLutFormat &&
                lhs.prefilteredMipMismatch == rhs.prefilteredMipMismatch;
        }

        IblStateKey MakeIblStateKey() {
            IblStateKey key{};
            key.valid = gData.valid;
            key.hasIrradiance = gData.hasIrradiance;
            key.hasPrefiltered = gData.hasPrefiltered;
            key.hasBrdfLut = gData.hasBrdfLut;
            key.irradianceHandle = gData.irradianceHandle;
            key.prefilteredHandle = gData.prefilteredHandle;
            key.brdfLutHandle = gData.brdfLutHandle;
            key.prefilteredMipCount = gData.prefilteredMipCount;
            key.irradianceMipCount = gData.irradianceMipCount;
            key.prefilteredActualMipCount = gData.prefilteredActualMipCount;
            key.brdfLutMipCount = gData.brdfLutMipCount;
            key.irradianceFormat = gData.irradianceFormat;
            key.prefilteredFormat = gData.prefilteredFormat;
            key.brdfLutFormat = gData.brdfLutFormat;
            key.prefilteredMipMismatch = gData.prefilteredMipMismatch;
            return key;
        }

        const char* BoolText(bool value) {
            return value ? "true" : "false";
        }

        D3D12_GPU_DESCRIPTOR_HANDLE ResolveSrv(int handle) {
            return DXTEX::DxTextureManager::GetSrvGpuHandle(handle);
        }

        bool IsValidSrv(D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            return handle.ptr != 0;
        }

        void RefreshResolvedHandles() {
            gData.irradianceSrv = ResolveSrv(gData.irradianceHandle);
            gData.prefilteredSrv = ResolveSrv(gData.prefilteredHandle);
            gData.brdfLutSrv = ResolveSrv(gData.brdfLutHandle);

            gData.irradianceMipCount = DXTEX::DxTextureManager::GetTextureMipCount(gData.irradianceHandle);
            gData.prefilteredActualMipCount = DXTEX::DxTextureManager::GetTextureMipCount(gData.prefilteredHandle);
            gData.brdfLutMipCount = DXTEX::DxTextureManager::GetTextureMipCount(gData.brdfLutHandle);
            gData.irradianceFormat = DXTEX::DxTextureManager::GetTextureFormat(gData.irradianceHandle);
            gData.prefilteredFormat = DXTEX::DxTextureManager::GetTextureFormat(gData.prefilteredHandle);
            gData.brdfLutFormat = DXTEX::DxTextureManager::GetTextureFormat(gData.brdfLutHandle);

            // IBL は SRV だけでなく dimension/mip も満たした時だけ有効にする。
            gData.hasIrradiance = IsValidSrv(gData.irradianceSrv) &&
                DXTEX::DxTextureManager::GetTextureDimension(gData.irradianceHandle) == DXTEX::TextureDimension::TextureCube &&
                gData.irradianceMipCount >= 1;
            gData.hasPrefiltered = IsValidSrv(gData.prefilteredSrv) &&
                DXTEX::DxTextureManager::GetTextureDimension(gData.prefilteredHandle) == DXTEX::TextureDimension::TextureCube &&
                gData.prefilteredActualMipCount >= 2;
            gData.hasBrdfLut = IsValidSrv(gData.brdfLutSrv) &&
                DXTEX::DxTextureManager::GetTextureDimension(gData.brdfLutHandle) == DXTEX::TextureDimension::Texture2D &&
                gData.brdfLutMipCount >= 1;

            const uint32_t requestedMipCount = std::max<uint32_t>(1u, gRequestedPrefilteredMipCount);
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

            gData.valid = gData.hasIrradiance || gData.hasPrefiltered || gData.hasBrdfLut;
        }

        void LogIblStateIfChanged() {
            const IblStateKey key = MakeIblStateKey();
            if (gHasLastIblKey && key == gLastIblKey) {
                return;
            }

            // IBL の状態変化だけを log に出し、毎 frame の spam を避ける。
            gLastIblKey = key;
            gHasLastIblKey = true;

            std::ostringstream oss;
            oss << "[Environment][IBL] state changed"
                << " valid=" << BoolText(gData.valid)
                << " irradiance=" << BoolText(gData.hasIrradiance)
                << " prefiltered=" << BoolText(gData.hasPrefiltered)
                << " brdf=" << BoolText(gData.hasBrdfLut)
                << " handles=" << gData.irradianceHandle << "/" << gData.prefilteredHandle << "/" << gData.brdfLutHandle
                << " mips=" << gData.prefilteredMipCount
                << " actualMips=" << gData.irradianceMipCount << "/" << gData.prefilteredActualMipCount << "/" << gData.brdfLutMipCount
                << " formats=" << GFX::FormatToString(gData.irradianceFormat) << "/"
                << GFX::FormatToString(gData.prefilteredFormat) << "/"
                << GFX::FormatToString(gData.brdfLutFormat)
                << " mipMismatch=" << BoolText(gData.prefilteredMipMismatch);
            HIKARI_LOG_INFO(oss.str());

            if (gData.irradianceHandle >= 0 && !gData.hasIrradiance) {
                HIKARI_LOG_WARN("[Environment][IBL] irradiance is invalid.");
            }
            if (gData.prefilteredHandle >= 0 && !gData.hasPrefiltered) {
                HIKARI_LOG_WARN("[Environment][IBL] prefiltered is invalid.");
            }
            if (gData.brdfLutHandle >= 0 && !gData.hasBrdfLut) {
                HIKARI_LOG_WARN("[Environment][IBL] BRDF LUT is invalid.");
            }
            if (gData.prefilteredMipMismatch) {
                HIKARI_LOG_WARN(
                    "[Environment][IBL] prefiltered mip count mismatch. requested=" +
                    std::to_string(std::max<uint32_t>(1u, gRequestedPrefilteredMipCount)) +
                    " actual=" + std::to_string(gData.prefilteredActualMipCount));
            }
        }
    }

    void Reset() {
        gData = {};
        gRequestedPrefilteredMipCount = 1;
        RefreshResolvedHandles();
    }

    void SetFromTextureHandles(
        int irradianceHandle,
        int prefilteredHandle,
        int brdfLutHandle,
        uint32_t prefilteredMipCount) {
        gData.irradianceHandle = irradianceHandle;
        gData.prefilteredHandle = prefilteredHandle;
        gData.brdfLutHandle = brdfLutHandle;
        gRequestedPrefilteredMipCount = std::max<uint32_t>(1u, prefilteredMipCount);
        RefreshResolvedHandles();
        LogIblStateIfChanged();
    }

    const IblEnvironmentData& GetEnvironmentData() {
        RefreshResolvedHandles();
        LogIblStateIfChanged();
        return gData;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetIrradianceSrv() {
        RefreshResolvedHandles();
        LogIblStateIfChanged();
        return gData.irradianceSrv;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetPrefilteredSrv() {
        RefreshResolvedHandles();
        LogIblStateIfChanged();
        return gData.prefilteredSrv;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetBrdfLutSrv() {
        RefreshResolvedHandles();
        LogIblStateIfChanged();
        return gData.brdfLutSrv;
    }

} // namespace HIKARI::IBL
