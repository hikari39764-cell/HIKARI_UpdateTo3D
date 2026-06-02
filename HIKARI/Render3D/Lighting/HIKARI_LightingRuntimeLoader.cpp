#include "HIKARI_LightingRuntimeLoader.h"

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Core/HIKARI_Logger.h"
#include "HIKARI_DxTexture.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"

namespace HIKARI::RENDER3D::LIGHTING {

    namespace {
        constexpr const char* kSharedBrdfLutPath = "Library/Generated/IBL/brdf_lut.dds";

        SceneLightingRuntimeData gLastLightingRuntimeData{};

        void AppendMessage(std::vector<std::string>& messages, const std::string& message) {
            messages.push_back(message);
        }

        bool HasAnyRuntimeResource(const SceneLightingRuntimeData& data) {
            return data.skyLoaded ||
                data.globalIblLoaded ||
                data.reflectionProbeLoaded ||
                data.lightProbeVolumeLoaded;
        }

        std::string ResolveSharedBrdfPath(const std::string& manifestBrdfPath) {
            return manifestBrdfPath.empty()
                ? std::string{ kSharedBrdfLutPath }
                : manifestBrdfPath;
        }
    } // namespace

    const char* ToString(LightingRuntimeSource source) {
        switch (source) {
        case LightingRuntimeSource::None:
            return "None";
        case LightingRuntimeSource::AuthoringFallback:
            return "AuthoringFallback";
        case LightingRuntimeSource::BakeManifestDiscovered:
            return "BakeManifestDiscovered";
        case LightingRuntimeSource::BakedRuntime:
            return "BakedRuntime";
        default:
            return "Unknown";
        }
    }

    const SceneLightingRuntimeData& GetLastLightingRuntimeData() {
        return gLastLightingRuntimeData;
    }

    void SetLastLightingRuntimeData(const SceneLightingRuntimeData& data) {
        gLastLightingRuntimeData = data;
    }

    LightingRuntimeLoadResult LightingRuntimeLoader::Load(
        const LightingRuntimeLoadRequest& request,
        const AssetRegistry& assetRegistry,
        SkyManager& skyManager) const {

        LightingRuntimeLoadResult result{};
        result.runtimeData.source = LightingRuntimeSource::None;
        LIGHTPROBE::Reset();

        const bool manifestLoaded = request.preferBakeManifest
            ? TryLoadBakeManifest(request, result.runtimeData, result.messages)
            : false;

        // Sky/IBL は authoring asset から読み込む。
        LoadSkyLightingFromAssets(
            request,
            assetRegistry,
            skyManager,
            result.runtimeData,
            result.messages);

        if (!result.runtimeData.reflectionProbeLoaded) {
            LoadReflectionProbeFromAuthoringSource(
                request,
                assetRegistry,
                result.runtimeData,
                result.messages);
        }

        if (result.runtimeData.reflectionProbeLoaded &&
            result.runtimeData.source != LightingRuntimeSource::BakedRuntime) {
            result.runtimeData.source = LightingRuntimeSource::AuthoringFallback;
        } else if (!result.runtimeData.reflectionProbeLoaded &&
            !result.runtimeData.lightProbeVolumeLoaded &&
            manifestLoaded) {
            result.runtimeData.source = LightingRuntimeSource::BakeManifestDiscovered;
        } else if (result.runtimeData.source == LightingRuntimeSource::None &&
            HasAnyRuntimeResource(result.runtimeData)) {
            result.runtimeData.source = LightingRuntimeSource::AuthoringFallback;
        }

        SetLastLightingRuntimeData(result.runtimeData);
        return result;
    }

