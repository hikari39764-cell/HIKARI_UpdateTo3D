#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace HIKARI {

    struct SceneEnvironment;

    namespace RENDER3D::DIAGNOSTICS {

        struct EnvironmentDiagnosticsSnapshot {
            bool skyRendererInitialized = false;
            bool skyLastRenderSubmitted = false;
            bool skyAssetFound = false;
            bool skyCubemapLoaded = false;
            bool skyUsingFallback = false;
            bool skyTextureValid = false;
            int skyCubemapHandle = -1;
            int skyTextureHandle = -1;
            size_t skyDrawCount = 0;
            std::string activeSkyAsset{};
            std::string activeSkyTexturePath{};

            bool iblValid = false;
            bool iblHasIrradiance = false;
            bool iblHasPrefiltered = false;
            bool iblHasBrdfLut = false;
            int irradianceHandle = -1;
            int prefilteredHandle = -1;
            int brdfLutHandle = -1;
            uint32_t prefilteredMipCount = 1;

            bool shadowEnabled = false;
            uint32_t shadowResolution = 0;
            size_t shadowSubmittedCasterCount = 0;
            size_t shadowStaticCasterDrawCount = 0;
            size_t shadowSkinnedCasterDrawCount = 0;
            size_t shadowAlphaMaskCasterDrawCount = 0;
            float shadowStrength = 0.0f;
            float shadowDepthBias = 0.0f;
            float shadowNormalBias = 0.0f;

            bool bloomEnabled = false;
            bool bloomInitialized = false;
            bool bloomFailed = false;
            uint32_t bloomPassCount = 0;
            int bloomTextureWidth = 0;
            int bloomTextureHeight = 0;
            float bloomThreshold = 0.0f;
            float bloomIntensity = 0.0f;
            float bloomRadius = 0.0f;

            bool toneMappingEnabled = false;
            bool fxaaEnabled = false;
            float fxaaEdgeThreshold = 0.0f;
            float fxaaEdgeThresholdMin = 0.0f;
            float fxaaSubpixelQuality = 0.0f;

            uint32_t recentRenderErrorCount = 0;
        };

        EnvironmentDiagnosticsSnapshot CaptureEnvironmentSnapshot(const SceneEnvironment* environment = nullptr);

        void LogEnvironmentSnapshot(const char* reason, const SceneEnvironment* environment = nullptr);
        void LogEnvironmentSnapshotIfChanged(const char* reason, const SceneEnvironment* environment = nullptr);
        void ResetEnvironmentDiagnosticsChangeCache();

        const char* ResolveSkySummaryLabel(const EnvironmentDiagnosticsSnapshot& snapshot);
        const char* ResolveIblSummaryLabel(const EnvironmentDiagnosticsSnapshot& snapshot);

    } // namespace RENDER3D::DIAGNOSTICS

} // namespace HIKARI
