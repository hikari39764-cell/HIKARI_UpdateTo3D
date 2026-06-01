#include "HIKARI_EnvironmentDiagnostics.h"

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#include <sstream>

namespace HIKARI::RENDER3D::DIAGNOSTICS {

    namespace {
        const char* BoolText(bool value) {
            return value ? "true" : "false";
        }

        const char* ReasonText(const char* reason) {
            return (reason && reason[0] != '\0') ? reason : "Manual";
        }

        void LogInfoLine(const std::string& message) {
            HIKARI_LOG_INFO(message);
        }

        void LogWarnLine(const std::string& message) {
            HIKARI_LOG_WARN(message);
        }
    }

    EnvironmentDiagnosticsSnapshot CaptureEnvironmentSnapshot(const SceneEnvironment* environment) {
        EnvironmentDiagnosticsSnapshot snapshot{};

        // Sky/IBL の runtime 状態を 1 回分の snapshot として集約する。
        const SKYRENDERER::SkyRendererDebugState& sky = SKYRENDERER::GetDebugState();
        snapshot.skyRendererInitialized = sky.initialized;
        snapshot.skyLastRenderSubmitted = sky.lastRenderSubmitted;
        snapshot.skyAssetFound = sky.skyAssetFound;
        snapshot.skyCubemapLoaded = sky.cubemapLoaded;
        snapshot.skyUsingFallback = sky.usingFallback;
        snapshot.skyTextureValid = sky.textureValid;
        snapshot.skyCubemapHandle = sky.cubemapHandle;
        snapshot.skyTextureHandle = sky.textureHandle;
        snapshot.skyDrawCount = sky.drawCount;
        snapshot.activeSkyAsset = sky.activeSkyAsset;
        snapshot.activeSkyTexturePath = sky.activeTexturePath;

        const IBL::IblEnvironmentData& ibl = IBL::GetEnvironmentData();
        snapshot.iblValid = ibl.valid;
        snapshot.iblHasIrradiance = ibl.hasIrradiance;
        snapshot.iblHasPrefiltered = ibl.hasPrefiltered;
        snapshot.iblHasBrdfLut = ibl.hasBrdfLut;
        snapshot.irradianceHandle = ibl.irradianceHandle;
        snapshot.prefilteredHandle = ibl.prefilteredHandle;
        snapshot.brdfLutHandle = ibl.brdfLutHandle;
        snapshot.prefilteredMipCount = ibl.prefilteredMipCount;

        const SHADOW::ShadowMapDebugStats& shadow = SHADOW::GetDebugStats();
        snapshot.shadowEnabled = shadow.enabled && SHADOW::IsDirectionalShadowEnabled();
        snapshot.shadowResolution = shadow.resolution;
        snapshot.shadowSubmittedCasterCount = shadow.submittedCasterCount;
        snapshot.shadowStaticCasterDrawCount = shadow.staticCasterDrawCount;
        snapshot.shadowSkinnedCasterDrawCount = shadow.skinnedCasterDrawCount;
        snapshot.shadowAlphaMaskCasterDrawCount = shadow.alphaMaskCasterDrawCount;
        snapshot.shadowStrength = shadow.strength;
        snapshot.shadowDepthBias = shadow.depthBias;
        snapshot.shadowNormalBias = shadow.normalBias;

        const POST::PostSystem::BloomDebugStats& bloom = POST::PostSystem::GetBloomDebugStats();
        snapshot.bloomEnabled = bloom.enabled;
        snapshot.bloomInitialized = bloom.initialized;
        snapshot.bloomFailed = bloom.failed;
        snapshot.bloomPassCount = bloom.passCount;
        snapshot.bloomTextureWidth = bloom.textureWidth;
        snapshot.bloomTextureHeight = bloom.textureHeight;
        snapshot.bloomThreshold = bloom.threshold;
        snapshot.bloomIntensity = bloom.intensity;
        snapshot.bloomRadius = bloom.radius;

        if (environment) {
            snapshot.toneMappingEnabled = environment->toneMapping.enabled;
        }
        const POST::PostSystem::FxaaSettings& fxaa = POST::PostSystem::GetFxaaSettings();
        snapshot.fxaaEnabled = fxaa.enabled;
        snapshot.fxaaEdgeThreshold = fxaa.edgeThreshold;
        snapshot.fxaaEdgeThresholdMin = fxaa.edgeThresholdMin;
        snapshot.fxaaSubpixelQuality = fxaa.subpixelQuality;

        snapshot.recentRenderErrorCount = static_cast<uint32_t>(DEBUGLOG::GetRecentRenderErrors(64).size());
        return snapshot;
    }

