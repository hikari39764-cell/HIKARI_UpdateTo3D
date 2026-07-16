#include "HIKARI_DocumentSceneBase.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <cctype>
#include <utility>
#include <numbers>
#include <memory>
#include <algorithm>
#include <vector>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <unordered_set>

#include "HIKARI_3D.h"
#include "Render2D/HIKARI_DxTexture.h"
#include "HIKARI_Services.h"
#include "Assets/HIKARI_AssetRegistryBuilder.h"
#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/HIKARI_LightDebugDraw.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#endif
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Scene/HIKARI_AnimationSystem.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/Components/HIKARI_DoorTransitionComponent.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/Components/HIKARI_PlayerControllerComponent.h"
#include "Scene/Components/HIKARI_SpawnPointComponent.h"
#include "Scene/Sequencer/Drivers/HIKARI_CameraSequenceTrackDriver.h"
#include "Scene/Components/HIKARI_TriggerVolumeComponent.h"
#include "Scene/Components/HIKARI_UIButtonSceneTransitionComponent.h"
#include "Vfx/Runtime/HIKARI_VfxAsset.h"
#include "Vfx/Runtime/HIKARI_VfxSystem.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include "Vfx/Post/HIKARI_PostProfile.h"
#include "Scene/Components/HIKARI_ComponentLinkComponent.h"
#include "Scene/Components/HIKARI_VfxPlayerComponent.h"
#include "Scene/HIKARI_CameraFollowSystem.h"
#include "Scene/HIKARI_DefaultSceneSystems.h"
#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"
#include "Scene/HIKARI_PlayerMovementSystem.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Tools/Baking/HIKARI_ProbeCubemapCaptureTarget.h"
#include "Tools/Baking/HIKARI_LightProbeBaker.h"
#include "Tools/Baking/HIKARI_ReflectionProbeBaker.h"
#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"

namespace HIKARI {
    namespace {
        constexpr const char* kDefaultGlobalPostProfileId = "Ani";

        bool TryUnprojectClipCorner(
            const MATH::Mat4& invViewProj,
            float x,
            float y,
            float z,
            MATH::Vec3& out) {

            const MATH::Vec4 clip{ x, y, z, 1.0f };
            const MATH::Vec4 world = invViewProj.TransformPoint(clip);
            if (std::abs(world.w) < 1.0e-6f ||
                !std::isfinite(world.x) ||
                !std::isfinite(world.y) ||
                !std::isfinite(world.z) ||
                !std::isfinite(world.w)) {
                return false;
            }

            const float invW = 1.0f / world.w;
            out = { world.x * invW, world.y * invW, world.z * invW };
            return true;
        }

