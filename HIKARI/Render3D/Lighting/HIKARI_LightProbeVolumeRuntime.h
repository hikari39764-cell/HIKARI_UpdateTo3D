#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <d3d12.h>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::LIGHTPROBE {

    struct Sh9Color {
        MATH::Vec3 coeffs[9]{};
    };

    struct LightProbeVolumeRuntimeData {
        bool enabled = false;
        bool valid = false;
        MATH::Vec3 origin{};
        MATH::Vec3 size{};
        MATH::Vec3 spacing{};
        uint32_t countX = 0;
        uint32_t countY = 0;
        uint32_t countZ = 0;
        uint32_t probeCount = 0;
        float intensity = 1.0f;
        std::string sourcePath{};
    };

    void Reset();

    bool LoadLightProbeVolume(
        const std::filesystem::path& path,
        std::string* outMessage = nullptr);

    void SetLightProbeVolumeEnabled(bool enabled);
    void SetLightProbeVolumeIntensity(float intensity);

    const LightProbeVolumeRuntimeData& GetRuntimeData();

    bool IsLightProbeVolumeSamplingSuppressed();

    class ScopedLightProbeVolumeSamplingSuppress {
    public:
        ScopedLightProbeVolumeSamplingSuppress();
        ~ScopedLightProbeVolumeSamplingSuppress();

        ScopedLightProbeVolumeSamplingSuppress(const ScopedLightProbeVolumeSamplingSuppress&) = delete;
        ScopedLightProbeVolumeSamplingSuppress& operator=(const ScopedLightProbeVolumeSamplingSuppress&) = delete;
    };

    D3D12_GPU_DESCRIPTOR_HANDLE GetShBufferSrv();

    bool IsValid();

} // namespace HIKARI::RENDER3D::LIGHTPROBE
