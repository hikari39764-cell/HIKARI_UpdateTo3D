#pragma once

#include <cstdint>
#include <d3d12.h>

namespace HIKARI::IBL {

    struct IblEnvironmentData {
        bool valid = false;
        bool hasIrradiance = false;
        bool hasPrefiltered = false;
        bool hasBrdfLut = false;

        int irradianceHandle = -1;
        int prefilteredHandle = -1;
        int brdfLutHandle = -1;

        D3D12_GPU_DESCRIPTOR_HANDLE irradianceSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE prefilteredSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE brdfLutSrv{};

        uint32_t prefilteredMipCount = 1;
    };

    void Reset();

    void SetFromTextureHandles(
        int irradianceHandle,
        int prefilteredHandle,
        int brdfLutHandle,
        uint32_t prefilteredMipCount);

    const IblEnvironmentData& GetEnvironmentData();

    D3D12_GPU_DESCRIPTOR_HANDLE GetIrradianceSrv();
    D3D12_GPU_DESCRIPTOR_HANDLE GetPrefilteredSrv();
    D3D12_GPU_DESCRIPTOR_HANDLE GetBrdfLutSrv();

} // namespace HIKARI::IBL