        void SubmitFrozenCullingCameraDebugFrustum() {
            if (!MESHRENDERER::IsGpuDrivenCullingCameraFrozen()) {
                return;
            }

            const MESHRENDERER::CameraCB* cullingCamera =
                MESHRENDERER::GetGpuDrivenCullingCameraConstants();
            if (cullingCamera == nullptr) {
                return;
            }

            constexpr std::array<std::array<float, 3>, 8> kClipCorners{ {
                { -1.0f, -1.0f, 0.0f },
                {  1.0f, -1.0f, 0.0f },
                {  1.0f,  1.0f, 0.0f },
                { -1.0f,  1.0f, 0.0f },
                { -1.0f, -1.0f, 1.0f },
                {  1.0f, -1.0f, 1.0f },
                {  1.0f,  1.0f, 1.0f },
                { -1.0f,  1.0f, 1.0f },
            } };

            std::array<MATH::Vec3, 8> corners{};
            for (size_t i = 0; i < kClipCorners.size(); ++i) {
                const auto& c = kClipCorners[i];
                if (!TryUnprojectClipCorner(
                    cullingCamera->invViewProj,
                    c[0],
                    c[1],
                    c[2],
                    corners[i])) {
                    return;
                }
            }

            constexpr std::array<std::array<int, 2>, 12> kEdges{ {
                { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
                { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
                { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
            } };
            constexpr unsigned int kNearColor = 0xFFD166FFu;
            constexpr unsigned int kFrustumColor = 0x37C8FFFFu;
            constexpr unsigned int kRayColor = 0x8F6AFFFFu;
            for (size_t i = 0; i < kEdges.size(); ++i) {
                const auto& edge = kEdges[i];
                RENDERER3D::DEBUG::SubmitLine3D({
                    corners[static_cast<size_t>(edge[0])],
                    corners[static_cast<size_t>(edge[1])],
                    i < 4 ? kNearColor : kFrustumColor,
                    RENDERER3D::DEBUG::DebugDepthMode::XRay
                });
            }

            const MATH::Vec3 cullingPosition{
                cullingCamera->cameraPos.x,
                cullingCamera->cameraPos.y,
                cullingCamera->cameraPos.z
            };
            for (size_t i = 0; i < 4; ++i) {
                RENDERER3D::DEBUG::SubmitLine3D({
                    cullingPosition,
                    corners[i],
                    kRayColor,
                    RENDERER3D::DEBUG::DebugDepthMode::XRay
                });
            }
        }

        std::string ToLowerCopy(std::string value) {
            for (char& ch : value) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        bool HasMissingAssetDescriptors(
            const AssetRegistry& registry,
            AssetType expectedType,
            const std::unordered_set<std::string>& assetIds) {

            for (const std::string& assetId : assetIds) {
                if (assetId.empty()) {
                    continue;
                }

                const AssetDescriptor* descriptor = registry.FindDescriptor(assetId);
                if (descriptor == nullptr || descriptor->type != expectedType) {
                    return true;
                }
            }

            return false;
        }

        bool NeedsRuntimeDependencyRegistryRefresh(
            const AssetRegistry& registry,
            const SceneDependencySet& dependencies) {

            return
                HasMissingAssetDescriptors(registry, AssetType::Model, dependencies.modelAssetIds) ||
                HasMissingAssetDescriptors(registry, AssetType::Material, dependencies.materialAssetIds) ||
                HasMissingAssetDescriptors(registry, AssetType::Sky, dependencies.skyAssetIds);
        }

        const char* ToModelTextureUsageText(ModelTextureUsage usage) {
            switch (usage) {
            case ModelTextureUsage::BaseColor: return "BaseColor";
            case ModelTextureUsage::Normal: return "Normal";
            case ModelTextureUsage::MetallicRoughness: return "MetallicRoughness";
            case ModelTextureUsage::Occlusion: return "Occlusion";
            case ModelTextureUsage::Emissive: return "Emissive";
            case ModelTextureUsage::Specular: return "Specular";
            case ModelTextureUsage::SpecularColor: return "SpecularColor";
            default: return "Unknown";
            }
        }

        bool IsCookedTextureRuntimePath(const std::filesystem::path& path) {
            const std::string ext = ToLowerCopy(path.extension().string());
            if (ext == ".htex") {
                return true;
            }

            const std::string generic = ToLowerCopy(path.generic_string());
            return generic.find("library/imported/") != std::string::npos;
        }

        bool EqualVec3(const MATH::Vec3& lhs, const MATH::Vec3& rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }

        bool EqualSkySettings(const SkySettings& lhs, const SkySettings& rhs) {
            return lhs.enabled == rhs.enabled &&
                lhs.mode == rhs.mode &&
                lhs.skyAsset == rhs.skyAsset &&
                lhs.scale == rhs.scale &&
                lhs.yaw == rhs.yaw &&
                lhs.exposure == rhs.exposure &&
                EqualVec3(lhs.tint, rhs.tint) &&
                lhs.followCamera == rhs.followCamera &&
                EqualVec3(lhs.zenithColor, rhs.zenithColor) &&
                EqualVec3(lhs.horizonColor, rhs.horizonColor) &&
                EqualVec3(lhs.groundColor, rhs.groundColor) &&
                lhs.horizonPower == rhs.horizonPower &&
                lhs.showSunDisk == rhs.showSunDisk &&
                lhs.sunDiskIntensity == rhs.sunDiskIntensity &&
                lhs.sunDiskSize == rhs.sunDiskSize &&
                lhs.ambientFromSky == rhs.ambientFromSky &&
                lhs.reflectionIntensity == rhs.reflectionIntensity &&
                lhs.showDebugTexture == rhs.showDebugTexture;
        }

        bool IsSkyRuntimeResourceBindingChanged(
             const SkySettings& before,
             const SkySettings& after)
        {
            return before.skyAsset != after.skyAsset ||
                   before.mode != after.mode;
        }

        bool IsReflectionProbeRuntimeBindingChanged(
            const ReflectionProbeSettings& before,
            const ReflectionProbeSettings& after)
        {
            return before.enabled != after.enabled ||
                before.sourceCubemapAsset != after.sourceCubemapAsset ||
                !EqualVec3(before.position, after.position) ||
                before.radius != after.radius ||
                before.intensity != after.intensity ||
                before.influenceShape != after.influenceShape ||
                before.projectionShape != after.projectionShape ||
                !EqualVec3(before.influenceBoxCenter, after.influenceBoxCenter) ||
                !EqualVec3(before.influenceBoxSize, after.influenceBoxSize) ||
                !EqualVec3(before.projectionBoxCenter, after.projectionBoxCenter) ||
                !EqualVec3(before.projectionBoxSize, after.projectionBoxSize) ||
                before.blendDistance != after.blendDistance ||
                before.priority != after.priority;
        }

        const char* ReflectionProbeInfluenceShapeName(ReflectionProbeInfluenceShape shape) {
            return shape == ReflectionProbeInfluenceShape::Box ? "Box" : "Sphere";
        }

        const char* ReflectionProbeProjectionShapeName(ReflectionProbeProjectionShape shape) {
            return shape == ReflectionProbeProjectionShape::Box ? "Box" : "Infinite";
        }

        REFLECTION::RuntimeReflectionProbeInfluenceShape ToRuntimeInfluenceShape(
            ReflectionProbeInfluenceShape shape) {
            return shape == ReflectionProbeInfluenceShape::Box
                ? REFLECTION::RuntimeReflectionProbeInfluenceShape::Box
                : REFLECTION::RuntimeReflectionProbeInfluenceShape::Sphere;
        }

        REFLECTION::RuntimeReflectionProbeProjectionShape ToRuntimeProjectionShape(
            ReflectionProbeProjectionShape shape) {
            return shape == ReflectionProbeProjectionShape::Box
                ? REFLECTION::RuntimeReflectionProbeProjectionShape::Box
                : REFLECTION::RuntimeReflectionProbeProjectionShape::Infinite;
        }

        const char* ProbeFaceName(uint32_t faceIndex) {
            static constexpr const char* kFaceNames[6] = {
                "+X", "-X", "+Y", "-Y", "+Z", "-Z"
            };
            return faceIndex < 6u ? kFaceNames[faceIndex] : "?";
        }

        void AddBakeError(TOOLS::BAKING::LightingBakeReport& report, std::string message) {
            report.success = false;
            report.errors.push_back(std::move(message));
        }

        bool IsBakeStateRunning(TOOLS::BAKING::LightingBakeJobState state) {
            switch (state) {
            case TOOLS::BAKING::LightingBakeJobState::Requested:
            case TOOLS::BAKING::LightingBakeJobState::Capturing:
            case TOOLS::BAKING::LightingBakeJobState::WaitingGpu:
            case TOOLS::BAKING::LightingBakeJobState::ProjectingSH:
            case TOOLS::BAKING::LightingBakeJobState::Saving:
            case TOOLS::BAKING::LightingBakeJobState::Finalizing:
                return true;
            default:
                return false;
            }
        }

        std::filesystem::path ReflectionProbeOutputDirectory(
            const std::filesystem::path& projectRoot,
            const std::string& sceneGuid) {

            return (ASSETS::LIGHTING::BuildLightingBakeRoot(projectRoot, sceneGuid) /
                "reflection_probes").lexically_normal();
        }

        std::filesystem::path LightProbeOutputDirectory(
            const std::filesystem::path& projectRoot,
            const std::string& sceneGuid) {

            return (ASSETS::LIGHTING::BuildLightingBakeRoot(projectRoot, sceneGuid) /
                "light_probes").lexically_normal();
        }

        std::filesystem::path LightProbeCaptureDirectory(
            const std::filesystem::path& projectRoot,
            const std::string& sceneGuid) {

            return (LightProbeOutputDirectory(projectRoot, sceneGuid) /
                "captures").lexically_normal();
        }

        std::filesystem::path LightProbeCapturePath(
            const std::filesystem::path& projectRoot,
            const std::string& sceneGuid,
            uint32_t probeIndex) {

            std::ostringstream filename{};
            filename << "probe_" << std::setfill('0') << std::setw(3) << probeIndex << "_capture.dds";
            return (LightProbeCaptureDirectory(projectRoot, sceneGuid) /
                filename.str()).lexically_normal();
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

        std::string MakeBakeGuid() {
            const auto now = std::chrono::system_clock::now();
            const std::time_t time = std::chrono::system_clock::to_time_t(now);
            std::tm local{};
#if defined(_WIN32)
            localtime_s(&local, &time);
#else
            localtime_r(&local, &time);
#endif

            std::ostringstream oss{};
            oss << "bake_" << std::put_time(&local, "%Y%m%d_%H%M%S");
            return oss.str();
        }

        ASSETS::LIGHTING::LightingBakeManifest LoadOrCreateLightingBakeManifest(
            const std::filesystem::path& manifestPath,
            const std::filesystem::path& projectRoot,
            const std::string& sceneGuid,
            std::vector<std::string>& warnings) {

            ASSETS::LIGHTING::LightingBakeManifest manifest{};
            std::string loadMessage{};
            if (!manifestPath.empty() &&
                std::filesystem::exists(manifestPath) &&
                !ASSETS::LIGHTING::LoadLightingBakeManifest(manifestPath, manifest, &loadMessage)) {
                warnings.push_back(loadMessage);
                manifest = {};
            }

            manifest.version = ASSETS::LIGHTING::kLightingBakeManifestVersion;
            manifest.sceneGuid = sceneGuid;
            if (manifest.bakeGuid.empty()) {
                manifest.bakeGuid = MakeBakeGuid();
            }
            manifest.bakeVersion = 1;
            manifest.generatedRoot = MakeProjectRelativeString(
                projectRoot,
                ASSETS::LIGHTING::BuildLightingBakeRoot(projectRoot, sceneGuid));
            return manifest;
        }

        void ReplaceReflectionProbeRecord(
            ASSETS::LIGHTING::LightingBakeManifest& manifest,
            const ASSETS::LIGHTING::ReflectionProbeBakeRecord& record) {

            manifest.reflectionProbes.clear();
            manifest.reflectionProbes.push_back(record);
        }

        void ReplaceLightProbeVolumeRecord(
            ASSETS::LIGHTING::LightingBakeManifest& manifest,
            const ASSETS::LIGHTING::LightProbeBakeRecord& record) {

            manifest.lightProbes.erase(
                std::remove_if(
                    manifest.lightProbes.begin(),
                    manifest.lightProbes.end(),
                    [](const ASSETS::LIGHTING::LightProbeBakeRecord& existing) {
                        return existing.type.empty() ||
                            existing.type == "VolumeGrid" ||
                            existing.id == "light_probe_volume_000";
                    }),
                manifest.lightProbes.end());
            manifest.lightProbes.push_back(record);
        }

        uint32_t GetLightProbeCaptureFaceCount(const LightProbeVolumeSettings& settings) {
            return GetLightProbeVolumeProbeCount(settings) * 6u;
        }

        MATH::Vec3 GetLightProbePositionByIndex(
            const LightProbeVolumeSettings& settings,
            uint32_t probeIndex) {

            const uint32_t xyCount = settings.countX * settings.countY;
            const uint32_t z = xyCount > 0u ? probeIndex / xyCount : 0u;
            const uint32_t xy = xyCount > 0u ? probeIndex % xyCount : 0u;
            const uint32_t y = settings.countX > 0u ? xy / settings.countX : 0u;
            const uint32_t x = settings.countX > 0u ? xy % settings.countX : 0u;
            return GetLightProbeVolumeProbePosition(settings, x, y, z);
        }

        float GetLightProbeCaptureFarPlane(const LightProbeVolumeSettings& settings) {
            const float xyMax = (std::max)(settings.size.x * 1.5f, settings.size.y * 1.5f);
            return (std::max)((std::max)(4.0f, xyMax), settings.size.z * 1.5f);
        }

        void SetBakeJobState(
            TOOLS::BAKING::LightingBakeReport& report,
            TOOLS::BAKING::LightingBakeJobState state) {

            report.jobState = state;
        }

        Camera3D MakeProbeFaceCamera(
            const MATH::Vec3& position,
            uint32_t faceIndex,
            float farPlane) {

            static constexpr MATH::Vec3 kDirections[6] = {
                { 1.0f, 0.0f, 0.0f },
                { -1.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f },
                { 0.0f, -1.0f, 0.0f },
                { 0.0f, 0.0f, 1.0f },
                { 0.0f, 0.0f, -1.0f },
            };
            static constexpr MATH::Vec3 kUps[6] = {
                { 0.0f, 1.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f },
                { 0.0f, 0.0f, -1.0f },
                { 0.0f, 0.0f, 1.0f },
                { 0.0f, 1.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f },
            };

            const uint32_t face = (std::min)(faceIndex, 5u);
            Camera3D camera{};
            camera.SetPerspective(
                std::numbers::pi_v<float> * 0.5f,
                1.0f,
                0.05f,
                (std::max)(1.0f, farPlane));
            camera.SetLookAt(position, position + kDirections[face], kUps[face]);
            return camera;
        }
      
    }

    struct ReflectionProbeBakeJob {
        TOOLS::BAKING::LightingBakeJobState state =
            TOOLS::BAKING::LightingBakeJobState::Idle;
        TOOLS::BAKING::ReflectionProbeBakeRequest request{};
        TOOLS::BAKING::LightingBakeReport report{};
        TOOLS::BAKING::ProbeCubemapCaptureTarget captureTarget{};
        uint64_t fenceValue = 0;
        uint32_t nextFaceIndex = 0;
    };

    struct LightProbeBakeJob {
        TOOLS::BAKING::LightingBakeJobState state =
            TOOLS::BAKING::LightingBakeJobState::Idle;
        TOOLS::BAKING::LightProbeBakeRequest request{};
        TOOLS::BAKING::LightingBakeReport report{};
        TOOLS::BAKING::ProbeCubemapCaptureTarget captureTarget{};
        uint64_t fenceValue = 0;
        uint32_t nextProbeIndex = 0;
        uint32_t nextFaceIndex = 0;
        std::vector<std::filesystem::path> probeCapturePaths{};
    };
    
    DocumentSceneBase::DocumentSceneBase(std::string sceneId)
        : sceneId_(std::move(sceneId)) {

        (void)sequencePlaybackService_.RegisterDriver(
            std::make_unique<SEQUENCER::CameraSequenceTrackDriver>(
                cameraDirector_,
                world_,
                camera_));
    }

    DocumentSceneBase::~DocumentSceneBase() = default;

    void DocumentSceneBase::OnEnter() {
        camera_.SetPerspective(60.0f * std::numbers::pi_v<float> / 180.0f, static_cast<float>(kScreenW) / static_cast<float>(kScreenH), 0.1f, 100.0f);
        gameplayCamera_ = camera_;
        debugCamera_.Reset({ 0.0f, 2.0f, -6.0f }, 0.0f, 0.0f);

        RegisterDefaultComponentTypes();
        RegisterDefaultSystemTypes();
        RuntimeFeatureContext runtimeFeatureContext{
            componentRegistry_,
            systemTypeRegistry_
        };
        RegisterBuiltInRuntimeFeatures(runtimeFeatureContext);

        ReloadAssets();
        sequenceAssetStore_.SetAssetDatabase(&assetDatabase_);
        sequencePlaybackService_.SetAssetStore(&sequenceAssetStore_);
        VFX::SetAssetRegistry(&assetRegistry_);
        if (!OpenStartupSceneAsset()) {
            CreateTransientEmptySceneDocument();
            RebuildRuntimeWorld();
        }
    }
    void DocumentSceneBase::OnExit() {
        sequencePlaybackService_.Reset();
        currentCameraSequenceHandle_ = {};
        sequenceAssetStore_.Clear();
        systemScheduler_.DetachWorld(world_);
        systemScheduler_.Clear();
        RuntimeSceneContext::SetCurrentWorld(nullptr);
    }
    void DocumentSceneBase::Update(float dt) {
        const FrameContext& frame = HIKARI::TIME::GetFrameContext();
        if (runtimePlayActive_ && runtimePreviewCameraActive_) {
            runtimePreviewCamera_.Update(
                dt,
                gameplayCamera_,
                CameraControlInputContext::RuntimeWindow);
        } else if (UseDebugCamera() && !IsEditorCameraPreviewActive()) {
            debugCamera_.Update(
                dt,
                camera_,
                CameraControlInputContext::EditorViewport);
        }
        systemScheduler_.PreUpdate(world_, frame);
        world_.Update(dt);
        systemScheduler_.Update(world_, frame);
        systemScheduler_.LateUpdate(world_, frame);

        if (runtimePlayActive_) {
            sequencePlaybackService_.Update(dt);
            resolvedCameraFrame_ = cameraDirector_.Resolve(
                world_,
                gameplayCamera_,
                camera_.GetAspect(),
                dt);
            camera_ = resolvedCameraFrame_.camera;
            gameplayCamera_ = cameraDirector_.GetControlCamera();
        } else if (IsEditorCameraPreviewActive()) {
            resolvedCameraFrame_ = cameraDirector_.Resolve(
                world_,
                editorCameraPreviewSnapshot_,
                camera_.GetAspect(),
                dt);
            camera_ = resolvedCameraFrame_.camera;
        } else {
            resolvedCameraFrame_.camera = camera_;
            resolvedCameraFrame_.sourceCameraObjectId = 0;
            resolvedCameraFrame_.cameraCut = editorCameraCutPending_;
            resolvedCameraFrame_.projectionChanged = false;
            resolvedCameraFrame_.valid = true;
            editorCameraCutPending_ = false;
        }
    }
    void DocumentSceneBase::Render() {
        int captureW = 0;
        int captureH = 0;
        POST::PostSystem::GetSceneCaptureSize(captureW, captureH);
        if (captureW > 0 && captureH > 0) {
            const float aspect = static_cast<float>(captureW) / static_cast<float>(captureH);
            const bool projectionChanged = std::abs(camera_.GetAspect() - aspect) > 1.0e-5f;
            camera_.SetPerspective(
                camera_.GetFovYRad(),
                aspect,
                camera_.GetNearZ(),
                camera_.GetFarZ());
            resolvedCameraFrame_.camera = camera_;
            resolvedCameraFrame_.projectionChanged =
                resolvedCameraFrame_.projectionChanged || projectionChanged;
            resolvedCameraFrame_.valid = true;
        }

        if (ProcessReflectionProbeBakeJob()) {
            return;
        }
        if (ProcessLightProbeBakeJob()) {
            return;
        }

        RENDERER3D::Reset();
        MODELRENDERER::ResetFrame();
        SKYRENDERER::Reset();

        if (environment_.post.enabled && environment_.post.globalPostProfileId.empty()) {
            environment_.post.globalPostProfileId = kDefaultGlobalPostProfileId;
            environment_.post.valuesInitialized = false;
        }

        if (environment_.post.enabled && !environment_.post.globalPostProfileId.empty()) {
            if (!environment_.post.valuesInitialized) {
                PostProfile profile{};
                if (PostProfile::LoadById(environment_.post.globalPostProfileId, profile)) {
                    profile.CopyValuesTo(environment_.post.paramValues);
                    environment_.post.valuesInitialized = true;
                }
            }
            POST::PostSystem::SetGlobalProfile(environment_.post.globalPostProfileId, environment_.post.paramValues);
        } else {
            POST::PostSystem::ClearGlobalProfile();
        }
        POST::PostSystem::SetBloomSettings(environment_.bloom);
        POST::PostSystem::SetToneMappingSettings(environment_.toneMapping);

        if (DrawDebugHelpers() && viewportOverlayState_.showGrid) {
            RENDERER3D::DEBUG::Grid3D grid{};
            grid.halfCount = 10;
            grid.spacing = 1.0f;
            RENDERER3D::DEBUG::SubmitGrid3D(grid);
        }

        if (DrawDebugHelpers() && viewportOverlayState_.showAxis) {
            RENDERER3D::DEBUG::Axis3D axis{};
            axis.length = 2.5f;
            RENDERER3D::DEBUG::SubmitAxis3D(axis);
        }

        world_.Render();
        const FrameContext& frame = HIKARI::TIME::GetFrameContext();
        RenderSubmissionSystem::SetAssetContext(&assetRegistry_, assetDatabase_.GetProjectRoot());
        systemScheduler_.PreRender(world_, frame);
        systemScheduler_.Render(world_, frame);
        systemScheduler_.PostRender(world_, frame);
        SceneEnvironment activeEnvironment = environment_;
        activeEnvironment.directional.direction = MATH::Normalize(activeEnvironment.directional.direction);
        const bool editorSsaoSuppressed =
            DrawDebugHelpers() &&
            viewportPerformanceState_.disableSsaoInEditorViewport;
        activeEnvironment.ambientOcclusion.editorViewportSuppressed = editorSsaoSuppressed;
        if (!UseEnvironmentLighting()) {
            activeEnvironment.directional.intensity = 0.0f;
            activeEnvironment.ambient.intensity = 0.0f;
            activeEnvironment.specularIntensity = 0.0f;
            for (PointLight& pointLight : activeEnvironment.pointLights) {
                pointLight.intensity = 0.0f;
            }
        }

        MESHRENDERER::SetGpuDrivenCullingCameraFreezeEnabled(
            DrawDebugHelpers() && viewportDebugViewState_.freezeCullingCamera);
        const Camera3D& renderCamera = camera_;

        SKYRENDERER::Render(renderCamera, activeEnvironment, modelManager_, skyManager_);
        if (DrawDebugHelpers()) {
            if (viewportOverlayState_.showLights) {
                LIGHTDEBUGDRAW::SubmitDirectionalLightArrow(activeEnvironment.directional.direction, activeEnvironment);
                LIGHTDEBUGDRAW::SubmitPointLightDebug(activeEnvironment);
            }
#if defined(HIKARI_WITH_EDITOR)
            if (viewportOverlayState_.showReflectionProbe) {
                reflectionProbeGizmoRenderer_.Submit(activeEnvironment, viewportOverlayState_, renderCamera);
            }
            lightProbeVolumeGizmoRenderer_.Submit(
                sceneDocument_.lightingBake.lightProbeVolume,
                viewportOverlayState_);
#endif
        }

        if (DrawDebugHelpers()) {
            componentGizmoRenderer_.SubmitWorldGizmos(
                world_,
                componentGizmoState_,
                selectedGizmoObjectId_,
                renderCamera.GetAspect());
        }
        RENDER3D::RenderViewContext primaryView{};
        primaryView.viewId = RENDER3D::kPrimaryRenderViewId;
        primaryView.purpose = runtimePlayActive_
            ? RENDER3D::RenderViewPurpose::Game
            : RENDER3D::RenderViewPurpose::EditorScene;
        primaryView.cameraFrame = &resolvedCameraFrame_;
        MODELRENDERER::RenderAll(primaryView, activeEnvironment, viewportDebugViewState_.renderView);
        SubmitFrozenCullingCameraDebugFrustum();
        RENDERER3D::RenderAll(renderCamera, static_cast<float>(captureW), static_cast<float>(captureH));
        VFX::Render(renderCamera);
#if defined(HIKARI_WITH_EDITOR)
        (void)RENDER3D::EDITORVIEW::RenderPending(
            activeEnvironment,
            sceneDocumentRevision_);
#endif
    }
    void DocumentSceneBase::RenderImGui() {
        if (!SERVICES::IsEditorUIEnabled() && !SERVICES::ArePortableObjectToolsEnabled()) {
            world_.RenderImGui();
        }
        componentGizmoRenderer_.DrawScreenSpaceGizmos(world_, componentGizmoState_, selectedGizmoObjectId_);
    }
    const std::string& DocumentSceneBase::GetSceneId() const {
        return sceneId_;
    }
    const std::string& DocumentSceneBase::GetScenePath() const {
        return scenePath_;
    }
    uint64_t DocumentSceneBase::GetSceneDocumentRevision() const noexcept {
        return sceneDocumentRevision_;
    }

    void DocumentSceneBase::SetSceneId(std::string sceneId) {
        sceneId_ = std::move(sceneId);
    }
    void DocumentSceneBase::SetScenePath(std::string scenePath) {
        scenePath_ = std::move(scenePath);
    }
    World& DocumentSceneBase::GetWorld() {
        return world_;
    }
    const World& DocumentSceneBase::GetWorld() const {
        return world_;
    }
    SceneDocument& DocumentSceneBase::GetSceneDocument() {
        return sceneDocument_;
    }
    const SceneDocument& DocumentSceneBase::GetSceneDocument() const {
        return sceneDocument_;
    }
    SceneEnvironment& DocumentSceneBase::GetSceneEnvironment() {
        return environment_;
    }
    const SceneEnvironment& DocumentSceneBase::GetSceneEnvironment() const {
        return environment_;
    }
    AssetRegistry& DocumentSceneBase::GetAssetRegistry() {
        return assetRegistry_;
    }

    const AssetRegistry& DocumentSceneBase::GetAssetRegistry() const {
        return assetRegistry_;
    }
    AssetDatabase& DocumentSceneBase::GetAssetDatabase() {
        return assetDatabase_;
    }
    const AssetDatabase& DocumentSceneBase::GetAssetDatabase() const {
        return assetDatabase_;
    }
    ModelManager& DocumentSceneBase::GetModelManager() {
        return modelManager_;
    }
    SkyManager& DocumentSceneBase::GetSkyManager() {
        return skyManager_;
    }
    ComponentRegistry& DocumentSceneBase::GetComponentRegistry() {
        return componentRegistry_;
    }

    SceneRuntimeBuilder& DocumentSceneBase::GetRuntimeBuilder() {
        return runtimeBuilder_;
    }

    const SceneRuntimeBuilder& DocumentSceneBase::GetRuntimeBuilder() const {
        return runtimeBuilder_;
    }
    Camera3D& DocumentSceneBase::GetCamera() {
        return camera_;
    }
    const Camera3D& DocumentSceneBase::GetCamera() const {
        return camera_;
    }
    CameraDirector& DocumentSceneBase::GetCameraDirector() {
        return cameraDirector_;
    }
    const CameraDirector& DocumentSceneBase::GetCameraDirector() const {
        return cameraDirector_;
    }
    const RENDER3D::ResolvedCameraFrame& DocumentSceneBase::GetResolvedCameraFrame() const {
        return resolvedCameraFrame_;
    }
    DebugCameraController3D& DocumentSceneBase::GetDebugCamera() {
        return debugCamera_;
    }
    bool& DocumentSceneBase::GetEnvironmentLightingEnabled() {
        return environmentLightingEnabled_;
    }
    void DocumentSceneBase::SetComponentGizmoState(const ComponentGizmoState& state) {
        componentGizmoState_ = state;
    }
    void DocumentSceneBase::SetViewportOverlayState(const ViewportOverlayState& state) {
        viewportOverlayState_ = state;
    }
    void DocumentSceneBase::SetViewportPerformanceState(const ViewportPerformanceState& state) {
        viewportPerformanceState_ = state;
    }
    void DocumentSceneBase::SetViewportDebugViewState(const ViewportDebugViewState& state) {
        viewportDebugViewState_ = state;
    }
    void DocumentSceneBase::SetSelectedGizmoObjectId(SceneObjectId id) {
        selectedGizmoObjectId_ = id;
    }
    void DocumentSceneBase::SyncReflectionProbeRuntimeFromAuthoring() {
        const ReflectionProbeSettings& probe = environment_.reflectionProbe;
        const REFLECTION::ReflectionProbeRuntimeData runtime = REFLECTION::GetActiveProbe();

        // Viewport 操作では現在の texture resource を保持し、authoring 値だけ同期する。
        REFLECTION::SetActiveProbeResources(
            probe.enabled,
            runtime.prefilteredResource,
            runtime.brdfLutResource,
            runtime.prefilteredMipCount,
            probe.position,
            probe.radius,
            probe.intensity,
            ToRuntimeInfluenceShape(probe.influenceShape),
            ToRuntimeProjectionShape(probe.projectionShape),
            probe.influenceBoxCenter,
            probe.influenceBoxSize,
            probe.projectionBoxCenter,
            probe.projectionBoxSize,
            probe.blendDistance,
            probe.priority,
            runtime.sourceAssetId,
            runtime.prefilteredPath,
            runtime.brdfLutPath);
    }
    bool DocumentSceneBase::ReloadAssets() {
        if (assetDatabase_.GetProjectRoot().empty()) {
            assetDatabase_.Initialize(std::filesystem::current_path());
        }
        const bool okDatabase = assetDatabase_.ScanAssets(true);

        // AssetDatabase 繧貞髪荳縺ｮ逋ｻ骭ｲ蜈・→縺励※ runtime descriptor 繧剃ｽ懊ｊ逶ｴ縺吶・
        assetRegistry_.Clear();
        AssetRegistryBuilder assetRegistryBuilder{};
        const bool okRegistry = assetRegistryBuilder.AppendToRegistry(assetDatabase_, assetRegistry_);

        ConfigureModelTextureResolver();
        RenderSubmissionSystem::InvalidateSceneResources(false);
        return okDatabase && okRegistry;
    }
    void DocumentSceneBase::ConfigureModelTextureResolver() {
        modelManager_.ResetTextureResolveStats();
        modelManager_.SetTexturePathResolver(
            [this](const std::string& sourceTexturePath, ModelTextureUsage usage) -> std::string {
                return ResolveModelTexturePathFromAssets(sourceTexturePath, usage);
            });
    }

    const AssetRecord* DocumentSceneBase::FindUniqueTextureAssetByFilename(
        const std::string& filename,
        const std::string& sourceTexturePath) const {

        if (filename.empty()) {
            return nullptr;
        }

        const std::string target = ToLowerCopy(filename);
        const AssetRecord* matchedRecord = nullptr;
        int matchCount = 0;

        for (const AssetRecord* record : assetDatabase_.CollectByType(AssetType::Texture)) {
            if (!record) {
                continue;
            }

            const std::string recordFilename = ToLowerCopy(record->sourcePath.filename().string());
            if (recordFilename != target) {
                continue;
            }

            matchedRecord = record;
            ++matchCount;
        }

        if (matchCount > 1) {
            modelManager_.RecordTextureResolveFailure(ModelTextureResolveFailureKind::Ambiguous);
            HIKARI_LOG_WARN("[ModelTextureResolver] fallback raw texture source=" +
                sourceTexturePath +
                " reason=ambiguous filename matches filename=" + filename +
                " count=" + std::to_string(matchCount));
            return nullptr;
        }

        return matchCount == 1 ? matchedRecord : nullptr;
    }

    std::string DocumentSceneBase::ResolveModelTexturePathFromAssets(
        const std::string& sourceTexturePath,
        ModelTextureUsage usage) const {

        if (sourceTexturePath.empty()) {
            return {};
        }

        std::filesystem::path sourcePath = std::filesystem::path(sourceTexturePath).lexically_normal();
        if (IsCookedTextureRuntimePath(sourcePath)) {
            return sourcePath.generic_string();
        }

        const AssetRecord* record = assetDatabase_.FindByPath(sourcePath);

        if (!record && !assetDatabase_.GetProjectRoot().empty()) {
            std::error_code ec{};
            const std::filesystem::path absolutePath = sourcePath.is_absolute()
                ? sourcePath.lexically_normal()
                : (assetDatabase_.GetProjectRoot() / sourcePath).lexically_normal();
            const std::filesystem::path relativePath =
                std::filesystem::relative(absolutePath, assetDatabase_.GetProjectRoot(), ec).lexically_normal();
            if (!ec && !relativePath.empty()) {
                record = assetDatabase_.FindByPath(relativePath);
            }
            if (!record) {
                record = assetDatabase_.FindByPath(absolutePath);
            }
        }

        if (!record) {
            record = FindUniqueTextureAssetByFilename(sourcePath.filename().string(), sourceTexturePath);
        }

        if (!record) {
            modelManager_.RecordTextureResolveFailure(ModelTextureResolveFailureKind::Missing);
            HIKARI_LOG_WARN("[ModelTextureResolver] fallback raw texture source=" +
                sourceTexturePath +
                " usage=" + ToModelTextureUsageText(usage) +
                " reason=texture asset not found");
            return sourceTexturePath;
        }

        if (record->type != AssetType::Texture || !record->guid.IsValid()) {
            modelManager_.RecordTextureResolveFailure(ModelTextureResolveFailureKind::Missing);
            HIKARI_LOG_WARN("[ModelTextureResolver] fallback raw texture source=" +
                sourceTexturePath +
                " usage=" + ToModelTextureUsageText(usage) +
                " reason=resolved asset is not a texture");
            return sourceTexturePath;
        }

        const auto* descriptor = assetRegistry_.FindAs<TextureAssetDescriptor>(AssetId{ record->guid.value });
        if (!descriptor || descriptor->sourcePath.empty()) {
            modelManager_.RecordTextureResolveFailure(ModelTextureResolveFailureKind::Missing);
            HIKARI_LOG_WARN("[ModelTextureResolver] fallback raw texture source=" +
                sourceTexturePath +
                " usage=" + ToModelTextureUsageText(usage) +
                " reason=texture descriptor missing");
            return sourceTexturePath;
        }

        return descriptor->sourcePath;
    }

    bool DocumentSceneBase::ReloadSceneDocument() {
        if (currentSceneAssetGuid_.IsValid()) {
            return OpenSceneAssetNow(currentSceneAssetGuid_);
        }
        return OpenStartupSceneAsset();
    }
    bool DocumentSceneBase::RebuildRuntimeWorld() {
        systemScheduler_.DetachWorld(world_);
        systemScheduler_.Clear();
        RenderSubmissionSystem::InvalidateSceneResources(true);

        SceneDependencySet deps = runtimeBuilder_.CollectDependencies(sceneDocument_);
        if (NeedsRuntimeDependencyRegistryRefresh(assetRegistry_, deps)) {
            // Editor 側で追加・再import された asset descriptor を runtime build 前に同期する。
            if (ReloadAssets()) {
                deps = runtimeBuilder_.CollectDependencies(sceneDocument_);
            } else {
                HIKARI_LOG_WARN("[SceneRuntime] asset registry refresh failed before runtime rebuild.");
            }
        }

        runtimeBuilder_.PreloadDependencies(
            deps,
            assetRegistry_,
            modelManager_,
            skyManager_,
            assetDatabase_.GetProjectRoot(),
            currentSceneAssetGuid_.value);
        const bool built = runtimeBuilder_.BuildWorldFromDocument(sceneDocument_, world_, assetRegistry_, componentRegistry_, modelManager_, skyManager_);

        environment_ = sceneDocument_.environment;
        environment_.directional.direction = MATH::Normalize(environment_.directional.direction);
        if (environment_.pointLights.empty()) {
            environment_.pointLights.push_back(PointLight{});
        }

        RuntimeSceneContext::SetCurrentWorld(&world_);
        RuntimeSceneContext::ResolvePendingSceneEntry(world_, sceneId_);

        if (built) {
            BuildSystemScheduleFromDocument();
            systemScheduler_.AttachWorld(world_);
        }
        ApplyCameraRuntimeChanges();
        return built;
    }
    bool DocumentSceneBase::ApplySystemRuntimeChanges() {
        systemScheduler_.DetachWorld(world_);
        systemScheduler_.Clear();
        RenderSubmissionSystem::InvalidateSceneResources(true);

        const bool configured = BuildSystemScheduleFromDocument();
        systemScheduler_.AttachWorld(world_);

        if (runtimePlayActive_) {
            runtimeSceneCameraActive_ = HasRuntimeSceneCameraDriver();
            runtimePreviewCameraActive_ = !runtimeSceneCameraActive_;
        }
        return configured;
    }
    bool DocumentSceneBase::ApplyCameraRuntimeChanges() {
        const SceneObjectId defaultCamera = sceneDocument_.camera.defaultCameraObjectId.value_or(SceneObjectId{});
        cameraDirector_.SetBaseCamera(defaultCamera);

        if (editorCameraPreviewObjectId_.value != 0) {
            const auto previewObject = std::find_if(
                sceneDocument_.objects.begin(),
                sceneDocument_.objects.end(),
                [this](const SceneObjectData& object) {
                    return object.id == editorCameraPreviewObjectId_;
                });
            bool previewCameraValid = previewObject != sceneDocument_.objects.end();
            if (previewCameraValid) {
                previewCameraValid = false;
                for (const auto& runtimeObject : world_.GetObjects()) {
                    if (!runtimeObject ||
                        !(runtimeObject->GetDocumentId() == editorCameraPreviewObjectId_)) {
                        continue;
                    }
                    const CameraComponent* camera = runtimeObject->GetComponent<CameraComponent>();
                    previewCameraValid = camera != nullptr && camera->IsEnabled();
                    break;
                }
            }
            if (!previewCameraValid) {
                EndEditorCameraPreview();
            }
        }

        if (runtimePlayActive_) {
            runtimeSceneCameraActive_ = HasRuntimeSceneCameraDriver();
            runtimePreviewCameraActive_ = !runtimeSceneCameraActive_;
        }
        return true;
    }
    bool DocumentSceneBase::SetGameDefaultCamera(SceneObjectId cameraObjectId) {
        if (cameraObjectId.value == 0) {
            return false;
        }

        const auto object = std::find_if(
            sceneDocument_.objects.begin(),
            sceneDocument_.objects.end(),
            [cameraObjectId](const SceneObjectData& candidate) {
                if (!(candidate.id == cameraObjectId)) {
                    return false;
                }
                return std::any_of(
                    candidate.components.begin(),
                    candidate.components.end(),
                    [](const SceneComponentData& component) {
                        return component.type == "CameraComponent";
                    });
            });
        if (object == sceneDocument_.objects.end()) {
            return false;
        }

        if (!sceneDocument_.camera.defaultCameraObjectId.has_value() ||
            !(sceneDocument_.camera.defaultCameraObjectId.value() == cameraObjectId)) {
            sceneDocument_.camera.defaultCameraObjectId = cameraObjectId;
            sceneDocumentDirty_ = true;
        }
        return ApplyCameraRuntimeChanges();
    }
    void DocumentSceneBase::ClearGameDefaultCamera() {
        if (!sceneDocument_.camera.defaultCameraObjectId.has_value()) {
            return;
        }
        sceneDocument_.camera.defaultCameraObjectId.reset();
        sceneDocumentDirty_ = true;
        ApplyCameraRuntimeChanges();
    }
    bool DocumentSceneBase::BeginEditorCameraPreview(
        SceneObjectId cameraObjectId,
        const CameraBlendDesc& blend,
        bool forceRestart) {

        if (runtimePlayActive_ || cameraObjectId.value == 0) {
            return false;
        }
        if (!forceRestart && IsEditorCameraPreviewActive() &&
            editorCameraPreviewObjectId_ == cameraObjectId) {
            return true;
        }

        GameObject* cameraObject = nullptr;
        for (const auto& object : world_.GetObjects()) {
            if (object && object->GetDocumentId() == cameraObjectId) {
                cameraObject = object.get();
                break;
            }
        }
        const CameraComponent* cameraComponent =
            cameraObject != nullptr ? cameraObject->GetComponent<CameraComponent>() : nullptr;
        if (cameraComponent == nullptr || !cameraComponent->IsEnabled()) {
            return false;
        }

        const bool replacingActivePreview =
            IsEditorCameraPreviewActive();
        if (replacingActivePreview) {
            (void)cameraDirector_.ReleaseOverride(
                editorCameraPreviewToken_);
            editorCameraPreviewObjectId_ = {};
            editorCameraPreviewToken_ = {};
        } else {
            editorCameraPreviewSnapshot_ = camera_;
        }
        cameraDirector_.SetBaseCamera(
            sceneDocument_.camera.defaultCameraObjectId.value_or(SceneObjectId{}));
        CameraActivationRequest request{};
        request.cameraObjectId = cameraObjectId;
        request.blend = blend;
        request.priority = 1000;
        request.affectsControlBasis = false;
        editorCameraPreviewToken_ = cameraDirector_.PushOverride(request);
        if (!editorCameraPreviewToken_.IsValid()) {
            return false;
        }
        editorCameraPreviewObjectId_ = cameraObjectId;
        return true;
    }
    bool DocumentSceneBase::UpdateEditorCameraPreview(
        const CinematicCameraEvaluation& evaluation) {

        if (!IsEditorCameraPreviewActive() || !evaluation.IsValid() ||
            !(editorCameraPreviewObjectId_ == evaluation.cameraObjectId)) {
            return false;
        }
        if (!evaluation.HasCameraAnimation()) {
            return cameraDirector_.ClearOverrideCamera(
                editorCameraPreviewToken_);
        }

        Camera3D sourceCamera{};
        if (!TryResolveCameraObjectView(
                evaluation.cameraObjectId,
                camera_.GetAspect(),
                sourceCamera)) {
            return false;
        }
        Camera3D evaluatedCamera{};
        return BuildEvaluatedCinematicCamera(
                evaluation,
                sourceCamera,
                camera_.GetAspect(),
                evaluatedCamera) &&
            cameraDirector_.SetOverrideCamera(
                editorCameraPreviewToken_,
                evaluatedCamera);
    }
    void DocumentSceneBase::EndEditorCameraPreview() {
        if (!IsEditorCameraPreviewActive()) {
            editorCameraPreviewObjectId_ = {};
            editorCameraPreviewToken_ = {};
            return;
        }
        (void)cameraDirector_.ReleaseOverride(editorCameraPreviewToken_);
        editorCameraPreviewObjectId_ = {};
        editorCameraPreviewToken_ = {};
        camera_ = editorCameraPreviewSnapshot_;
        resolvedCameraFrame_.camera = camera_;
        resolvedCameraFrame_.sourceCameraObjectId = 0;
        resolvedCameraFrame_.valid = true;
        editorCameraCutPending_ = true;
    }
    bool DocumentSceneBase::IsEditorCameraPreviewActive() const {
        return editorCameraPreviewObjectId_.value != 0 && editorCameraPreviewToken_.IsValid();
    }
    SceneObjectId DocumentSceneBase::GetEditorCameraPreviewObjectId() const {
        return editorCameraPreviewObjectId_;
    }
    bool DocumentSceneBase::TryResolveCameraObjectView(
        SceneObjectId cameraObjectId,
        float aspect,
        Camera3D& outCamera) const {

        return cameraDirector_.TryResolveCameraObject(
            world_,
            cameraObjectId,
            aspect,
            outCamera);
    }
    bool DocumentSceneBase::ApplyCameraObjectPose(
        SceneObjectId cameraObjectId,
        const MATH::Vec3& position,
        const MATH::Quat& rotation,
        bool markDirty) {

        if (runtimePlayActive_ || cameraObjectId.value == 0) {
            return false;
        }

        auto documentObject = std::find_if(
            sceneDocument_.objects.begin(),
            sceneDocument_.objects.end(),
            [cameraObjectId](const SceneObjectData& object) {
                return object.id == cameraObjectId;
            });
        if (documentObject == sceneDocument_.objects.end() ||
            documentObject->parent.has_value()) {
            return false;
        }

        GameObject* runtimeObject = nullptr;
        for (const auto& object : world_.GetObjects()) {
            if (object && object->GetDocumentId() == cameraObjectId) {
                runtimeObject = object.get();
                break;
            }
        }
        CameraComponent* cameraComponent =
            runtimeObject != nullptr
                ? runtimeObject->GetComponent<CameraComponent>()
                : nullptr;
        if (cameraComponent == nullptr) {
            return false;
        }

        const MATH::Quat normalizedRotation = MATH::NormalizeQ(rotation);
        documentObject->transform.position = position;
        documentObject->transform.rotationEulerDeg =
            MATH::EulerXYZDegreesFromQuatNearest(
                normalizedRotation,
                documentObject->transform.rotationEulerDeg);
        documentObject->transform.scale = { 1.0f, 1.0f, 1.0f };

        Transform3D& runtimeTransform = runtimeObject->Transform();
        runtimeTransform.position = position;
        runtimeTransform.rotation = normalizedRotation;
        runtimeTransform.scale = { 1.0f, 1.0f, 1.0f };
        runtimeTransform.useExplicitMatrix = false;
        runtimeObject->MarkRenderStateDirty();

        if (markDirty) {
            sceneDocumentDirty_ = true;
        }
        return true;
    }
    bool DocumentSceneBase::SnapCameraObjectToEditorView(SceneObjectId cameraObjectId) {
        const MATH::Quat rotation = MATH::Quat::FromEulerXYZ(
            -debugCamera_.GetPitch(),
            debugCamera_.GetYaw(),
            0.0f);
        return ApplyCameraObjectPose(
            cameraObjectId,
            debugCamera_.GetPosition(),
            rotation,
            true);
    }
    CinematicPlaybackHandle DocumentSceneBase::PlayCameraSequence(
        CinematicSequenceId sequenceId,
        const CinematicPlaybackOptions& options,
        float startTimeSeconds) {

        if (!runtimePlayActive_) {
            return {};
        }
        const CinematicSequence* sequence = FindCinematicSequence(
            sceneDocument_.cinematics,
            sequenceId);
        if (sequence == nullptr) {
            return {};
        }
        currentCameraSequenceHandle_ =
            sequencePlaybackService_.PlayInline(
                *sequence,
                {},
                options,
                startTimeSeconds,
                "Camera.Sequence",
                0,
                true);
        return currentCameraSequenceHandle_;
    }
    bool DocumentSceneBase::StopCameraSequence(
        CinematicPlaybackHandle handle) {

        const bool stopped = sequencePlaybackService_.Stop(handle);
        if (stopped && currentCameraSequenceHandle_ == handle) {
            currentCameraSequenceHandle_ = {};
        }
        return stopped;
    }
    bool DocumentSceneBase::PauseCameraSequence(
        CinematicPlaybackHandle handle) {

        return sequencePlaybackService_.Pause(handle);
    }
    bool DocumentSceneBase::ResumeCameraSequence(
        CinematicPlaybackHandle handle) {

        return sequencePlaybackService_.Resume(handle);
    }
    bool DocumentSceneBase::SeekCameraSequence(
        CinematicPlaybackHandle handle,
        float timeSeconds) {

        return sequencePlaybackService_.Seek(handle, timeSeconds);
    }
    bool DocumentSceneBase::IsCameraSequencePlaying() const noexcept {
        return sequencePlaybackService_.IsPlaying(
            currentCameraSequenceHandle_);
    }
    CinematicPlaybackHandle
    DocumentSceneBase::GetCameraSequencePlaybackHandle() const noexcept {
        return currentCameraSequenceHandle_;
    }
    SEQUENCER::SequencePlaybackHandle DocumentSceneBase::PlaySequence(
        const SEQUENCER::SequencePlayRequest& request) {

        return runtimePlayActive_
            ? sequencePlaybackService_.Play(request)
            : SEQUENCER::SequencePlaybackHandle{};
    }
    bool DocumentSceneBase::StopSequence(
        SEQUENCER::SequencePlaybackHandle handle) {

        return sequencePlaybackService_.Stop(handle);
    }
    bool DocumentSceneBase::PauseSequence(
        SEQUENCER::SequencePlaybackHandle handle) {

        return sequencePlaybackService_.Pause(handle);
    }
    bool DocumentSceneBase::ResumeSequence(
        SEQUENCER::SequencePlaybackHandle handle) {

        return sequencePlaybackService_.Resume(handle);
    }
    bool DocumentSceneBase::SeekSequence(
        SEQUENCER::SequencePlaybackHandle handle,
        float timeSeconds) {

        return sequencePlaybackService_.Seek(handle, timeSeconds);
    }
    uint64_t DocumentSceneBase::SubmitSequenceCommand(
        SEQUENCER::SequencePlaybackCommand command) {

        return runtimePlayActive_
            ? sequencePlaybackService_.Submit(std::move(command))
            : 0;
    }
    bool DocumentSceneBase::IsSequencePlaying(
        SEQUENCER::SequencePlaybackHandle handle) const noexcept {

        return sequencePlaybackService_.IsPlaying(handle);
    }
    std::vector<SEQUENCER::SequencePlaybackEvent>
        DocumentSceneBase::ConsumeSequencePlaybackEvents() {

        return sequencePlaybackService_.ConsumeEvents();
    }
    bool DocumentSceneBase::BeginRuntimePlay() {
        if (runtimePlayActive_ || !currentSceneAssetGuid_.IsValid()) {
            return false;
        }

        EndEditorCameraPreview();
        editorCameraSnapshot_ = camera_;
        editorDebugCameraSnapshot_ = debugCamera_;
        editorComponentGizmoSnapshot_ = componentGizmoState_;
        editorViewportOverlaySnapshot_ = viewportOverlayState_;
        editorViewportPerformanceSnapshot_ = viewportPerformanceState_;
        editorViewportDebugViewSnapshot_ = viewportDebugViewState_;
        editorSelectedGizmoObjectSnapshot_ = selectedGizmoObjectId_;
        if (!ReloadSceneDocument()) {
            camera_ = editorCameraSnapshot_;
            debugCamera_ = editorDebugCameraSnapshot_;
            HIKARI_LOG_ERROR("Runtime Play scene rebuild failed.");
            return false;
        }

        camera_ = editorCameraSnapshot_;
        gameplayCamera_ = editorCameraSnapshot_;
        sequencePlaybackService_.Reset();
        currentCameraSequenceHandle_ = {};
        cameraDirector_.Reset();
        cameraDirector_.SetBaseCamera(
            sceneDocument_.camera.defaultCameraObjectId.value_or(SceneObjectId{}));
        resolvedCameraFrame_ = {};
        runtimePreviewCamera_ = editorDebugCameraSnapshot_;
        runtimePreviewCamera_.ResetFromCamera(gameplayCamera_);
        runtimePreviewCamera_.SetEnabled(true);
        runtimeSceneCameraActive_ = HasRuntimeSceneCameraDriver();
        runtimePreviewCameraActive_ = !runtimeSceneCameraActive_;
        componentGizmoState_ = {};
        viewportOverlayState_ = {};
        viewportDebugViewState_ = {};
        selectedGizmoObjectId_ = {};
        runtimePlayActive_ = true;
        HIKARI_LOG_INFO("Document scene entered runtime Play state.");
        return true;
    }
    bool DocumentSceneBase::EndRuntimePlay() {
        if (!runtimePlayActive_) {
            return true;
        }

        runtimePlayActive_ = false;
        runtimePreviewCameraActive_ = false;
        runtimeSceneCameraActive_ = false;
        sequencePlaybackService_.Reset();
        currentCameraSequenceHandle_ = {};
        cameraDirector_.Reset();
        const bool restored = ReloadSceneDocument();
        camera_ = editorCameraSnapshot_;
        gameplayCamera_ = camera_;
        debugCamera_ = editorDebugCameraSnapshot_;
        componentGizmoState_ = editorComponentGizmoSnapshot_;
        viewportOverlayState_ = editorViewportOverlaySnapshot_;
        viewportPerformanceState_ = editorViewportPerformanceSnapshot_;
        viewportDebugViewState_ = editorViewportDebugViewSnapshot_;
        selectedGizmoObjectId_ = editorSelectedGizmoObjectSnapshot_;
        resolvedCameraFrame_ = {};
        resolvedCameraFrame_.camera = camera_;
        resolvedCameraFrame_.cameraCut = true;
        resolvedCameraFrame_.valid = true;
        editorCameraCutPending_ = true;
        if (!restored) {
            HIKARI_LOG_ERROR("Editor scene restoration after runtime Play failed.");
            return false;
        }
        HIKARI_LOG_INFO("Document scene restored after runtime Play.");
        return true;
    }
    bool DocumentSceneBase::ParkRuntimeForStandalone() {
        if (runtimeParkedForStandalone_) {
            return true;
        }
        if (runtimePlayActive_) {
            return false;
        }

        systemScheduler_.DetachWorld(world_);
        systemScheduler_.Clear();
        RuntimeSceneContext::SetCurrentWorld(nullptr);
        world_.Clear();
        RenderSubmissionSystem::InvalidateSceneResources(true);
        MODELRENDERER::Reset();
        modelManager_.UnloadAllAssets();
        skyManager_.Clear();
        SKYRENDERER::InvalidateSkyTextureCache();
        runtimeParkedForStandalone_ = true;
        HIKARI_LOG_INFO("Editor scene runtime parked for Standalone Game.");
        return true;
    }
    bool DocumentSceneBase::RestoreRuntimeAfterStandalone() {
        if (!runtimeParkedForStandalone_) {
            return true;
        }

        if (!ReloadSceneDocument()) {
            HIKARI_LOG_ERROR(
                "Editor scene runtime restoration after Standalone Game failed.");
            return false;
        }
        VFX::SetAssetRegistry(&assetRegistry_);
        runtimeParkedForStandalone_ = false;
        HIKARI_LOG_INFO("Editor scene runtime restored after Standalone Game.");
        return true;
    }
    bool DocumentSceneBase::RequestOpenSceneAsset(const AssetGuid& sceneGuid) {
        return OpenSceneAssetNow(sceneGuid);
    }
    bool DocumentSceneBase::OpenSceneAssetNow(const AssetGuid& sceneGuid) {
        if (!sceneGuid.IsValid()) {
            return false;
        }
        if (!SERVICES::IsRuntimeSceneGuidAllowed(sceneGuid.value)) {
            HIKARI_LOG_WARN("[SceneAsset] blocked scene outside runtime export set: " + sceneGuid.value);
            return false;
        }
        if (assetDatabase_.GetProjectRoot().empty()) {
            assetDatabase_.Initialize(std::filesystem::current_path());
        }

        const AssetRecord* record = assetDatabase_.FindByGuid(sceneGuid);
        if (!record) {
            assetDatabase_.ScanAssets(false);
            record = assetDatabase_.FindByGuid(sceneGuid);
        }
        if (!record || record->type != AssetType::Scene || record->sourcePath.empty()) {
            return false;
        }

        const std::filesystem::path scenePath =
            (assetDatabase_.GetProjectRoot() / record->sourcePath).lexically_normal();
        SceneDocument loaded{};
        if (!sceneSerializer_.LoadFromFile(scenePath.generic_string(), loaded)) {
            return false;
        }

        if (!runtimePlayActive_) {
            EndEditorCameraPreview();
        }
        sceneDocument_ = std::move(loaded);
        ++sceneDocumentRevision_;
        scenePath_ = scenePath.generic_string();
        sceneId_ = sceneGuid.value;
        currentSceneAssetGuid_ = sceneGuid;
        sceneDocumentDirty_ = false;

        environment_ = sceneDocument_.environment;
        ReloadAssets();
        return RebuildRuntimeWorld();
    }
    bool DocumentSceneBase::OpenStartupSceneAsset() {
        if (assetDatabase_.GetProjectRoot().empty()) {
            assetDatabase_.Initialize(std::filesystem::current_path());
        }

        // ProjectSettings 縺ｮ GUID 繧貞━蜈医＠縲∵悴險ｭ螳壹↑繧画怙蛻昴・ Scene Asset 繧呈治逕ｨ縺吶ｋ縲・
        assetDatabase_.ScanAssets(true);

        const std::string& runtimeStartupSceneGuid = SERVICES::GetRuntimeStartupSceneGuid();
        if (!runtimeStartupSceneGuid.empty() && OpenSceneAssetNow(AssetGuid{ runtimeStartupSceneGuid })) {
            return true;
        }

        ProjectSettingsService settings{};
        settings.Load(assetDatabase_.GetProjectRoot());

        const AssetGuid startupGuid = settings.GetSettings().startupSceneGuid;
        if (startupGuid.IsValid() &&
            SERVICES::IsRuntimeSceneGuidAllowed(startupGuid.value) &&
            OpenSceneAssetNow(startupGuid)) {
            return true;
        }

        std::vector<const AssetRecord*> sceneRecords = assetDatabase_.CollectByType(AssetType::Scene);
        std::sort(sceneRecords.begin(), sceneRecords.end(), [](const AssetRecord* lhs, const AssetRecord* rhs) {
            if (!lhs || !rhs) {
                return lhs < rhs;
            }
            return lhs->sourcePath.generic_string() < rhs->sourcePath.generic_string();
        });

        for (const AssetRecord* record : sceneRecords) {
            if (!record || !record->guid.IsValid()) {
                continue;
            }
            if (!SERVICES::IsRuntimeSceneGuidAllowed(record->guid.value)) {
                continue;
            }
            if (SERVICES::IsEditorHost()) {
                settings.SetStartupSceneGuid(record->guid);
                settings.Save();
            }
            return OpenSceneAssetNow(record->guid);
        }

        return false;
    }
    bool DocumentSceneBase::CreateTransientEmptySceneDocument() {
        EndEditorCameraPreview();
        sequencePlaybackService_.Reset();
        currentCameraSequenceHandle_ = {};
        cameraDirector_.Reset();
        sceneDocument_ = SceneDocument{};
        ++sceneDocumentRevision_;
        sceneDocument_.sceneName = "Untitled Scene";
        sceneDocument_.systems = CreateDefaultSceneSystems();
        environment_ = sceneDocument_.environment;
        scenePath_.clear();
        sceneId_ = "TransientScene";
        currentSceneAssetGuid_ = {};
        sceneDocumentDirty_ = false;
        RenderSubmissionSystem::InvalidateSceneResources(true);
        cameraDirector_.SetBaseCamera({});
        return true;
    }
    bool DocumentSceneBase::HasUnsavedSceneChanges() const {
        return sceneDocumentDirty_;
    }
    void DocumentSceneBase::SetUnsavedSceneChanges(bool dirty) {
        sceneDocumentDirty_ = dirty;
    }
    bool DocumentSceneBase::ApplyEnvironmentRuntimeChanges()
    {
        const bool skyResourceBindingChanged =
            IsSkyRuntimeResourceBindingChanged(
                sceneDocument_.environment.sky,
                environment_.sky);
        const bool reflectionProbeBindingChanged =
            IsReflectionProbeRuntimeBindingChanged(
                sceneDocument_.environment.reflectionProbe,
                environment_.reflectionProbe);

        sceneDocument_.environment = environment_;
        sceneDocumentDirty_ = true;

        if (!skyResourceBindingChanged && !reflectionProbeBindingChanged) {
            return true;
        }

        return RefreshLightingRuntime();
    }

    bool DocumentSceneBase::RefreshLightingRuntime() {
        SceneDependencySet deps{};
        if (!environment_.sky.skyAsset.empty()) {
            deps.skyAssetIds.insert(environment_.sky.skyAsset);
        }
        if (environment_.reflectionProbe.enabled &&
            !environment_.reflectionProbe.sourceCubemapAsset.empty()) {
            deps.reflectionProbeCubemapAssetIds.insert(environment_.reflectionProbe.sourceCubemapAsset);
            deps.reflectionProbeEnabled = true;
            deps.reflectionProbePosition = environment_.reflectionProbe.position;
            deps.reflectionProbeRadius = environment_.reflectionProbe.radius;
            deps.reflectionProbeIntensity = environment_.reflectionProbe.intensity;
            deps.reflectionProbeInfluenceShape = environment_.reflectionProbe.influenceShape;
            deps.reflectionProbeProjectionShape = environment_.reflectionProbe.projectionShape;
            deps.reflectionProbeInfluenceBoxCenter = environment_.reflectionProbe.influenceBoxCenter;
            deps.reflectionProbeInfluenceBoxSize = environment_.reflectionProbe.influenceBoxSize;
            deps.reflectionProbeProjectionBoxCenter = environment_.reflectionProbe.projectionBoxCenter;
            deps.reflectionProbeProjectionBoxSize = environment_.reflectionProbe.projectionBoxSize;
            deps.reflectionProbeBlendDistance = environment_.reflectionProbe.blendDistance;
            deps.reflectionProbePriority = environment_.reflectionProbe.priority;
        }
        LightProbeVolumeSettings lightProbe = sceneDocument_.lightingBake.lightProbeVolume;
        ClampLightProbeVolumeSettings(lightProbe);
        deps.lightProbeVolumeEnabled = lightProbe.enabled;
        deps.lightProbeVolumeIntensity = lightProbe.intensity;

        const bool ok = runtimeBuilder_.PreloadDependencies(
            deps,
            assetRegistry_,
            modelManager_,
            skyManager_,
            assetDatabase_.GetProjectRoot(),
            currentSceneAssetGuid_.value);

        SKYRENDERER::InvalidateSkyTextureCache();

        if (!ok) {
            HIKARI_LOG_WARN("[LightingRuntime] failed to preload lighting dependency.");
        }

        return ok;
    }

    bool DocumentSceneBase::RefreshSkyRuntime() {
        return RefreshLightingRuntime();
    }

    bool DocumentSceneBase::RefreshCurrentSkyRuntime() {
        return RefreshLightingRuntime();
    }

    bool DocumentSceneBase::RefreshTextureRuntimeByPath(const std::string& path) {
        if (path.empty()) {
            return false;
        }

        DXTEX::DxTextureManager::InvalidateTextureCacheByPath(path);
        return true;
    }

    bool DocumentSceneBase::ReloadModelAssetRuntime(const AssetId& modelId) {
        if (modelId.value.empty()) {
            return false;
        }

        const bool reloaded = modelManager_.ReloadAssetNow(modelId.value);
        if (reloaded) {
            RenderSubmissionSystem::InvalidateSceneResources(false);
        }
        return reloaded;
    }

    int DocumentSceneBase::RebindModelComponents() {
        int reboundCount = 0;
        SceneDependencySet deps = runtimeBuilder_.CollectDependencies(sceneDocument_);
        if (NeedsRuntimeDependencyRegistryRefresh(assetRegistry_, deps)) {
            if (ReloadAssets()) {
                deps = runtimeBuilder_.CollectDependencies(sceneDocument_);
            } else {
                HIKARI_LOG_WARN("[SceneRuntime] asset registry refresh failed before model rebind.");
            }
        }

        runtimeBuilder_.PreloadDependencies(
            deps,
            assetRegistry_,
            modelManager_,
            skyManager_,
            assetDatabase_.GetProjectRoot(),
            currentSceneAssetGuid_.value);

        world_.ForEachObjectWith<ModelComponent>(
            [this, &reboundCount](GameObject&, ModelComponent& modelComponent) {
                modelComponent.SetModelAsset(modelManager_.FindAsset(modelComponent.GetAssetId()));
                ++reboundCount;
            });

        RebuildMaterialOverrides();
        if (reboundCount > 0) {
            RenderSubmissionSystem::InvalidateSceneResources(false);
        }
        return reboundCount;
    }

    int DocumentSceneBase::RebuildMaterialOverrides() {
        int rebuiltCount = 0;
        MaterialRuntimeBuilder materialBuilder{};

        // Material override 縺ｯ scene load / refresh 譎ゅ□縺大・讒狗ｯ峨☆繧九・
        world_.ForEachObjectWith<ModelComponent>(
            [this, &rebuiltCount, &materialBuilder](GameObject&, ModelComponent& modelComponent) {
                modelComponent.ClearRuntimeMaterialOverride();
                for (const ModelMaterialOverrideSlot& slot : modelComponent.GetMaterialOverrides()) {
                    if (slot.slotIndex != 0 || !slot.materialAssetGuid.IsValid()) {
                        continue;
                    }

                    const auto* descriptor = assetRegistry_.FindAs<MaterialAssetDescriptor>(
                        AssetId{ slot.materialAssetGuid.value });
                    if (!descriptor) {
                        HIKARI_LOG_WARN("[MaterialRuntime] material asset not registered: " +
                            slot.materialAssetGuid.value);
                        continue;
                    }

                    auto runtimeMaterial = std::make_unique<Material>();
                    if (materialBuilder.BuildRuntimeMaterial(
                            descriptor->data,
                            assetRegistry_,
                            *runtimeMaterial,
                            descriptor->id.value)) {
                        modelComponent.SetRuntimeMaterialOverride(
                            std::move(runtimeMaterial),
                            slot.materialAssetGuid);
                        ++rebuiltCount;
                    }
                    break;
                }
            });

        return rebuiltCount;
    }

    int DocumentSceneBase::RebuildMaterialOverridesForMaterial(const AssetGuid& materialGuid) {
        if (!materialGuid.IsValid()) {
            return 0;
        }

        const auto* descriptor = assetRegistry_.FindAs<MaterialAssetDescriptor>(
            AssetId{ materialGuid.value });
        if (!descriptor) {
            HIKARI_LOG_WARN("[MaterialRuntime] material asset not registered: " + materialGuid.value);
            return 0;
        }

        int rebuiltCount = 0;
        MaterialRuntimeBuilder materialBuilder{};
        world_.ForEachObjectWith<ModelComponent>(
            [this, &rebuiltCount, &materialBuilder, &materialGuid, descriptor](GameObject&, ModelComponent& modelComponent) {
                for (const ModelMaterialOverrideSlot& slot : modelComponent.GetMaterialOverrides()) {
                    if (slot.slotIndex != 0 || slot.materialAssetGuid != materialGuid) {
                        continue;
                    }

                    auto runtimeMaterial = std::make_unique<Material>();
                    if (materialBuilder.BuildRuntimeMaterial(
                            descriptor->data,
                            assetRegistry_,
                            *runtimeMaterial,
                            descriptor->id.value)) {
                        modelComponent.SetRuntimeMaterialOverride(
                            std::move(runtimeMaterial),
                            materialGuid);
                        ++rebuiltCount;
                    }
                    break;
                }
            });

        return rebuiltCount;
    }

    int DocumentSceneBase::ApplyRuntimeMaterialOverridePreview(
        const AssetGuid& materialGuid,
        const PbrMaterialAssetData& data) {

        if (!materialGuid.IsValid()) {
            return 0;
        }

        int rebuiltCount = 0;
        MaterialRuntimeBuilder materialBuilder{};
        world_.ForEachObjectWith<ModelComponent>(
            [this, &rebuiltCount, &materialBuilder, &materialGuid, &data](GameObject&, ModelComponent& modelComponent) {
                for (const ModelMaterialOverrideSlot& slot : modelComponent.GetMaterialOverrides()) {
                    if (slot.slotIndex != 0 || slot.materialAssetGuid != materialGuid) {
                        continue;
                    }

                    auto runtimeMaterial = std::make_unique<Material>();
                    if (materialBuilder.BuildRuntimeMaterial(
                            data,
                            assetRegistry_,
                            *runtimeMaterial,
                            materialGuid.value + "/preview")) {
                        modelComponent.SetRuntimeMaterialOverride(
                            std::move(runtimeMaterial),
                            materialGuid);
                        ++rebuiltCount;
                    }
                    break;
                }
            });

        return rebuiltCount;
    }

    bool DocumentSceneBase::RequestReflectionProbeBake() {
        if (reflectionProbeBakeJob_ && IsBakeStateRunning(reflectionProbeBakeJob_->state)) {
            lastLightingBakeReport_ = reflectionProbeBakeJob_->report;
            lastLightingBakeReport_.warnings.push_back("Reflection probe bake is already running.");
            hasLastLightingBakeReport_ = true;
            return false;
        }
        if (lightProbeBakeJob_ && IsBakeStateRunning(lightProbeBakeJob_->state)) {
            lastLightingBakeReport_ = lightProbeBakeJob_->report;
            lastLightingBakeReport_.warnings.push_back("Light probe volume bake is already running.");
            hasLastLightingBakeReport_ = true;
            return false;
        }

        TOOLS::BAKING::LightingBakeReport report{};
        report.action = TOOLS::BAKING::LightingBakeAction::BakeReflectionProbes;
        report.target = TOOLS::BAKING::LightingBakeTarget::ReflectionProbesOnly;
        SetBakeJobState(report, TOOLS::BAKING::LightingBakeJobState::Requested);

        if (assetDatabase_.GetProjectRoot().empty()) {
            assetDatabase_.Initialize(std::filesystem::current_path());
        }

        const std::filesystem::path projectRoot = assetDatabase_.GetProjectRoot();
        if (projectRoot.empty()) {
            AddBakeError(report, "Project root is empty.");
        }
        if (!currentSceneAssetGuid_.IsValid()) {
            AddBakeError(report, "Current scene has no stable asset GUID. Save scene before baking.");
        }
        if (!environment_.reflectionProbe.enabled) {
            AddBakeError(report, "Reflection probe is disabled in the current scene.");
        }
        if (environment_.reflectionProbe.radius <= 0.0f) {
            AddBakeError(report, "Reflection probe radius must be greater than zero.");
        }

        report.bakeRoot = ASSETS::LIGHTING::BuildLightingBakeRoot(
            projectRoot,
            currentSceneAssetGuid_.value);
        report.manifestPath = ASSETS::LIGHTING::BuildLightingBakeManifestPath(
            projectRoot,
            currentSceneAssetGuid_.value);
            report.reflectionProbeCapturePath =
                ReflectionProbeOutputDirectory(projectRoot, currentSceneAssetGuid_.value) /
                "probe_000_capture.dds";
        report.reflectionProbePrefilteredPath =
            ReflectionProbeOutputDirectory(projectRoot, currentSceneAssetGuid_.value) /
            "probe_000_prefiltered.dds";
            report.reflectionProbeBrdfLutPath =
                (projectRoot / "Library" / "Generated" / "IBL" / "brdf_lut.dds").lexically_normal();
            report.reflectionProbeCaptureMode = "SceneCapture";
            report.reflectionProbeCaptureResolution = 128;

            if (!report.errors.empty()) {
            SetBakeJobState(report, TOOLS::BAKING::LightingBakeJobState::Failed);
            lastLightingBakeReport_ = report;
            hasLastLightingBakeReport_ = true;
            HIKARI_LOG_WARN("[LightingBake] reflection probe bake request failed.");
            return false;
        }

        auto job = std::make_unique<ReflectionProbeBakeJob>();
        job->state = TOOLS::BAKING::LightingBakeJobState::Requested;
        job->request.projectRoot = projectRoot;
        job->request.sceneGuid = currentSceneAssetGuid_.value;
        job->request.sceneName = GetCurrentSceneDisplayName();
        job->request.position = environment_.reflectionProbe.position;
        job->request.radius = environment_.reflectionProbe.radius;
        job->request.intensity = environment_.reflectionProbe.intensity;
        job->request.influenceShape = ReflectionProbeInfluenceShapeName(environment_.reflectionProbe.influenceShape);
        job->request.influenceBoxCenter = environment_.reflectionProbe.influenceBoxCenter;
        job->request.influenceBoxSize = environment_.reflectionProbe.influenceBoxSize;
        job->request.projectionShape = ReflectionProbeProjectionShapeName(environment_.reflectionProbe.projectionShape);
        job->request.projectionBoxCenter = environment_.reflectionProbe.projectionBoxCenter;
        job->request.projectionBoxSize = environment_.reflectionProbe.projectionBoxSize;
        job->request.blendDistance = environment_.reflectionProbe.blendDistance;
        job->request.priority = environment_.reflectionProbe.priority;
        job->request.resolution = 128;
        job->request.prefilteredMipCount = 7;
        job->request.prefilteredSampleCount = 128;
        job->request.brdfLutSize = 256;
        job->request.brdfSampleCount = 256;
        job->request.forceRebake = true;
        job->report = report;
        job->report.messages.push_back("Reflection probe scene capture requested.");

        reflectionProbeBakeJob_ = std::move(job);
        lastLightingBakeReport_ = reflectionProbeBakeJob_->report;
        hasLastLightingBakeReport_ = true;
        HIKARI_LOG_INFO("[LightingBake] reflection probe scene capture requested scene=" +
            currentSceneAssetGuid_.value);
        return true;
    }

    bool DocumentSceneBase::RequestLightProbeBake() {
        if (lightProbeBakeJob_ && IsBakeStateRunning(lightProbeBakeJob_->state)) {
            lastLightingBakeReport_ = lightProbeBakeJob_->report;
            lastLightingBakeReport_.warnings.push_back("Light probe volume bake is already running.");
            hasLastLightingBakeReport_ = true;
            return false;
        }
        if (reflectionProbeBakeJob_ && IsBakeStateRunning(reflectionProbeBakeJob_->state)) {
            lastLightingBakeReport_ = reflectionProbeBakeJob_->report;
            lastLightingBakeReport_.warnings.push_back("Reflection probe bake is already running.");
            hasLastLightingBakeReport_ = true;
            return false;
        }

        TOOLS::BAKING::LightingBakeReport report{};
        report.action = TOOLS::BAKING::LightingBakeAction::BakeLightProbes;
        report.target = TOOLS::BAKING::LightingBakeTarget::LightProbesOnly;
        SetBakeJobState(report, TOOLS::BAKING::LightingBakeJobState::Requested);

        if (assetDatabase_.GetProjectRoot().empty()) {
            assetDatabase_.Initialize(std::filesystem::current_path());
        }

        const std::filesystem::path projectRoot = assetDatabase_.GetProjectRoot();
        LightProbeVolumeSettings settings = sceneDocument_.lightingBake.lightProbeVolume;
        ClampLightProbeVolumeSettings(settings);

        if (projectRoot.empty()) {
            AddBakeError(report, "Project root is empty.");
        }
        if (!currentSceneAssetGuid_.IsValid()) {
            AddBakeError(report, "Current scene has no stable asset GUID. Save scene before baking.");
        }
        if (!settings.enabled) {
            AddBakeError(report, "Light probe volume is disabled in the current scene.");
        }

        const uint32_t probeCount = GetLightProbeVolumeProbeCount(settings);
        report.bakeRoot = ASSETS::LIGHTING::BuildLightingBakeRoot(
            projectRoot,
            currentSceneAssetGuid_.value);
        report.manifestPath = ASSETS::LIGHTING::BuildLightingBakeManifestPath(
            projectRoot,
            currentSceneAssetGuid_.value);
        report.lightProbeVolumePath = ASSETS::LIGHTING::BuildLightProbeVolumeOutputPath(
            projectRoot,
            currentSceneAssetGuid_.value);
        report.lightProbeProbeCount = probeCount;
        report.lightProbeCaptureResolution = settings.captureResolution;
        report.lightProbeMessages.push_back(
            "Light probe volume requested: probes=" + std::to_string(probeCount) +
            " faces=" + std::to_string(GetLightProbeCaptureFaceCount(settings)));

        if (!report.errors.empty()) {
            SetBakeJobState(report, TOOLS::BAKING::LightingBakeJobState::Failed);
            lastLightingBakeReport_ = report;
            hasLastLightingBakeReport_ = true;
            HIKARI_LOG_WARN("[LightingBake] light probe bake request failed.");
            return false;
        }

        auto job = std::make_unique<LightProbeBakeJob>();
        job->state = TOOLS::BAKING::LightingBakeJobState::Requested;
        job->request.projectRoot = projectRoot;
        job->request.sceneGuid = currentSceneAssetGuid_.value;
        job->request.sceneName = GetCurrentSceneDisplayName();
        job->request.settings = settings;
        job->request.forceRebake = true;
        job->probeCapturePaths.reserve(probeCount);
        job->report = report;
        job->report.messages.push_back("Light probe volume scene capture requested.");

        lightProbeBakeJob_ = std::move(job);
        lastLightingBakeReport_ = lightProbeBakeJob_->report;
        hasLastLightingBakeReport_ = true;
        HIKARI_LOG_INFO("[LightingBake] light probe volume scene capture requested scene=" +
            currentSceneAssetGuid_.value +
            " probes=" + std::to_string(probeCount));
        return true;
    }

    TOOLS::BAKING::LightingBakeJobState DocumentSceneBase::GetLightingBakeJobState() const {
        if (lightProbeBakeJob_ && IsBakeStateRunning(lightProbeBakeJob_->state)) {
            return lightProbeBakeJob_->state;
        }
        if (reflectionProbeBakeJob_ && IsBakeStateRunning(reflectionProbeBakeJob_->state)) {
            return reflectionProbeBakeJob_->state;
        }
        return hasLastLightingBakeReport_
            ? lastLightingBakeReport_.jobState
            : TOOLS::BAKING::LightingBakeJobState::Idle;
    }

    bool DocumentSceneBase::HasLastLightingBakeReport() const {
        return hasLastLightingBakeReport_;
    }

    const TOOLS::BAKING::LightingBakeReport& DocumentSceneBase::GetLastLightingBakeReport() const {
        return lastLightingBakeReport_;
    }

        bool DocumentSceneBase::RenderSceneForReflectionProbeCaptureFace(
            const Camera3D& faceCamera,
            const SceneEnvironment& captureEnvironment,
            uint32_t faceIndex) {

        if (!reflectionProbeBakeJob_ ||
            !reflectionProbeBakeJob_->captureTarget.BeginFace(faceIndex, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f)) {
            return false;
        }

        const std::string eventName =
            "ReflectionProbe.CaptureFace" + std::string(ProbeFaceName(faceIndex));
        GFX::PIX::ScopedGpuEvent pixFace(
            SERVICES::gCtx.cmdList,
            GFX::PIX::kColorRender,
            eventName.c_str());

        REFLECTION::ScopedReflectionProbeSamplingSuppress suppress{};

        RENDERER3D::Reset();
        MODELRENDERER::Reset();
        SKYRENDERER::Reset();

        const FrameContext& frame = HIKARI::TIME::GetFrameContext();
        RenderSubmissionSystem::SetAssetContext(&assetRegistry_, assetDatabase_.GetProjectRoot());
        systemScheduler_.PreRender(world_, frame);

        SKYRENDERER::Render(faceCamera, captureEnvironment, modelManager_, skyManager_);
        MODELRENDERER::RenderOpaqueForReflectionProbeCapture(
            faceCamera,
            captureEnvironment,
            reflectionProbeBakeJob_->captureTarget.GetResolution(),
            reflectionProbeBakeJob_->captureTarget.GetResolution(),
            MODELRENDERER::ModelRendererFrameKind::ReflectionProbeCapture);

        reflectionProbeBakeJob_->captureTarget.EndFace(faceIndex);
        return true;
    }

    bool DocumentSceneBase::RenderSceneForLightProbeCaptureFace(
        const Camera3D& faceCamera,
        const SceneEnvironment& captureEnvironment,
        uint32_t faceIndex) {

        if (!lightProbeBakeJob_ ||
            !lightProbeBakeJob_->captureTarget.BeginFace(faceIndex, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f)) {
            return false;
        }

        const std::string eventName =
            "LightProbe.CaptureFace" + std::string(ProbeFaceName(faceIndex));
        GFX::PIX::ScopedGpuEvent pixFace(
            SERVICES::gCtx.cmdList,
            GFX::PIX::kColorRender,
            eventName.c_str());

        REFLECTION::ScopedReflectionProbeSamplingSuppress reflectionSuppress{};
        RENDER3D::LIGHTPROBE::ScopedLightProbeVolumeSamplingSuppress lightProbeSuppress{};

        RENDERER3D::Reset();
        MODELRENDERER::Reset();
        SKYRENDERER::Reset();

        const FrameContext& frame = HIKARI::TIME::GetFrameContext();
        RenderSubmissionSystem::SetAssetContext(&assetRegistry_, assetDatabase_.GetProjectRoot());
        systemScheduler_.PreRender(world_, frame);

        SKYRENDERER::Render(faceCamera, captureEnvironment, modelManager_, skyManager_);
        MODELRENDERER::RenderOpaqueForReflectionProbeCapture(
            faceCamera,
            captureEnvironment,
            lightProbeBakeJob_->captureTarget.GetResolution(),
            lightProbeBakeJob_->captureTarget.GetResolution(),
            MODELRENDERER::ModelRendererFrameKind::LightProbeCapture);

        lightProbeBakeJob_->captureTarget.EndFace(faceIndex);
        return true;
    }

    bool DocumentSceneBase::ProcessReflectionProbeBakeJob() {
        if (!reflectionProbeBakeJob_) {
            return false;
        }

        ReflectionProbeBakeJob& job = *reflectionProbeBakeJob_;
        if (job.state == TOOLS::BAKING::LightingBakeJobState::Requested) {
            job.state = TOOLS::BAKING::LightingBakeJobState::Capturing;
            job.nextFaceIndex = 0;
            SetBakeJobState(job.report, job.state);
            lastLightingBakeReport_ = job.report;
            hasLastLightingBakeReport_ = true;

            const uint32_t resolution = std::clamp(job.request.resolution, 32u, 256u);
            if (!job.captureTarget.Initialize(resolution, DXGI_FORMAT_R16G16B16A16_FLOAT)) {
                AddBakeError(job.report, "Failed to initialize reflection probe capture target.");
                job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                SetBakeJobState(job.report, job.state);
                lastLightingBakeReport_ = job.report;
                return false;
            }
            job.report.reflectionProbeCaptureResolution = resolution;
            job.report.reflectionProbeCaptureFormat = "R16G16B16A16_FLOAT";
            job.report.messages.push_back(
                "Capture exclusions: reflection probe sampling suppressed; SSAO/post/debug/VFX helpers excluded.");
        }

        if (job.state == TOOLS::BAKING::LightingBakeJobState::Capturing) {
            if (job.nextFaceIndex >= 6u) {
                job.state = TOOLS::BAKING::LightingBakeJobState::WaitingGpu;
                SetBakeJobState(job.report, job.state);
                lastLightingBakeReport_ = job.report;
                return false;
            }

            SceneEnvironment captureEnvironment = environment_;
            captureEnvironment.reflectionProbe.enabled = false;
            captureEnvironment.ambientOcclusion.enabled = false;
            captureEnvironment.bloom.enabled = false;
            captureEnvironment.toneMapping.enabled = false;
            captureEnvironment.post.enabled = false;
            captureEnvironment.directionalShadow.enabled = false;
            captureEnvironment.showLightDebug = false;
            captureEnvironment.showPointLightMarkers = false;
            captureEnvironment.showSkyDebugInfo = false;

            const uint32_t face = job.nextFaceIndex;
            const Camera3D faceCamera = MakeProbeFaceCamera(
                job.request.position,
                face,
                (std::max)(4.0f, job.request.radius));
            bool captureOk = RenderSceneForReflectionProbeCaptureFace(
                faceCamera,
                captureEnvironment,
                face);

            std::string readbackMessage{};
            if (captureOk) {
                captureOk = job.captureTarget.QueueReadbackFace(face, &readbackMessage);
            }

            POST::PostSystem::RebindCurrentRenderTarget();
            RENDERER3D::Reset();
            MODELRENDERER::Reset();
            SKYRENDERER::Reset();

            if (!captureOk) {
                AddBakeError(job.report, readbackMessage.empty()
                    ? "Failed to render reflection probe scene capture."
                    : readbackMessage);
                job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                SetBakeJobState(job.report, job.state);
                lastLightingBakeReport_ = job.report;
                return true;
            }

            job.fenceValue = SERVICES::gCtx.currentFrameRetireFenceValue;
            job.report.gpuFenceValue = job.fenceValue;
            job.report.reflectionProbeSceneCaptured = true;
            job.report.reflectionProbeUsedSourceOverride = false;
            job.report.reflectionProbeCapturedFaceCount =
                (std::max)(job.report.reflectionProbeCapturedFaceCount, face + 1u);
            ++job.report.reflectionProbeQueuedReadbackFaceCount;
            job.report.messages.push_back(readbackMessage);
            job.report.messages.push_back("Face " + std::string(ProbeFaceName(face)) + " captured.");
            ++job.nextFaceIndex;

            if (job.nextFaceIndex >= 6u) {
                job.report.reflectionProbeCaptured = true;
                job.report.messages.push_back("Reflection probe GPU capture submitted. fence=" +
                std::to_string(job.fenceValue));
                job.state = TOOLS::BAKING::LightingBakeJobState::WaitingGpu;
            }
            SetBakeJobState(job.report, job.state);
            lastLightingBakeReport_ = job.report;
            HIKARI_LOG_INFO("[LightingBake] reflection probe capture face submitted face=" +
                std::to_string(face) + " fence=" + std::to_string(job.fenceValue));
            // Capture 中は通常描画で CameraCB を上書きしない。
            return true;
        }

        if (job.state == TOOLS::BAKING::LightingBakeJobState::WaitingGpu) {
            if (!SERVICES::gCore.IsFenceComplete(job.fenceValue)) {
                lastLightingBakeReport_ = job.report;
                return false;
            }

            job.state = TOOLS::BAKING::LightingBakeJobState::Finalizing;
            SetBakeJobState(job.report, job.state);
            lastLightingBakeReport_ = job.report;

            if (job.report.reflectionProbeCapturedFaceCount != 6u ||
                job.report.reflectionProbeQueuedReadbackFaceCount != 6u) {
                AddBakeError(job.report, "Reflection probe capture did not queue all six faces.");
                job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                SetBakeJobState(job.report, job.state);
                lastLightingBakeReport_ = job.report;
                return false;
            }

            std::string saveMessage{};
            if (!job.captureTarget.SaveReadbackToCubemapDds(
                    job.report.reflectionProbeCapturePath,
                    &saveMessage)) {
                AddBakeError(job.report, saveMessage);
                job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                SetBakeJobState(job.report, job.state);
                lastLightingBakeReport_ = job.report;
                return false;
            }

            job.report.messages.push_back(saveMessage);

            TOOLS::BAKING::ReflectionProbeBaker baker{};
            const TOOLS::BAKING::ReflectionProbeBakeResult bake =
                baker.FinalizeCapturedProbe(
                    job.request,
                    job.report.reflectionProbeCapturePath);

            job.report.reflectionProbeCaptured = bake.captured;
            job.report.reflectionProbePrefiltered = bake.prefiltered;
            job.report.reflectionProbeCaptureValidated = bake.captureValidated;
            job.report.reflectionProbePrefilterValidated = bake.prefilterValidated;
            job.report.reflectionProbeCapturedFaceCount = bake.capturedFaceCount;
            job.report.reflectionProbeCaptureResolution = bake.captureResolution;
            job.report.reflectionProbeCaptureMipCount = bake.captureMipCount;
            job.report.reflectionProbeCaptureFormat = bake.captureFormat;
            job.report.reflectionProbePrefilteredMipCount = bake.prefilteredMipCount;
            job.report.reflectionProbePrefilteredFormat = bake.prefilteredFormat;
            job.report.reflectionProbeFaceSummaries = bake.faceSummaries;
            job.report.bakeFolderCreated = bake.success;
            job.report.reflectionProbeCapturePath = bake.capturePath;
            job.report.reflectionProbePrefilteredPath = bake.prefilteredPath;
            job.report.reflectionProbeBrdfLutPath = bake.brdfLutPath;
            job.report.messages.insert(job.report.messages.end(), bake.messages.begin(), bake.messages.end());
            job.report.warnings.insert(job.report.warnings.end(), bake.warnings.begin(), bake.warnings.end());
            job.report.errors.insert(job.report.errors.end(), bake.errors.begin(), bake.errors.end());

            if (!bake.success) {
                job.report.success = false;
                job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                SetBakeJobState(job.report, job.state);
                lastLightingBakeReport_ = job.report;
                return false;
            }

            ASSETS::LIGHTING::LightingBakeManifest manifest =
                LoadOrCreateLightingBakeManifest(
                    job.report.manifestPath,
                    job.request.projectRoot,
                    job.request.sceneGuid,
                    job.report.warnings);
            ReplaceReflectionProbeRecord(manifest, bake.record);

            std::string manifestMessage{};
            if (!ASSETS::LIGHTING::SaveLightingBakeManifest(
                    job.report.manifestPath,
                    manifest,
                    &manifestMessage)) {
                AddBakeError(job.report, manifestMessage);
                job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                SetBakeJobState(job.report, job.state);
                lastLightingBakeReport_ = job.report;
                return false;
            }

            job.report.manifestWritten = true;
            job.report.reflectionProbeRecordWritten = true;
            job.report.reflectionProbeRecordCount =
                static_cast<uint32_t>(manifest.reflectionProbes.size());
            job.report.lightProbeRecordCount =
                static_cast<uint32_t>(manifest.lightProbes.size());
            job.report.lightmapRecordCount =
                static_cast<uint32_t>(manifest.lightmaps.size());
            job.report.messages.push_back(manifestMessage);
            job.report.messages.push_back("Reflection probe bake manifest updated: " +
                job.report.manifestPath.generic_string());

            RefreshTextureRuntimeByPath(bake.record.captureCubemapPath);
            RefreshTextureRuntimeByPath(bake.record.prefilteredCubemapPath);
            RefreshTextureRuntimeByPath(bake.record.brdfLutPath);
            RefreshLightingRuntime();

            job.state = TOOLS::BAKING::LightingBakeJobState::Completed;
            SetBakeJobState(job.report, job.state);
            lastLightingBakeReport_ = job.report;
            HIKARI_LOG_INFO("[LightingBake] reflection probe scene capture finalized scene=" +
                job.request.sceneGuid +
                " manifest=" + job.report.manifestPath.generic_string());
        }

        return false;
    }

    bool DocumentSceneBase::ProcessLightProbeBakeJob() {
        if (!lightProbeBakeJob_) {
            return false;
        }

        LightProbeBakeJob& job = *lightProbeBakeJob_;
        LightProbeVolumeSettings settings = job.request.settings;
        ClampLightProbeVolumeSettings(settings);

        if (job.state == TOOLS::BAKING::LightingBakeJobState::Requested) {
            job.state = TOOLS::BAKING::LightingBakeJobState::Capturing;
            job.nextProbeIndex = 0;
            job.nextFaceIndex = 0;
            job.probeCapturePaths.clear();
            SetBakeJobState(job.report, job.state);
            lastLightingBakeReport_ = job.report;
            hasLastLightingBakeReport_ = true;

            const uint32_t resolution = NormalizeLightProbeCaptureResolution(settings.captureResolution);
            if (!job.captureTarget.Initialize(resolution, DXGI_FORMAT_R16G16B16A16_FLOAT)) {
                AddBakeError(job.report, "Failed to initialize light probe capture target.");
                job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                SetBakeJobState(job.report, job.state);
                lastLightingBakeReport_ = job.report;
                return false;
            }
            job.report.lightProbeCaptureResolution = resolution;
            job.report.lightProbeProbeCount = GetLightProbeVolumeProbeCount(settings);
            job.report.lightProbeMessages.push_back(
                "Capture exclusions: reflection/light probe sampling suppressed; SSAO/post/debug/VFX helpers excluded.");
        }

        if (job.state == TOOLS::BAKING::LightingBakeJobState::Capturing) {
            const uint32_t probeCount = GetLightProbeVolumeProbeCount(settings);
            if (job.nextProbeIndex >= probeCount) {
                job.state = TOOLS::BAKING::LightingBakeJobState::ProjectingSH;
                SetBakeJobState(job.report, job.state);
                lastLightingBakeReport_ = job.report;
            } else {
                SceneEnvironment captureEnvironment = environment_;
                captureEnvironment.reflectionProbe.enabled = false;
                captureEnvironment.ambientOcclusion.enabled = false;
                captureEnvironment.bloom.enabled = false;
                captureEnvironment.toneMapping.enabled = false;
                captureEnvironment.post.enabled = false;
                captureEnvironment.directionalShadow.enabled = false;
                captureEnvironment.showLightDebug = false;
                captureEnvironment.showPointLightMarkers = false;
                captureEnvironment.showSkyDebugInfo = false;

                const uint32_t probe = job.nextProbeIndex;
                const uint32_t face = job.nextFaceIndex;
                const Camera3D faceCamera = MakeProbeFaceCamera(
                    GetLightProbePositionByIndex(settings, probe),
                    face,
                    GetLightProbeCaptureFarPlane(settings));
                bool captureOk = RenderSceneForLightProbeCaptureFace(
                    faceCamera,
                    captureEnvironment,
                    face);

                std::string readbackMessage{};
                if (captureOk) {
                    captureOk = job.captureTarget.QueueReadbackFace(face, &readbackMessage);
                }

                POST::PostSystem::RebindCurrentRenderTarget();
                RENDERER3D::Reset();
                MODELRENDERER::Reset();
                SKYRENDERER::Reset();

                if (!captureOk) {
                    AddBakeError(job.report, readbackMessage.empty()
                        ? "Failed to render light probe volume scene capture."
                        : readbackMessage);
                    job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                    SetBakeJobState(job.report, job.state);
                    lastLightingBakeReport_ = job.report;
                    return true;
                }

                job.fenceValue = SERVICES::gCtx.currentFrameRetireFenceValue;
                job.report.gpuFenceValue = job.fenceValue;
                job.report.lightProbeCurrentProbeIndex = probe;
                job.report.lightProbeCurrentFaceIndex = face;
                ++job.report.lightProbeCapturedFaceCount;
                ++job.report.lightProbeQueuedReadbackFaceCount;
                job.report.lightProbeMessages.push_back(
                    "Probe " + std::to_string(probe) +
                    " face " + std::string(ProbeFaceName(face)) +
                    " captured.");
                ++job.nextFaceIndex;

                if (job.nextFaceIndex >= 6u) {
                    job.report.lightProbeMessages.push_back(
                        "Light probe GPU capture submitted. probe=" +
                        std::to_string(probe) +
                        " fence=" + std::to_string(job.fenceValue));
                    job.state = TOOLS::BAKING::LightingBakeJobState::WaitingGpu;
                }
                SetBakeJobState(job.report, job.state);
                lastLightingBakeReport_ = job.report;
                HIKARI_LOG_INFO("[LightingBake] light probe capture face submitted probe=" +
                    std::to_string(probe) +
                    " face=" + std::to_string(face) +
                    " fence=" + std::to_string(job.fenceValue));
                // Capture 中は通常描画で CameraCB を上書きしない。
                return true;
            }
        }

        if (job.state == TOOLS::BAKING::LightingBakeJobState::WaitingGpu) {
            if (!SERVICES::gCore.IsFenceComplete(job.fenceValue)) {
                lastLightingBakeReport_ = job.report;
                return false;
            }

            const uint32_t probe = job.nextProbeIndex;
            const std::filesystem::path capturePath =
                LightProbeCapturePath(job.request.projectRoot, job.request.sceneGuid, probe);

            std::string saveMessage{};
            if (!job.captureTarget.SaveReadbackToCubemapDds(capturePath, &saveMessage)) {
                AddBakeError(job.report, saveMessage);
                job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                SetBakeJobState(job.report, job.state);
                lastLightingBakeReport_ = job.report;
                return false;
            }

            job.probeCapturePaths.push_back(capturePath);
            job.report.lightProbeMessages.push_back(saveMessage);
            job.report.bakeFolderCreated = true;
            ++job.nextProbeIndex;
            job.nextFaceIndex = 0;

            if (job.nextProbeIndex < GetLightProbeVolumeProbeCount(settings)) {
                job.state = TOOLS::BAKING::LightingBakeJobState::Capturing;
                job.report.lightProbeCurrentProbeIndex = job.nextProbeIndex;
                job.report.lightProbeCurrentFaceIndex = 0;
                SetBakeJobState(job.report, job.state);
                lastLightingBakeReport_ = job.report;
                return false;
            }

            job.state = TOOLS::BAKING::LightingBakeJobState::ProjectingSH;
            SetBakeJobState(job.report, job.state);
            lastLightingBakeReport_ = job.report;
        }

        if (job.state == TOOLS::BAKING::LightingBakeJobState::ProjectingSH) {
            TOOLS::BAKING::LightProbeBaker baker{};
            const TOOLS::BAKING::LightProbeBakeResult bake =
                baker.FinalizeCapturedVolume(job.request, job.probeCapturePaths);

            job.report.lightProbeBaked = bake.success;
            job.report.lightProbeVolumeWritten = bake.volumeWritten;
            job.report.lightProbeDebugJsonWritten = bake.debugJsonWritten;
            job.report.lightProbeProbeCount = bake.probeCount;
            job.report.lightProbeCaptureResolution = bake.captureResolution;
            job.report.lightProbeVolumePath = bake.volumePath;
            job.report.lightProbeDebugJsonPath = bake.debugJsonPath;
            job.report.lightProbeMessages.insert(
                job.report.lightProbeMessages.end(),
                bake.messages.begin(),
                bake.messages.end());
            job.report.warnings.insert(job.report.warnings.end(), bake.warnings.begin(), bake.warnings.end());
            job.report.errors.insert(job.report.errors.end(), bake.errors.begin(), bake.errors.end());

            if (!bake.success) {
                job.report.success = false;
                job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                SetBakeJobState(job.report, job.state);
                lastLightingBakeReport_ = job.report;
                return false;
            }

            job.state = TOOLS::BAKING::LightingBakeJobState::Saving;
            SetBakeJobState(job.report, job.state);
            lastLightingBakeReport_ = job.report;

            ASSETS::LIGHTING::LightingBakeManifest manifest =
                LoadOrCreateLightingBakeManifest(
                    job.report.manifestPath,
                    job.request.projectRoot,
                    job.request.sceneGuid,
                    job.report.warnings);
            ReplaceLightProbeVolumeRecord(manifest, bake.record);

            std::string manifestMessage{};
            if (!ASSETS::LIGHTING::SaveLightingBakeManifest(
                    job.report.manifestPath,
                    manifest,
                    &manifestMessage)) {
                AddBakeError(job.report, manifestMessage);
                job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                SetBakeJobState(job.report, job.state);
                lastLightingBakeReport_ = job.report;
                return false;
            }

            job.report.manifestWritten = true;
            job.report.lightProbeRecordWritten = true;
            job.report.reflectionProbeRecordCount =
                static_cast<uint32_t>(manifest.reflectionProbes.size());
            job.report.lightProbeRecordCount =
                static_cast<uint32_t>(manifest.lightProbes.size());
            job.report.lightmapRecordCount =
                static_cast<uint32_t>(manifest.lightmaps.size());
            job.report.messages.push_back(manifestMessage);
            job.report.messages.push_back("Light probe volume bake manifest updated: " +
                job.report.manifestPath.generic_string());

            RefreshLightingRuntime();
            job.report.lightProbeRuntimeLoaded = RENDER3D::LIGHTPROBE::IsValid();

            job.state = TOOLS::BAKING::LightingBakeJobState::Completed;
            SetBakeJobState(job.report, job.state);
            lastLightingBakeReport_ = job.report;
            HIKARI_LOG_INFO("[LightingBake] light probe volume scene capture finalized scene=" +
                job.request.sceneGuid +
                " manifest=" + job.report.manifestPath.generic_string());
        }

        return false;
    }

    bool DocumentSceneBase::SaveCurrentSceneDocument() {
        if (!currentSceneAssetGuid_.IsValid() || scenePath_.empty()) {
            return false;
        }

        sceneDocument_.environment = environment_;
        const bool saved = sceneSerializer_.SaveToFile(scenePath_, sceneDocument_);
        if (saved) {
            sceneDocumentDirty_ = false;
        }
        return saved;
    }
    bool DocumentSceneBase::SaveCurrentSceneDocumentAs(const AssetGuid& sceneGuid) {
        if (!sceneGuid.IsValid()) {
            return false;
        }
        if (assetDatabase_.GetProjectRoot().empty()) {
            assetDatabase_.Initialize(std::filesystem::current_path());
        }

        const AssetRecord* record = assetDatabase_.FindByGuid(sceneGuid);
        if (!record) {
            assetDatabase_.ScanAssets(false);
            record = assetDatabase_.FindByGuid(sceneGuid);
        }
        if (!record || record->type != AssetType::Scene || record->sourcePath.empty()) {
            return false;
        }

        const std::filesystem::path absolutePath =
            (assetDatabase_.GetProjectRoot() / record->sourcePath).lexically_normal();
        sceneDocument_.environment = environment_;
        if (!sceneSerializer_.SaveToFile(absolutePath.generic_string(), sceneDocument_)) {
            return false;
        }

        scenePath_ = absolutePath.generic_string();
        sceneId_ = sceneGuid.value;
        currentSceneAssetGuid_ = sceneGuid;
        sceneDocumentDirty_ = false;
        return true;
    }
    const AssetGuid& DocumentSceneBase::GetCurrentSceneAssetGuid() const {
        return currentSceneAssetGuid_;
    }
    bool DocumentSceneBase::IsCurrentSceneAsset(const AssetGuid& guid) const {
        return currentSceneAssetGuid_.IsValid() && currentSceneAssetGuid_ == guid;
    }
    std::string DocumentSceneBase::GetCurrentSceneDisplayName() const {
        if (currentSceneAssetGuid_.IsValid()) {
            if (const AssetRecord* record = assetDatabase_.FindByGuid(currentSceneAssetGuid_)) {
                if (!record->displayName.empty()) {
                    return record->displayName;
                }
            }
        }
        return sceneDocument_.sceneName;
    }
    void DocumentSceneBase::RegisterDefaultSystemTypes() {
        systemTypeRegistry_.Register(SystemTypeInfo{
            "ModelRenderSystem",
            [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                return std::make_unique<RenderSubmissionSystem>();
            }
        });
        systemTypeRegistry_.Register(SystemTypeInfo{
            "PlayerMovementSystem",
            [this](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                return std::make_unique<PlayerMovementSystem>(&gameplayCamera_);
            }
        });
        systemTypeRegistry_.Register(SystemTypeInfo{
            "AnimationSystem",
            [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                return std::make_unique<AnimationSystem>();
            }
        });
        systemTypeRegistry_.Register(SystemTypeInfo{
            "CameraFollowSystem",
            [this](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                return std::make_unique<CameraFollowSystem>(gameplayCamera_, runtimeSceneCameraActive_);
            }
        });
    }
    bool DocumentSceneBase::BuildSystemScheduleFromDocument() {
        if (sceneDocument_.systems.empty()) {
            sceneDocument_.systems = CreateDefaultSceneSystems();
        }

        bool fullyConfigured = true;
        for (const SceneSystemData& entry : sceneDocument_.systems) {
            if (!entry.enabled) {
                continue;
            }

            if (!systemTypeRegistry_.Find(entry.systemId)) {
                HIKARI_LOG_WARN(
                    "[SceneSystem] unavailable system skipped: " + entry.systemId);
                fullyConfigured = false;
                continue;
            }

            std::unique_ptr<ISystem> system =
                systemTypeRegistry_.Create(entry.systemId, entry.settings);
            if (!system) {
                HIKARI_LOG_ERROR(
                    "[SceneSystem] factory failed: " + entry.systemId);
                fullyConfigured = false;
                continue;
            }

            if (!systemScheduler_.AddSystem(
                    entry.systemId,
                    entry.executionOrder,
                    std::move(system))) {
                HIKARI_LOG_WARN(
                    "[SceneSystem] duplicate or invalid system skipped: " + entry.systemId);
                fullyConfigured = false;
            }
        }

        std::ostringstream schedule;
        const std::vector<std::string> order = systemScheduler_.GetExecutionOrder();
        for (size_t i = 0; i < order.size(); ++i) {
            if (i > 0) {
                schedule << ", ";
            }
            schedule << order[i];
        }
        HIKARI_LOG_INFO(
            "[SceneSystem] active schedule: " +
            (order.empty() ? std::string("<none>") : schedule.str()));
        return fullyConfigured;
    }
    void DocumentSceneBase::RegisterDefaultComponentTypes() {
        if (!componentRegistry_.Find("CameraComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "CameraComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<CameraComponent>(); },
                {},
                {},
                {},
                false,
                [](const SceneObjectData&, nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["verticalFovDegrees"] = 60.0f;
                    properties["nearClip"] = 0.1f;
                    properties["farClip"] = 100.0f;
                }
            });
        }
        if (!componentRegistry_.Find("ModelComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "ModelComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<ModelComponent>(); },
                {},
                { "AnimatorComponent" },
                {},
                false
            });
        }
        if (!componentRegistry_.Find("AnimatorComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "AnimatorComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<AnimatorComponent>(); },
                { "ModelComponent" },
                {},
                {},
                false,
                [](const SceneObjectData&, nlohmann::json& properties) {
                    properties["clip"] = "";
                    properties["timeSec"] = 0.0f;
                    properties["speed"] = 1.0f;
                    properties["loop"] = true;
                    properties["autoPlay"] = true;
                    properties["playing"] = true;
                    properties["finished"] = false;
                }
            });
        }

