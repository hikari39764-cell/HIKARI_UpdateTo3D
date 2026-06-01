#pragma once

#include <cstdint>
#include <string>

#include <d3d12.h>
#include <dxgiformat.h>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::REFLECTION {

    struct ReflectionProbeRuntimeData {
        bool enabled = false;
        bool valid = false;
        bool hasPrefiltered = false;
        bool hasBrdfLut = false;

        int prefilteredHandle = -1;
        int brdfLutHandle = -1;

        D3D12_GPU_DESCRIPTOR_HANDLE prefilteredSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE brdfLutSrv{};

        uint32_t prefilteredMipCount = 1;
        uint32_t prefilteredActualMipCount = 0;
        uint32_t brdfLutMipCount = 0;
        DXGI_FORMAT prefilteredFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT brdfLutFormat = DXGI_FORMAT_UNKNOWN;
        bool prefilteredMipMismatch = false;

        MATH::Vec3 position{ 0.0f, 2.0f, 0.0f };
        float radius = 8.0f;
        float intensity = 1.0f;

        std::string sourceAssetId{};
        std::string prefilteredPath{};
        std::string brdfLutPath{};
    };

    void Reset();

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
        std::string brdfLutPath);

    const ReflectionProbeRuntimeData& GetActiveProbe();

    D3D12_GPU_DESCRIPTOR_HANDLE GetPrefilteredSrv();
    D3D12_GPU_DESCRIPTOR_HANDLE GetBrdfLutSrv();

} // namespace HIKARI::REFLECTION