    bool LightingRuntimeLoader::TryLoadBakeManifest(
        const LightingRuntimeLoadRequest& request,
        SceneLightingRuntimeData& runtimeData,
        std::vector<std::string>& messages) const {

        if (request.sceneGuid.empty() || request.projectRoot.empty()) {
            return false;
        }

        const std::filesystem::path manifestPath =
            ASSETS::LIGHTING::BuildLightingBakeManifestPath(
                request.projectRoot,
                request.sceneGuid);

        runtimeData.bakeManifestPath = manifestPath.generic_string();

        std::string message{};
        ASSETS::LIGHTING::LightingBakeManifest manifest{};
        if (!ASSETS::LIGHTING::LoadLightingBakeManifest(manifestPath, manifest, &message)) {
            AppendMessage(messages, "[LightingRuntimeLoader][BakeManifest] " + message);
            return false;
        }

        runtimeData.bakeManifestLoaded = true;
        runtimeData.bakedReflectionProbeCount =
            static_cast<uint32_t>(manifest.reflectionProbes.size());
        runtimeData.bakedLightProbeCount =
            static_cast<uint32_t>(manifest.lightProbes.size());
        runtimeData.bakedLightmapCount =
            static_cast<uint32_t>(manifest.lightmaps.size());

        HIKARI_LOG_INFO("[LightingRuntimeLoader][BakeManifest] loaded manifest=" +
            runtimeData.bakeManifestPath +
            " probes=" + std::to_string(runtimeData.bakedReflectionProbeCount) +
            " lightProbes=" + std::to_string(runtimeData.bakedLightProbeCount) +
            " lightmaps=" + std::to_string(runtimeData.bakedLightmapCount));

        if (request.lightProbeVolumeEnabled && !manifest.lightProbes.empty()) {
            const ASSETS::LIGHTING::LightProbeBakeRecord* volumeRecord = nullptr;
            for (const ASSETS::LIGHTING::LightProbeBakeRecord& record : manifest.lightProbes) {
                if (record.type.empty() || record.type == "VolumeGrid") {
                    volumeRecord = &record;
                    break;
                }
            }

            if (volumeRecord != nullptr && !volumeRecord->shDataPath.empty()) {
                const std::filesystem::path shPath =
                    volumeRecord->shDataPath.empty()
                        ? std::filesystem::path{}
                        : (request.projectRoot / volumeRecord->shDataPath).lexically_normal();
                std::string lightProbeMessage{};
                if (LIGHTPROBE::LoadLightProbeVolume(shPath, &lightProbeMessage)) {
                    LIGHTPROBE::SetLightProbeVolumeIntensity(request.lightProbeVolumeIntensity);
                    runtimeData.lightProbeVolumeLoaded = LIGHTPROBE::IsValid();
                    runtimeData.activeLightProbeVolumePath = volumeRecord->shDataPath;
                    runtimeData.bakedLightProbeCount = volumeRecord->probeCount > 0
                        ? volumeRecord->probeCount
                        : runtimeData.bakedLightProbeCount;
                    runtimeData.source = LightingRuntimeSource::BakedRuntime;
                    AppendMessage(messages, "[LightingRuntimeLoader][LightProbe] " + lightProbeMessage);
                    HIKARI_LOG_INFO("[LightingRuntimeLoader][LightProbe] loaded valid=true probes=" +
                        std::to_string(runtimeData.bakedLightProbeCount) +
                        " path=" + volumeRecord->shDataPath);
                } else {
                    LIGHTPROBE::Reset();
                    AppendMessage(messages, "[LightingRuntimeLoader][LightProbe][WARN] " + lightProbeMessage);
                    HIKARI_LOG_WARN("[LightingRuntimeLoader][LightProbe][WARN] failed to load HLPV path=" +
                        volumeRecord->shDataPath +
                        " message=" + lightProbeMessage);
                }
            } else {
                AppendMessage(messages, "[LightingRuntimeLoader][LightProbe] volume grid record not found.");
            }
        } else if (!request.lightProbeVolumeEnabled) {
            LIGHTPROBE::SetLightProbeVolumeEnabled(false);
        }

        if (!manifest.reflectionProbes.empty()) {
            const ASSETS::LIGHTING::ReflectionProbeBakeRecord& record =
                manifest.reflectionProbes.front();

            if (record.prefilteredCubemapPath.empty()) {
                AppendMessage(messages, "[LightingRuntimeLoader][BakeManifest] baked probe has no prefiltered cubemap.");
                if (!runtimeData.lightProbeVolumeLoaded) {
                    runtimeData.source = LightingRuntimeSource::BakeManifestDiscovered;
                }
                return true;
            }

            const int prefiltered = DXTEX::DxTextureManager::LoadCubemap(
                "reflection_probe/baked/" + record.id,
                record.prefilteredCubemapPath,
                DXTEX::TextureColorSpace::Linear);

            const std::string brdfLutPath = ResolveSharedBrdfPath(record.brdfLutPath);
            const int brdf = DXTEX::DxTextureManager::LoadTextureLinear(
                "reflection_probe/brdf_lut",
                brdfLutPath);

            // Bake manifest の probe を runtime probe として優先する。
            REFLECTION::SetActiveProbe(
                true,
                prefiltered,
                brdf,
                record.prefilteredMipCount,
                record.position,
                record.radius,
                record.intensity,
                record.id,
                record.prefilteredCubemapPath,
                brdfLutPath);

            runtimeData.reflectionProbeLoaded = REFLECTION::GetActiveProbe().valid;
            runtimeData.activeReflectionProbeSourceAssetId = record.id;
            if (runtimeData.reflectionProbeLoaded) {
                runtimeData.source = LightingRuntimeSource::BakedRuntime;
            } else if (!runtimeData.lightProbeVolumeLoaded) {
                runtimeData.source = LightingRuntimeSource::BakeManifestDiscovered;
            }

            if (runtimeData.reflectionProbeLoaded) {
                HIKARI_LOG_INFO("[LightingRuntimeLoader][ReflectionProbe] baked probe activation valid=true source=BakedRuntime path=" +
                    record.prefilteredCubemapPath +
                    " prefiltered=" + std::to_string(prefiltered) +
                    " brdf=" + std::to_string(brdf));
            } else {
                HIKARI_LOG_WARN("[LightingRuntimeLoader][ReflectionProbe][WARN] baked probe invalid, fallback to authoring source. path=" +
                    record.prefilteredCubemapPath +
                    " prefiltered=" + std::to_string(prefiltered) +
                    " brdf=" + std::to_string(brdf));
            }
        } else if (!runtimeData.lightProbeVolumeLoaded) {
            runtimeData.source = LightingRuntimeSource::BakeManifestDiscovered;
        }

        return true;
    }

