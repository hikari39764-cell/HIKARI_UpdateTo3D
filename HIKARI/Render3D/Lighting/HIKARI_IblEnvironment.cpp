#include "Render3D/Lighting/HIKARI_IblEnvironment.h"

#include <algorithm>
#include <sstream>

#include "Core/HIKARI_Logger.h"
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
        };

        bool gHasLastIblKey = false;
        IblStateKey gLastIblKey{};

        bool operator==(const IblStateKey& lhs, const IblStateKey& rhs) {
            return lhs.valid == rhs.valid &&
                lhs.hasIrradiance == rhs.hasIrradiance &&
                lhs.hasPrefiltered == rhs.hasPrefiltered &&
                lhs.hasBrdfLut == rhs.hasBrdfLut &&
                lhs.irradianceHandle == rhs.irradianceHandle &&
                lhs.prefilteredHandle == rhs.prefilteredHandle &&
                lhs.brdfLutHandle == rhs.brdfLutHandle &&
                lhs.prefilteredMipCount == rhs.prefilteredMipCount;
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

            gData.hasIrradiance = IsValidSrv(gData.irradianceSrv) &&
                DXTEX::DxTextureManager::GetTextureDimension(gData.irradianceHandle) == DXTEX::TextureDimension::TextureCube;
            gData.hasPrefiltered = IsValidSrv(gData.prefilteredSrv) &&
                DXTEX::DxTextureManager::GetTextureDimension(gData.prefilteredHandle) == DXTEX::TextureDimension::TextureCube;
            gData.hasBrdfLut = IsValidSrv(gData.brdfLutSrv);
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
                << " mips=" << gData.prefilteredMipCount;
            HIKARI_LOG_INFO(oss.str());

            if (gData.irradianceHandle >= 0 && gData.irradianceSrv.ptr == 0) {
                HIKARI_LOG_WARN("[Environment][IBL] irradiance handle has no SRV.");
            }
            if (gData.prefilteredHandle >= 0 && gData.prefilteredSrv.ptr == 0) {
                HIKARI_LOG_WARN("[Environment][IBL] prefiltered handle has no SRV.");
            }
            if (gData.brdfLutHandle >= 0 && gData.brdfLutSrv.ptr == 0) {
                HIKARI_LOG_WARN("[Environment][IBL] BRDF LUT handle has no SRV.");
            }
        }
    }

    void Reset() {
        gData = {};
        RefreshResolvedHandles();
        LogIblStateIfChanged();
    }

    void SetFromTextureHandles(
        int irradianceHandle,
        int prefilteredHandle,
        int brdfLutHandle,
        uint32_t prefilteredMipCount) {
        gData.irradianceHandle = irradianceHandle;
        gData.prefilteredHandle = prefilteredHandle;
        gData.brdfLutHandle = brdfLutHandle;
        gData.prefilteredMipCount = std::max<uint32_t>(1u, prefilteredMipCount);
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
