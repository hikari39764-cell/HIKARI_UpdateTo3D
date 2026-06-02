#include "HIKARI_ReflectionProbeBaker.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "Assets/Importers/HIKARI_IblBaker.h"
#include "Core/HIKARI_Logger.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"
#include "Tools/Baking/HIKARI_ReflectionProbeCaptureValidator.h"

namespace HIKARI::TOOLS::BAKING {

    namespace {

        constexpr const char* kProbeId = "probe_000";
        constexpr const char* kProbeName = "ReflectionProbe_000";

        void AddError(ReflectionProbeBakeResult& result, std::string message) {
            result.errors.push_back(std::move(message));
            result.success = false;
        }

        const char* FormatName(DXGI_FORMAT format) {
            switch (format) {
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
                return "R16G16B16A16_FLOAT";
            case DXGI_FORMAT_R32G32B32A32_FLOAT:
                return "R32G32B32A32_FLOAT";
            case DXGI_FORMAT_R11G11B10_FLOAT:
                return "R11G11B10_FLOAT";
            case DXGI_FORMAT_BC6H_UF16:
                return "BC6H_UF16";
            case DXGI_FORMAT_BC6H_SF16:
                return "BC6H_SF16";
            default:
                return "DXGI_FORMAT_OTHER";
            }
        }

        void MergeValidationMessages(
            ReflectionProbeBakeResult& result,
            const ReflectionProbeCaptureValidationResult& validation) {

            result.messages.insert(
                result.messages.end(),
                validation.messages.begin(),
                validation.messages.end());
            result.warnings.insert(
                result.warnings.end(),
                validation.warnings.begin(),
                validation.warnings.end());
            result.errors.insert(
                result.errors.end(),
                validation.errors.begin(),
                validation.errors.end());
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
            AddError(result, "Manual reflection probe source override path is empty.");
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

        // Manual override は外部 cubemap を capture 入力として扱う。
        result.warnings.push_back(
            "Manual source cubemap override used for reflection probe bake.");

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

        return FinalizeCapturedProbe(request, result.capturePath);
    }

    ReflectionProbeBakeResult ReflectionProbeBaker::FinalizeCapturedProbe(
        const ReflectionProbeBakeRequest& request,
        const std::filesystem::path& capturePath) const {

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
        if (capturePath.empty()) {
            AddError(result, "Reflection probe capture path is empty.");
            return result;
        }
        if (request.radius <= 0.0f) {
            AddError(result, "Reflection probe radius must be greater than zero.");
            return result;
        }

        result.capturePath = MakeAbsoluteNormalized(request.projectRoot, capturePath);
        std::error_code ec{};
        if (!std::filesystem::exists(result.capturePath, ec)) {
            AddError(result, "Reflection probe capture DDS does not exist: " +
                result.capturePath.generic_string());
            return result;
        }

        const std::filesystem::path outputDirectory =
            ReflectionProbeOutputDirectory(request.projectRoot, request.sceneGuid);
        result.prefilteredPath = outputDirectory / "probe_000_prefiltered.dds";
        result.brdfLutPath = (request.projectRoot / "Library" / "Generated" /
            "IBL" / "brdf_lut.dds").lexically_normal();

        std::filesystem::create_directories(outputDirectory, ec);
        if (ec) {
            AddError(result, "Failed to create reflection probe output folder: " + ec.message());
            return result;
        }

        result.captured = true;
        result.messages.push_back("Probe scene capture cubemap ready: " +
            result.capturePath.generic_string());

        const ReflectionProbeCaptureValidationResult captureValidation =
            ValidateReflectionProbeCaptureDds(result.capturePath);
        MergeValidationMessages(result, captureValidation);
        result.captureValidated = captureValidation.success;
        result.capturedFaceCount = captureValidation.arraySize;
        result.captureResolution = captureValidation.width;
        result.captureMipCount = captureValidation.mipCount;
        result.captureFormat = FormatName(captureValidation.format);
        result.faceSummaries = captureValidation.faceSummaries;
        if (!captureValidation.success) {
            result.success = false;
            return result;
        }

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

        const ReflectionProbeCaptureValidationResult prefilterValidation =
            ValidateReflectionProbePrefilteredDds(result.prefilteredPath);
        MergeValidationMessages(result, prefilterValidation);
        result.prefilterValidated = prefilterValidation.success;
        result.prefilteredMipCount = prefilterValidation.mipCount;
        result.prefilteredFormat = FormatName(prefilterValidation.format);
        if (!prefilterValidation.success) {
            result.success = false;
            return result;
        }

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