    void LightingRuntimeLoader::LoadSkyLightingFromAssets(
        const LightingRuntimeLoadRequest& request,
        const AssetRegistry& assetRegistry,
        SkyManager& skyManager,
        SceneLightingRuntimeData& runtimeData,
        std::vector<std::string>& messages) const {

        if (request.skyAssetIds.empty()) {
            IBL::Reset();
            return;
        }

        for (const std::string& skyId : request.skyAssetIds) {
            const auto* descriptor = assetRegistry.FindAs<SkyAssetDescriptor>(AssetId{ skyId });
            if (!descriptor) {
                IBL::Reset();
                HIKARI_LOG_WARN("[LightingRuntimeLoader][Sky] sky descriptor missing: " + skyId);
                AppendMessage(messages, "[LightingRuntimeLoader][Sky] descriptor missing: " + skyId);
                continue;
            }

            std::string texturePath = descriptor->sourcePath;
            if (!descriptor->textureAssetId.empty()) {
                if (const auto* texture = assetRegistry.FindAs<TextureAssetDescriptor>(
                        AssetId{ descriptor->textureAssetId })) {
                    texturePath = texture->sourcePath;
                }
            }

            skyManager.RegisterOrUpdateAsset(SkyAsset{
                descriptor->id.value,
                descriptor->meshAssetId,
                texturePath,
                descriptor->preferredMode
            });
            runtimeData.skyLoaded = true;
            runtimeData.activeSkyAssetId = descriptor->id.value;

            if (!descriptor->hasIbl) {
                IBL::Reset();
                AppendMessage(messages, "[LightingRuntimeLoader][IBL] sky has no IBL artifacts: " + descriptor->id.value);
                continue;
            }

            int irradiance = -1;
            int prefiltered = -1;
            int brdf = -1;
            if (!descriptor->irradiancePath.empty()) {
                irradiance = DXTEX::DxTextureManager::LoadCubemap(
                    "ibl/irradiance/" + descriptor->id.value,
                    descriptor->irradiancePath,
                    DXTEX::TextureColorSpace::Linear);
            }
            if (!descriptor->prefilteredPath.empty()) {
                prefiltered = DXTEX::DxTextureManager::LoadCubemap(
                    "ibl/prefiltered/" + descriptor->id.value,
                    descriptor->prefilteredPath,
                    DXTEX::TextureColorSpace::Linear);
            }
            const std::string brdfLutPath = descriptor->brdfLutPath.empty()
                ? std::string{ kSharedBrdfLutPath }
                : descriptor->brdfLutPath;
            if (!brdfLutPath.empty()) {
                brdf = DXTEX::DxTextureManager::LoadTextureLinear(
                    "ibl/brdf_lut",
                    brdfLutPath);
            }

            IBL::SetFromTextureHandles(
                irradiance,
                prefiltered,
                brdf,
                descriptor->prefilteredMipCount);

            runtimeData.globalIblLoaded = irradiance >= 0 || prefiltered >= 0 || brdf >= 0;
            HIKARI_LOG_INFO("[LightingRuntimeLoader][IBL] loaded sky=" + descriptor->id.value +
                " irradiance=" + std::to_string(irradiance) +
                " prefiltered=" + std::to_string(prefiltered) +
                " brdf=" + std::to_string(brdf));
        }
    }