    void LogEnvironmentSnapshot(const char* reason, const SceneEnvironment* environment) {
        const EnvironmentDiagnosticsSnapshot snapshot = CaptureEnvironmentSnapshot(environment);
        const char* safeReason = ReasonText(reason);

        // 手動 dump の時だけ詳細診断を log に出す。
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics] reason=" << safeReason;
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][Sky]"
                << " initialized=" << BoolText(snapshot.skyRendererInitialized)
                << " submitted=" << BoolText(snapshot.skyLastRenderSubmitted)
                << " assetFound=" << BoolText(snapshot.skyAssetFound)
                << " cubemapLoaded=" << BoolText(snapshot.skyCubemapLoaded)
                << " textureValid=" << BoolText(snapshot.skyTextureValid)
                << " fallback=" << BoolText(snapshot.skyUsingFallback)
                << " cubemapHandle=" << snapshot.skyCubemapHandle
                << " textureHandle=" << snapshot.skyTextureHandle
                << " draws=" << snapshot.skyDrawCount
                << " asset=" << snapshot.activeSkyAsset
                << " texture=" << snapshot.activeSkyTexturePath;
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][IBL]"
                << " valid=" << BoolText(snapshot.iblValid)
                << " irradiance=" << BoolText(snapshot.iblHasIrradiance)
                << " prefiltered=" << BoolText(snapshot.iblHasPrefiltered)
                << " brdf=" << BoolText(snapshot.iblHasBrdfLut)
                << " irradianceHandle=" << snapshot.irradianceHandle
                << " prefilteredHandle=" << snapshot.prefilteredHandle
                << " brdfHandle=" << snapshot.brdfLutHandle
                << " mips=" << snapshot.prefilteredMipCount;
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][Shadow]"
                << " enabled=" << BoolText(snapshot.shadowEnabled)
                << " resolution=" << snapshot.shadowResolution
                << " casters=" << snapshot.shadowSubmittedCasterCount
                << " staticDraws=" << snapshot.shadowStaticCasterDrawCount
                << " skinnedDraws=" << snapshot.shadowSkinnedCasterDrawCount
                << " alphaMaskDraws=" << snapshot.shadowAlphaMaskCasterDrawCount
                << " strength=" << snapshot.shadowStrength
                << " depthBias=" << snapshot.shadowDepthBias
                << " normalBias=" << snapshot.shadowNormalBias;
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][Bloom]"
                << " enabled=" << BoolText(snapshot.bloomEnabled)
                << " initialized=" << BoolText(snapshot.bloomInitialized)
                << " failed=" << BoolText(snapshot.bloomFailed)
                << " passes=" << snapshot.bloomPassCount
                << " size=" << snapshot.bloomTextureWidth << "x" << snapshot.bloomTextureHeight
                << " threshold=" << snapshot.bloomThreshold
                << " intensity=" << snapshot.bloomIntensity
                << " radius=" << snapshot.bloomRadius;
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][Post]"
                << " toneMapping=" << BoolText(snapshot.toneMappingEnabled)
                << " fxaa=" << BoolText(snapshot.fxaaEnabled)
                << " fxaaEdge=" << snapshot.fxaaEdgeThreshold
                << " fxaaEdgeMin=" << snapshot.fxaaEdgeThresholdMin
                << " fxaaSubpixel=" << snapshot.fxaaSubpixelQuality;
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][Errors]"
                << " recentRenderErrors=" << snapshot.recentRenderErrorCount;
            LogInfoLine(oss.str());
        }

        if (!snapshot.skyAssetFound && !snapshot.activeSkyAsset.empty()) {
            LogWarnLine("[EnvironmentDiagnostics][Sky] sky asset missing: " + snapshot.activeSkyAsset);
        }
        if (snapshot.skyUsingFallback) {
            LogWarnLine("[EnvironmentDiagnostics][Sky] sky renderer is using fallback.");
        }
        if (!snapshot.iblValid) {
            LogWarnLine("[EnvironmentDiagnostics][IBL] IBL is invalid or incomplete.");
        }
        if (snapshot.bloomFailed) {
            LogWarnLine("[EnvironmentDiagnostics][Bloom] bloom is in failed state.");
        }
        if (snapshot.recentRenderErrorCount > 0) {
            LogWarnLine("[EnvironmentDiagnostics][Errors] recent render errors exist.");
        }
    }

    const char* ResolveSkySummaryLabel(const EnvironmentDiagnosticsSnapshot& snapshot) {
        if (!snapshot.skyRendererInitialized) {
            return "Sky Missing";
        }
        if (!snapshot.skyAssetFound && !snapshot.activeSkyAsset.empty()) {
            return "Sky Missing";
        }
        if (snapshot.skyUsingFallback) {
            return "Sky Fallback";
        }
        if (snapshot.skyCubemapLoaded || snapshot.skyTextureValid) {
            return "Sky Ready";
        }
        return "Sky Pending";
    }

    const char* ResolveIblSummaryLabel(const EnvironmentDiagnosticsSnapshot& snapshot) {
        if (snapshot.iblValid) {
            return "IBL Ready";
        }
        if (snapshot.iblHasIrradiance || snapshot.iblHasPrefiltered || snapshot.iblHasBrdfLut) {
            return "IBL Partial";
        }
        return "IBL Missing";
    }

} // namespace HIKARI::RENDER3D::DIAGNOSTICS
