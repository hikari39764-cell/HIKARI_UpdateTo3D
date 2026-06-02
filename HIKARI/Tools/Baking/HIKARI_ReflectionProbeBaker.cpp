#include "HIKARI_ReflectionProbeBaker.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "Assets/Importers/HIKARI_IblBaker.h"
#include "Core/HIKARI_Logger.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

namespace HIKARI::TOOLS::BAKING {

    namespace {

        constexpr const char* kProbeId = "probe_000";
        constexpr const char* kProbeName = "ReflectionProbe_000";

        void AddError(ReflectionProbeBakeResult& result, std::string message) {
            result.errors.push_back(std::move(message));
            result.success = false;
        }

        std::filesystem::path MakeAbsoluteNormalized(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& path) {

            if (path.empty()) {
                return {};
            }
            if (path.is_absolute()) {
                return path.lexically_normal();
            }
            return (projectRoot / path).lexically_normal();
        }

        std::string MakeProjectRelativeString(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& path) {

            std::error_code ec{};
            const std::filesystem::path relative = std::filesystem::relative(path, projectRoot, ec);
            if (ec) {
                return path.lexically_normal().generic_string();
            }
            return relative.lexically_normal().generic_string();
        }

        std::filesystem::path ReflectionProbeOutputDirectory(
            const std::filesystem::path& projectRoot,
            const std::string& sceneGuid) {

            return (ASSETS::LIGHTING::BuildLightingBakeRoot(projectRoot, sceneGuid) /
                "reflection_probes").lexically_normal();
        }

    } // namespace

    ReflectionProbeBakeResult ReflectionProbeBaker::BakeSingleProbe(
        DocumentSceneBase& scene,
        const ReflectionProbeBakeRequest& request) const {

        (void)scene;

        ReflectionProbeBakeResult result{};
        result.success = false;

        if (request.projectRoot.empty()) {
            AddError(result, "Project root is empty.");
            return result;
        }
        if (request.sceneGuid.empty()) {
            AddError(result, "Scene GUID is empty.");
            return result;
        }
        if (request.sourceCubemapPath.empty()) {
            AddError(result, "Reflection probe source cubemap is empty.");
            return result;
        }
        if (request.radius <= 0.0f) {
            AddError(result, "Reflection probe radius must be greater than zero.");
            return result;
        }

        const std::filesystem::path sourceCubemap =
            MakeAbsoluteNormalized(request.projectRoot, request.sourceCubemapPath);

        std::error_code ec{};
        if (!std::filesystem::exists(sourceCubemap, ec)) {
            AddError(result, "Source cubemap does not exist: " + sourceCubemap.generic_string());
            return result;
        }

        const std::filesystem::path outputDirectory =
            ReflectionProbeOutputDirectory(request.projectRoot, request.sceneGuid);
        result.capturePath = outputDirectory / "probe_000_capture.dds";
        result.prefilteredPath = outputDirectory / "probe_000_prefiltered.dds";
        result.brdfLutPath = (request.projectRoot / "Library" / "Generated" /
            "IBL" / "brdf_lut.dds").lexically_normal();

        std::filesystem::create_directories(outputDirectory, ec);
        if (ec) {
            AddError(result, "Failed to create reflection probe output folder: " + ec.message());
            return result;
        }

        // 現段階では authoring cubemap を capture 入力として使う。
        result.warnings.push_back(
            "Offscreen scene cubemap capture is not implemented yet; using authoring source cubemap.");
        result.warnings.push_back(
            "Static geometry filtering is reserved for the capture pipeline phase.");

        std::filesystem::copy_file(
            sourceCubemap,
            result.capturePath,
            std::filesystem::copy_options::overwrite_existing,
            ec);
        if (ec) {
            AddError(result, "Failed to write probe capture cubemap: " + ec.message());
            return result;
        }
        result.captured = true;
        result.messages.push_back("Probe capture cubemap written: " + result.capturePath.generic_string());

        const uint32_t resolution = std::clamp(request.resolution, 32u, 256u);
        const uint32_t mipCount = std::clamp(request.prefilteredMipCount, 1u, 9u);
        const uint32_t sampleCount = std::clamp(request.prefilteredSampleCount, 32u, 256u);

        std::string message{};
        if (!IblBaker::BakePrefilteredCubemapOnly(
                result.capturePath,
                result.prefilteredPath,
                resolution,
                mipCount,
                sampleCount,
                &message)) {
            AddError(result, "Failed to prefilter reflection probe: " + message);
            return result;
        }
        result.prefiltered = true;
        result.messages.push_back(message);

        if (!IblBaker::EnsureSharedBrdfLut(
                result.brdfLutPath,
                request.brdfLutSize,
                request.brdfSampleCount,
                request.forceRebake,
                &message)) {
            AddError(result, "Failed to prepare shared BRDF LUT: " + message);
            return result;
        }
        result.brdfReady = true;
        result.messages.push_back(message);

        result.record.id = kProbeId;
        result.record.name = kProbeName;
        result.record.position = request.position;
        result.record.radius = request.radius;
        result.record.intensity = request.intensity;
        result.record.captureCubemapPath =
            MakeProjectRelativeString(request.projectRoot, result.capturePath);
        result.record.prefilteredCubemapPath =
            MakeProjectRelativeString(request.projectRoot, result.prefilteredPath);
        result.record.brdfLutPath =
            MakeProjectRelativeString(request.projectRoot, result.brdfLutPath);
        result.record.prefilteredMipCount = mipCount;

        result.success = true;
        HIKARI_LOG_INFO("[ReflectionProbeBaker] baked single probe scene=" +
            request.sceneGuid +
            " prefiltered=" + result.prefilteredPath.generic_string());
        return result;
    }

} // namespace HIKARI::TOOLS::BAKING