    void LightingRuntimeLoader::LoadReflectionProbeFromAuthoringSource(
        const LightingRuntimeLoadRequest& request,
        const AssetRegistry& assetRegistry,
        SceneLightingRuntimeData& runtimeData,
        std::vector<std::string>& messages) const {

        if (!request.reflectionProbeEnabled ||
            request.reflectionProbeCubemapAssetIds.empty() ||
            !request.allowAuthoringReflectionProbeFallback) {
            REFLECTION::Reset();
            return;
        }

        const std::string probeAssetId = *request.reflectionProbeCubemapAssetIds.begin();
        const auto* probeDescriptor = assetRegistry.FindAs<SkyAssetDescriptor>(AssetId{ probeAssetId });
        if (!probeDescriptor) {
            REFLECTION::Reset();
            HIKARI_LOG_WARN("[LightingRuntimeLoader][ReflectionProbe] source asset missing: " + probeAssetId);
            AppendMessage(messages, "[LightingRuntimeLoader][ReflectionProbe] source missing: " + probeAssetId);
            return;
        }

        int probePrefiltered = -1;
        int probeBrdf = -1;
        if (!probeDescriptor->prefilteredPath.empty()) {
            probePrefiltered = DXTEX::DxTextureManager::LoadCubemap(
                "reflection_probe/prefiltered/" + probeDescriptor->id.value,
                probeDescriptor->prefilteredPath,
                DXTEX::TextureColorSpace::Linear);
        }
        const std::string probeBrdfPath = probeDescriptor->brdfLutPath.empty()
            ? std::string{ kSharedBrdfLutPath }
            : probeDescriptor->brdfLutPath;
        if (!probeBrdfPath.empty()) {
            probeBrdf = DXTEX::DxTextureManager::LoadTextureLinear(
                "reflection_probe/brdf_lut",
                probeBrdfPath);
        }

        // 未 bake 時だけ authoring cubemap を fallback として使う。
        REFLECTION::SetActiveProbe(
            request.reflectionProbeEnabled,
            probePrefiltered,
            probeBrdf,
            probeDescriptor->prefilteredMipCount,
            request.reflectionProbePosition,
            request.reflectionProbeRadius,
            request.reflectionProbeIntensity,
            probeDescriptor->id.value,
            probeDescriptor->prefilteredPath,
            probeBrdfPath);

        runtimeData.reflectionProbeLoaded = REFLECTION::GetActiveProbe().valid;
        runtimeData.activeReflectionProbeSourceAssetId = probeDescriptor->id.value;
        HIKARI_LOG_INFO("[LightingRuntimeLoader][ReflectionProbe] loaded authoring fallback source=" +
            probeDescriptor->id.value +
            " prefiltered=" + std::to_string(probePrefiltered) +
            " brdf=" + std::to_string(probeBrdf) +
            " valid=" + std::string(runtimeData.reflectionProbeLoaded ? "true" : "false"));
    }

} // namespace HIKARI::RENDER3D::LIGHTING