        if (!componentRegistry_.Find("PlayerControllerComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "PlayerControllerComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<PlayerControllerComponent>(); },
                { "AnimatorComponent" },
                { "CameraFollowComponent", "SceneScanFxComponent" },
                {},
                false,
                [](const SceneObjectData&, nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["moveXAxisName"] = "MoveX";
                    properties["moveYAxisName"] = "MoveY";
                    properties["moveSpeed"] = 4.0f;
                    properties["acceleration"] = 60.0f;
                    properties["deceleration"] = 72.0f;
                    properties["turnSpeed"] = 12.0f;
                    properties["inputDeadZone"] = 0.08f;
                    properties["rotateToMove"] = true;
                    properties["cameraRelativeMovement"] = true;
                    properties["useBounds"] = true;
                    properties["bounds"] = {
                        { "minX", -12.0f },
                        { "maxX", 12.0f },
                        { "minZ", -12.0f },
                        { "maxZ", 12.0f }
                    };
                    properties["animationEnabled"] = true;
                    properties["autoSelectAnimationClips"] = true;
                    properties["idleClip"] = "";
                    properties["moveClip"] = "";
                }
            });
        }

        if (!componentRegistry_.Find("CameraFollowComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "CameraFollowComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<CameraFollowComponent>(); },
                {},
                {},
                {},
                false,
                [](const SceneObjectData&, nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["targetObjectId"] = 0;
                    properties["useOwnerAsFallbackTarget"] = true;
                    properties["offset"] = nlohmann::json::array({ 0.0f, 5.5f, -7.5f });
                    properties["lookAtOffset"] = nlohmann::json::array({ 0.0f, 1.2f, 0.0f });
                    properties["followSmooth"] = 10.0f;
                    properties["lookSmooth"] = 12.0f;
                }
            });
        }

        if (!componentRegistry_.Find("UIButtonSceneTransitionComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "UIButtonSceneTransitionComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<UIButtonSceneTransitionComponent>(); },
                {},
                {},
                {},
                false,
                [](const SceneObjectData&, nlohmann::json& properties) {
                    properties["targetSceneAssetGuid"] = "";
                    properties["transitionProfileId"] = "noise_wipe";
                    properties["screenRect"] = {
                        { "x", 100.0f },
                        { "y", 100.0f },
                        { "w", 200.0f },
                        { "h", 80.0f }
                    };
                }
            });
        }

        if (!componentRegistry_.Find("SpawnPointComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "SpawnPointComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<SpawnPointComponent>(); },
                {},
                {},
                {},
                false,
                [](const SceneObjectData& object, nlohmann::json& properties) {
                    properties["spawnPointId"] = object.name.empty() ? "DefaultSpawn" : object.name;
                    properties["enabled"] = true;
                }
            });
        }

        if (!componentRegistry_.Find("TriggerVolumeComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "TriggerVolumeComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<TriggerVolumeComponent>(); },
                {},
                {},
                {},
                false,
                [](const SceneObjectData&, nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["boxSize"] = {
                        { "x", 1.0f },
                        { "y", 2.0f },
                        { "z", 1.0f }
                    };
                }
            });
        }


        if (!componentRegistry_.Find("VfxPlayerComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "VfxPlayerComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<VfxPlayerComponent>(); },
                {},
                {},
                {},
                false,
                [](const SceneObjectData&, nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["visible"] = true;
                    properties["slots"] = nlohmann::json::array({
                        {
                            { "slotName", "Default" },
                            { "effectAssetId", "" },
                            { "loop", false },
                            { "autoPlay", false },
                            { "restartIfAlreadyPlaying", true }
                        }
                    });
                }
            });
        }
        if (!componentRegistry_.Find("ComponentLinkComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "ComponentLinkComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<ComponentLinkComponent>(); },
                {},
                {},
                {},
                false
            });
        }
        if (!componentRegistry_.Find("DoorTransitionComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "DoorTransitionComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<DoorTransitionComponent>(); },
                { "TriggerVolumeComponent" },
                {},
                {},
                false,
                [](const SceneObjectData&, nlohmann::json& properties) {
                    properties["targetSceneAssetGuid"] = "";
                    properties["requireInteractKey"] = true;
                    properties["enabled"] = true;
                }
            });
        }
    }

    bool DocumentSceneBase::UseDebugCamera() const {
        return !runtimePlayActive_;
    }

    bool DocumentSceneBase::HasRuntimeSceneCameraDriver() const {
        if (sceneDocument_.camera.defaultCameraObjectId.has_value()) {
            for (const auto& object : world_.GetObjects()) {
                if (!object ||
                    !(object->GetDocumentId() == sceneDocument_.camera.defaultCameraObjectId.value())) {
                    continue;
                }
                const CameraComponent* camera = object->GetComponent<CameraComponent>();
                if (camera != nullptr && camera->IsEnabled()) {
                    return true;
                }
                break;
            }
        }

        const auto system = std::find_if(
            sceneDocument_.systems.begin(),
            sceneDocument_.systems.end(),
            [](const SceneSystemData& entry) {
                return entry.systemId == "CameraFollowSystem";
            });
        if (system != sceneDocument_.systems.end() && !system->enabled) {
            return false;
        }

        for (const auto& object : world_.GetObjects()) {
            if (!object) {
                continue;
            }
            const auto* follow = object->GetComponent<CameraFollowComponent>();
            if (follow != nullptr && follow->IsEnabled()) {
                return true;
            }
        }
        return false;
    }
    bool DocumentSceneBase::DrawDebugHelpers() const {
        return !runtimePlayActive_ && SERVICES::IsEditorUIEnabled();
    }
    bool DocumentSceneBase::UseEnvironmentLighting() const {
        return environmentLightingEnabled_;
    }

} // namespace HIKARI
