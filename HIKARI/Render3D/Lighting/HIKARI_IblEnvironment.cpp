#include "Render3D/Lighting/HIKARI_IblEnvironment.h"

#include <algorithm>

#include "HIKARI_DxTexture.h"

namespace HIKARI::IBL {

    namespace {
        IblEnvironmentData gData{};

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
    }

    void Reset() {
        gData = {};
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
    }

    const IblEnvironmentData& GetEnvironmentData() {
        RefreshResolvedHandles();
        return gData;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetIrradianceSrv() {
        RefreshResolvedHandles();
        return gData.irradianceSrv;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetPrefilteredSrv() {
        RefreshResolvedHandles();
        return gData.prefilteredSrv;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetBrdfLutSrv() {
        RefreshResolvedHandles();
        return gData.brdfLutSrv;
    }

} // namespace HIKARI::IBL
