#include "HIKARI_LightingBakeService.h"

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Core/HIKARI_Logger.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"
#include "Tools/Baking/HIKARI_ReflectionProbeBaker.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace HIKARI::TOOLS::BAKING {

    namespace {

        void AddError(LightingBakeReport& report, std::string message) {
            report.success = false;
            report.errors.push_back(std::move(message));
        }

        std::string ToLowerGenericPath(std::filesystem::path path) {
            std::string text = path.lexically_normal().generic_string();
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        }

        bool HasPathPrefix(const std::filesystem::path& path, const std::filesystem::path& prefix) {
            const std::string lhs = ToLowerGenericPath(path);
            const std::string rhs = ToLowerGenericPath(prefix);
            if (lhs == rhs) {
                return true;
            }
            if (lhs.size() <= rhs.size()) {
                return false;
            }
            return lhs.compare(0, rhs.size(), rhs) == 0 && lhs[rhs.size()] == '/';
        }

        std::filesystem::path MakeAbsoluteNormalized(const std::filesystem::path& path) {
            std::error_code ec{};
            std::filesystem::path absolute = std::filesystem::absolute(path, ec);
            if (ec) {
                return path.lexically_normal();
            }
            return absolute.lexically_normal();
        }

        std::string MakeProjectRelativeString(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& path) {

            std::error_code ec{};
            std::filesystem::path relative = std::filesystem::relative(path, projectRoot, ec);
            if (ec) {
                return path.lexically_normal().generic_string();
            }
            return relative.lexically_normal().generic_string();
        }

        std::string MakeBakeGuid() {
            const auto now = std::chrono::system_clock::now();
            const std::time_t time = std::chrono::system_clock::to_time_t(now);
            std::tm local{};
#if defined(_WIN32)
            localtime_s(&local, &time);
#else
            localtime_r(&time, &local);
#endif

            std::ostringstream oss{};
            oss << "bake_" << std::put_time(&local, "%Y%m%d_%H%M%S");
            return oss.str();
        }

        void FillBakePaths(const LightingBakeRequest& request, LightingBakeReport& report) {
            if (!request.projectRoot.empty() && !request.sceneGuid.empty()) {
                report.bakeRoot = ASSETS::LIGHTING::BuildLightingBakeRoot(
                    request.projectRoot,
                    request.sceneGuid);
                report.manifestPath = ASSETS::LIGHTING::BuildLightingBakeManifestPath(
                    request.projectRoot,
                    request.sceneGuid);
            }
        }

        bool ValidateStableSceneForPrepare(const LightingBakeRequest& request, LightingBakeReport& report) {
            if (request.projectRoot.empty()) {
                AddError(report, "Project root is empty.");
            }
            if (request.sceneGuid.empty()) {
                AddError(report, "Current scene has no stable asset GUID. Save scene before preparing lighting bake.");
            }
            if (!request.sceneDocument) {
                AddError(report, "SceneDocument is missing.");
            }
            return report.errors.empty();
        }

        LightingBakeRequest BuildRequestFromScene(DocumentSceneBase& scene, LightingBakeTarget target) {
            LightingBakeRequest request{};
            request.projectRoot = scene.GetAssetDatabase().GetProjectRoot();
            request.sceneGuid = scene.GetCurrentSceneAssetGuid().value;
            request.sceneName = scene.GetCurrentSceneDisplayName();
            request.scenePath = scene.GetScenePath();
            request.sceneDocument = &scene.GetSceneDocument();
            request.target = target;
            return request;
        }

    } // namespace

    LightingBakeReport LightingBakeService::ValidateLightingBakeSetup(const LightingBakeRequest& request) const {
        LightingBakeReport report{};
        report.action = LightingBakeAction::ValidateOnly;
        report.target = request.target;
        FillBakePaths(request, report);

        if (request.projectRoot.empty()) {
            AddError(report, "Project root is empty.");
        }
        if (request.sceneGuid.empty()) {
            AddError(report, "Current scene has no stable asset GUID. Save scene before preparing lighting bake.");
        }
        if (!request.sceneDocument) {
            AddError(report, "SceneDocument is missing.");
        }

        AppendSceneAuthoringSummary(request, report);
        report.messages.push_back("Phase 9 prepares manifest only. Reflection probe bake is now a separate action.");

        HIKARI_LOG_INFO("[LightingBake] validate scene=" + request.sceneGuid +
            " success=" + std::string(report.success ? "true" : "false"));
        return report;
    }

    LightingBakeReport LightingBakeService::PrepareLightingBakeManifest(const LightingBakeRequest& request) const {
        LightingBakeReport report{};
        report.action = LightingBakeAction::PrepareManifest;
        report.target = request.target;
        FillBakePaths(request, report);

        if (!ValidateStableSceneForPrepare(request, report)) {
            AppendSceneAuthoringSummary(request, report);
            return report;
        }

        std::error_code ec{};
        std::filesystem::create_directories(report.bakeRoot, ec);
        if (ec) {
            AddError(report, "Failed to create bake folder: " + ec.message());
            return report;
        }
        report.bakeFolderCreated = true;

        const ASSETS::LIGHTING::LightingBakeManifest manifest = BuildEmptyManifest(request);
        std::string saveMessage{};
        // 空の manifest を作成し、bake 出力先を確定する。
        if (!ASSETS::LIGHTING::SaveLightingBakeManifest(report.manifestPath, manifest, &saveMessage)) {
            AddError(report, saveMessage);
            return report;
        }

        report.manifestWritten = true;
        report.messages.push_back(saveMessage);
        report.messages.push_back("Lighting bake manifest prepared: " + report.manifestPath.generic_string());
        AppendSceneAuthoringSummary(request, report);

        HIKARI_LOG_INFO("[LightingBake] manifest prepared scene=" + request.sceneGuid +
            " path=" + report.manifestPath.generic_string());
        return report;
    }

    LightingBakeReport LightingBakeService::BakeReflectionProbesOnly(DocumentSceneBase& scene) const {
        const LightingBakeRequest request =
            BuildRequestFromScene(scene, LightingBakeTarget::ReflectionProbesOnly);

        LightingBakeReport report{};
        report.action = LightingBakeAction::BakeReflectionProbes;
        report.target = request.target;
        FillBakePaths(request, report);

        if (!ValidateStableSceneForPrepare(request, report)) {
            AppendSceneAuthoringSummary(request, report);
            return report;
        }

        const ReflectionProbeSettings& settings =
            request.sceneDocument->environment.reflectionProbe;
        if (!settings.enabled) {
            AddError(report, "Reflection probe is disabled in the current scene.");
            AppendSceneAuthoringSummary(request, report);
            return report;
        }
        if (settings.sourceCubemapAsset.empty()) {
            AddError(report, "Reflection probe source cubemap asset is empty.");
            AppendSceneAuthoringSummary(request, report);
            return report;
        }

        const auto* descriptor = scene.GetAssetRegistry().FindAs<SkyAssetDescriptor>(
            AssetId{ settings.sourceCubemapAsset });
        if (!descriptor) {
            AddError(report, "Reflection probe source cubemap asset is not registered: " +
                settings.sourceCubemapAsset);
            AppendSceneAuthoringSummary(request, report);
            return report;
        }
        if (descriptor->sourcePath.empty()) {
            AddError(report, "Reflection probe source cubemap path is empty: " +
                descriptor->id.value);
            AppendSceneAuthoringSummary(request, report);
            return report;
        }

        ReflectionProbeBakeRequest bakeRequest{};
        bakeRequest.projectRoot = request.projectRoot;
        bakeRequest.sceneGuid = request.sceneGuid;
        bakeRequest.sceneName = request.sceneName;
        bakeRequest.position = settings.position;
        bakeRequest.radius = settings.radius;
        bakeRequest.intensity = settings.intensity;
        bakeRequest.resolution = 128;
        bakeRequest.prefilteredMipCount = 7;
        bakeRequest.prefilteredSampleCount = 128;
        bakeRequest.brdfLutSize = 256;
        bakeRequest.brdfSampleCount = 256;
        bakeRequest.forceRebake = request.force;
        bakeRequest.sourceCubemapPath = descriptor->sourcePath;

        ReflectionProbeBaker baker{};
        const ReflectionProbeBakeResult bake = baker.BakeSingleProbe(scene, bakeRequest);

        report.reflectionProbeCaptured = bake.captured;
        report.reflectionProbePrefiltered = bake.prefiltered;
        report.bakeFolderCreated = bake.success;
        report.reflectionProbeCapturePath = bake.capturePath;
        report.reflectionProbePrefilteredPath = bake.prefilteredPath;
        report.reflectionProbeBrdfLutPath = bake.brdfLutPath;
        report.messages.insert(report.messages.end(), bake.messages.begin(), bake.messages.end());
        report.warnings.insert(report.warnings.end(), bake.warnings.begin(), bake.warnings.end());
        report.errors.insert(report.errors.end(), bake.errors.begin(), bake.errors.end());

        if (!bake.success) {
            report.success = false;
            AppendSceneAuthoringSummary(request, report);
            return report;
        }

        ASSETS::LIGHTING::LightingBakeManifest manifest{};
        std::string manifestMessage{};
        if (!ASSETS::LIGHTING::LoadLightingBakeManifest(report.manifestPath, manifest, &manifestMessage)) {
            manifest = BuildEmptyManifest(request);
            report.messages.push_back("Created new bake manifest because existing manifest was missing.");
        }

        manifest.sceneGuid = request.sceneGuid;
        manifest.generatedRoot = MakeProjectRelativeString(
            request.projectRoot,
            ASSETS::LIGHTING::BuildLightingBakeRoot(request.projectRoot, request.sceneGuid));
        manifest.reflectionProbes.clear();
        manifest.reflectionProbes.push_back(bake.record);

        std::string saveMessage{};
        if (!ASSETS::LIGHTING::SaveLightingBakeManifest(report.manifestPath, manifest, &saveMessage)) {
            AddError(report, saveMessage);
            AppendSceneAuthoringSummary(request, report);
            return report;
        }

        report.manifestWritten = true;
        report.reflectionProbeRecordWritten = true;
        report.reflectionProbeRecordCount =
            static_cast<uint32_t>(manifest.reflectionProbes.size());
        report.lightProbeRecordCount =
            static_cast<uint32_t>(manifest.lightProbes.size());
        report.lightmapRecordCount =
            static_cast<uint32_t>(manifest.lightmaps.size());
        report.messages.push_back(saveMessage);
        report.messages.push_back("Reflection probe bake manifest updated: " +
            report.manifestPath.generic_string());

        scene.RefreshTextureRuntimeByPath(bake.record.prefilteredCubemapPath);
        scene.RefreshTextureRuntimeByPath(bake.record.brdfLutPath);
        scene.RefreshLightingRuntime();
        HIKARI_LOG_INFO("[LightingBake] reflection probe baked scene=" + request.sceneGuid +
            " manifest=" + report.manifestPath.generic_string());
        return report;
    }

    LightingBakeReport LightingBakeService::ClearLightingBake(const LightingBakeRequest& request) const {
        LightingBakeReport report{};
        report.action = LightingBakeAction::ClearBake;
        report.target = request.target;
        FillBakePaths(request, report);

        if (request.projectRoot.empty()) {
            AddError(report, "Project root is empty.");
            return report;
        }
        if (request.sceneGuid.empty()) {
            AddError(report, "Current scene has no stable asset GUID.");
            return report;
        }

        const std::filesystem::path projectRoot = MakeAbsoluteNormalized(request.projectRoot);
        const std::filesystem::path bakeRoot = MakeAbsoluteNormalized(report.bakeRoot);
        const std::filesystem::path lightingRoot = MakeAbsoluteNormalized(
            request.projectRoot / "Library" / "Generated" / "Lighting");

        if (!HasPathPrefix(bakeRoot, projectRoot) || !HasPathPrefix(bakeRoot, lightingRoot)) {
            AddError(report, "Refused to clear unsafe bake folder: " + bakeRoot.generic_string());
            return report;
        }

        std::error_code ec{};
        if (!std::filesystem::exists(bakeRoot, ec)) {
            report.messages.push_back("Bake folder is already empty: " + bakeRoot.generic_string());
            HIKARI_LOG_INFO("[LightingBake] clear skipped, folder missing: " + bakeRoot.generic_string());
            return report;
        }

        const uintmax_t removed = std::filesystem::remove_all(bakeRoot, ec);
        if (ec) {
            AddError(report, "Failed to clear bake folder: " + ec.message());
            return report;
        }

        report.bakeFolderCleared = true;
        report.messages.push_back("Bake folder cleared: " + bakeRoot.generic_string());
        report.messages.push_back("Removed entries: " + std::to_string(removed));
        HIKARI_LOG_INFO("[LightingBake] clear bake folder scene=" + request.sceneGuid +
            " path=" + bakeRoot.generic_string());
        return report;
    }

    ASSETS::LIGHTING::LightingBakeManifest LightingBakeService::BuildEmptyManifest(
        const LightingBakeRequest& request) const {

        ASSETS::LIGHTING::LightingBakeManifest manifest{};
        manifest.version = ASSETS::LIGHTING::kLightingBakeManifestVersion;
        manifest.sceneGuid = request.sceneGuid;
        manifest.bakeGuid = MakeBakeGuid();
        manifest.bakeVersion = 1;
        manifest.generatedRoot = MakeProjectRelativeString(
            request.projectRoot,
            ASSETS::LIGHTING::BuildLightingBakeRoot(request.projectRoot, request.sceneGuid));
        return manifest;
    }

    void LightingBakeService::AppendSceneAuthoringSummary(
        const LightingBakeRequest& request,
        LightingBakeReport& report) const {

        if (!request.sceneDocument) {
            return;
        }

        const SceneEnvironment& environment = request.sceneDocument->environment;
        report.messages.push_back("Scene objects: " + std::to_string(request.sceneDocument->objects.size()));
        report.messages.push_back("Scene systems: " + std::to_string(request.sceneDocument->systems.size()));

        if (environment.sky.enabled && environment.sky.skyAsset.empty()) {
            report.warnings.push_back("Sky is enabled but sky asset is empty.");
        }
        if (environment.reflectionProbe.enabled && environment.reflectionProbe.sourceCubemapAsset.empty()) {
            report.warnings.push_back("Reflection probe is enabled but source cubemap asset is empty.");
        }
        if (environment.reflectionProbe.enabled && environment.reflectionProbe.radius <= 0.0f) {
            report.warnings.push_back("Reflection probe radius should be greater than zero.");
        }
        if (environment.ambientOcclusion.enabled) {
            if (environment.ambientOcclusion.radius <= 0.0f) {
                report.warnings.push_back("Ambient occlusion radius should be greater than zero.");
            }
            if (environment.ambientOcclusion.sampleCount < 8u) {
                report.warnings.push_back("Ambient occlusion sample count is very low.");
            }
        }
    }

} // namespace HIKARI::TOOLS::BAKING
