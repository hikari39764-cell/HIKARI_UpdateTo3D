#include "HIKARI_DocumentSceneBase.h"

#include <filesystem>
#include <cctype>
#include <utility>
#include <numbers>
#include <memory>
#include <algorithm>
#include <vector>

#include "HIKARI_3D.h"
#include "HIKARI_DxTexture.h"
#include "HIKARI_Services.h"
#include "Assets/HIKARI_AssetRegistryBuilder.h"
#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/HIKARI_LightDebugDraw.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Scene/HIKARI_AnimationSystem.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/HIKARI_DoorTransitionComponent.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/Components/HIKARI_SpawnPointComponent.h"
#include "Scene/Components/HIKARI_TriggerVolumeComponent.h"
#include "Scene/Components/HIKARI_UIButtonSceneTransitionComponent.h"
#include "Vfx/Runtime/HIKARI_VfxAsset.h"
#include "Vfx/Runtime/HIKARI_VfxSystem.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include "Vfx/Post/HIKARI_PostProfile.h"
#include "Scene/Components/HIKARI_ComponentLinkComponent.h"
#include "Scene/Components/HIKARI_VfxPlayerComponent.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

namespace HIKARI {
    namespace {
        // Scene Asset で開く通常 scene の標準 System 一覧。
        std::vector<SceneSystemData> CreateDefaultSceneSystems() {
            return {
                SceneSystemData{ "TransformSystem", true, 0, nlohmann::json::object() },
                SceneSystemData{ "ModelRenderSystem", true, 100, nlohmann::json::object() },
                SceneSystemData{ "AnimationSystem", true, 150, nlohmann::json::object() },
                SceneSystemData{ "VfxSystem", true, 200, nlohmann::json::object() },
                SceneSystemData{ "PhysicsSystem", false, 300, nlohmann::json::object() },
                SceneSystemData{ "ScriptSystem", false, 400, nlohmann::json::object() },
            };
        }

        std::string ToLowerCopy(std::string value) {
            for (char& ch : value) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        const char* ToModelTextureUsageText(ModelTextureUsage usage) {
            switch (usage) {
            case ModelTextureUsage::BaseColor: return "BaseColor";
            case ModelTextureUsage::Normal: return "Normal";
            case ModelTextureUsage::MetallicRoughness: return "MetallicRoughness";
            case ModelTextureUsage::Occlusion: return "Occlusion";
            case ModelTextureUsage::Emissive: return "Emissive";
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
      
    }
    
    DocumentSceneBase::DocumentSceneBase(std::string sceneId)
        : sceneId_(std::move(sceneId)) {
    }
    void DocumentSceneBase::OnEnter() {
        camera_.SetPerspective(60.0f * std::numbers::pi_v<float> / 180.0f, static_cast<float>(kScreenW) / static_cast<float>(kScreenH), 0.1f, 100.0f);
        debugCamera_.Reset({ 0.0f, 2.0f, -6.0f }, 0.0f, 0.0f);

        RegisterDefaultComponentTypes();

        ReloadAssets();
        VFX::SetAssetRegistry(&assetRegistry_);
        if (!OpenStartupSceneAsset()) {
            CreateTransientEmptySceneDocument();
        }
        if (RebuildRuntimeWorld()) {
            systemScheduler_.Clear();
            RegisterDefaultSystems();
            systemScheduler_.AttachWorld(world_);
        }
    }
    void DocumentSceneBase::OnExit() {
        systemScheduler_.DetachWorld(world_);
        systemScheduler_.Clear();
        RuntimeSceneContext::SetCurrentWorld(nullptr);
    }
    void DocumentSceneBase::Update(float dt) {
        const FrameContext& frame = HIKARI::TIME::GetFrameContext();
        if (UseDebugCamera()) {
            debugCamera_.Update(dt, camera_);
        }
        systemScheduler_.PreUpdate(world_, frame);
        world_.Update(dt);
        systemScheduler_.Update(world_, frame);
        systemScheduler_.LateUpdate(world_, frame);
    }
    void DocumentSceneBase::Render() {
        int captureW = 0;
        int captureH = 0;
        POST::PostSystem::GetSceneCaptureSize(captureW, captureH);
        if (captureW > 0 && captureH > 0) {
            camera_.SetPerspective(
                60.0f * std::numbers::pi_v<float> / 180.0f,
                static_cast<float>(captureW) / static_cast<float>(captureH),
                0.1f,
                100.0f);
        }

        RENDERER3D::Reset();
        MODELRENDERER::Reset();
        SKYRENDERER::Reset();

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
        systemScheduler_.PreRender(world_, frame);
        systemScheduler_.Render(world_, frame);
        systemScheduler_.PostRender(world_, frame);
        SceneEnvironment activeEnvironment = environment_;
        activeEnvironment.directional.direction = MATH::Normalize(activeEnvironment.directional.direction);
        if (!UseEnvironmentLighting()) {
            activeEnvironment.directional.intensity = 0.0f;
            activeEnvironment.ambient.intensity = 0.0f;
            activeEnvironment.specularIntensity = 0.0f;
            for (PointLight& pointLight : activeEnvironment.pointLights) {
                pointLight.intensity = 0.0f;
            }
        }

        SKYRENDERER::Render(camera_, activeEnvironment, modelManager_, skyManager_);
        if (DrawDebugHelpers()) {
            LIGHTDEBUGDRAW::SubmitDirectionalLightArrow(activeEnvironment.directional.direction, activeEnvironment);
            LIGHTDEBUGDRAW::SubmitPointLightDebug(activeEnvironment);
        }

        componentGizmoRenderer_.SubmitWorldGizmos(world_, componentGizmoState_, selectedGizmoObjectId_);
        MODELRENDERER::RenderAll(camera_, activeEnvironment);
        RENDERER3D::RenderAll(camera_, static_cast<float>(captureW), static_cast<float>(captureH));
        VFX::Render(camera_);
    }
    void DocumentSceneBase::RenderImGui() {
        if (!SERVICES::IsEditorUIEnabled()) {
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
    void DocumentSceneBase::SetSelectedGizmoObjectId(SceneObjectId id) {
        selectedGizmoObjectId_ = id;
    }
    bool DocumentSceneBase::ReloadAssets() {
        if (assetDatabase_.GetProjectRoot().empty()) {
            assetDatabase_.Initialize(std::filesystem::current_path());
        }
        const bool okDatabase = assetDatabase_.ScanAssets(true);

        // AssetDatabase を唯一の登録元として runtime descriptor を作り直す。
        assetRegistry_.Clear();
        AssetRegistryBuilder assetRegistryBuilder{};
        const bool okRegistry = assetRegistryBuilder.AppendToRegistry(assetDatabase_, assetRegistry_);

        ConfigureModelTextureResolver();
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
        const SceneDependencySet deps = runtimeBuilder_.CollectDependencies(sceneDocument_);
        runtimeBuilder_.PreloadDependencies(deps, assetRegistry_, modelManager_, skyManager_);
        const bool built = runtimeBuilder_.BuildWorldFromDocument(sceneDocument_, world_, assetRegistry_, componentRegistry_, modelManager_, skyManager_);

        environment_ = sceneDocument_.environment;
        environment_.directional.direction = MATH::Normalize(environment_.directional.direction);
        if (environment_.pointLights.empty()) {
            environment_.pointLights.push_back(PointLight{});
        }

        RuntimeSceneContext::SetCurrentWorld(&world_);
        RuntimeSceneContext::ResolvePendingSceneEntry(world_, sceneId_);

        return built;
    }
    bool DocumentSceneBase::RequestOpenSceneAsset(const AssetGuid& sceneGuid) {
        return OpenSceneAssetNow(sceneGuid);
    }
    bool DocumentSceneBase::OpenSceneAssetNow(const AssetGuid& sceneGuid) {
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

        const std::filesystem::path scenePath =
            (assetDatabase_.GetProjectRoot() / record->sourcePath).lexically_normal();
        SceneDocument loaded{};
        if (!sceneSerializer_.LoadFromFile(scenePath.generic_string(), loaded)) {
            return false;
        }

        sceneDocument_ = std::move(loaded);
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

        // ProjectSettings の GUID を優先し、未設定なら最初の Scene Asset を採用する。
        assetDatabase_.ScanAssets(true);

        ProjectSettingsService settings{};
        settings.Load(assetDatabase_.GetProjectRoot());

        const AssetGuid startupGuid = settings.GetSettings().startupSceneGuid;
        if (startupGuid.IsValid() && OpenSceneAssetNow(startupGuid)) {
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
            settings.SetStartupSceneGuid(record->guid);
            settings.Save();
            return OpenSceneAssetNow(record->guid);
        }

        return false;
    }
    bool DocumentSceneBase::CreateTransientEmptySceneDocument() {
        sceneDocument_ = SceneDocument{};
        sceneDocument_.sceneName = "Untitled Scene";
        sceneDocument_.systems = CreateDefaultSceneSystems();
        environment_ = sceneDocument_.environment;
        scenePath_.clear();
        sceneId_ = "TransientScene";
        currentSceneAssetGuid_ = {};
        sceneDocumentDirty_ = false;
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

        sceneDocument_.environment = environment_;
        sceneDocumentDirty_ = true;

        if (!skyResourceBindingChanged) {
            return true;
        }

        return RefreshSkyRuntime();
    }

    bool DocumentSceneBase::RefreshSkyRuntime() {
        SceneDependencySet deps{};
        if (!environment_.sky.skyAsset.empty()) {
            deps.skyAssetIds.insert(environment_.sky.skyAsset);
        }

        const bool ok = runtimeBuilder_.PreloadDependencies(
            deps,
            assetRegistry_,
            modelManager_,
            skyManager_);

        SKYRENDERER::InvalidateSkyTextureCache();

        if (!ok) {
            HIKARI_LOG_WARN("[SkyRuntime] failed to preload sky dependency.");
        }

        return ok;
    }

    bool DocumentSceneBase::RefreshCurrentSkyRuntime() {
        return RefreshSkyRuntime();
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

        return modelManager_.ReloadAssetNow(modelId.value);
    }

    int DocumentSceneBase::RebindModelComponents() {
        int reboundCount = 0;

        world_.ForEachObjectWith<ModelComponent>(
            [this, &reboundCount](GameObject&, ModelComponent& modelComponent) {
                modelComponent.SetModelAsset(modelManager_.FindAsset(modelComponent.GetAssetId()));
                ++reboundCount;
            });

        RebuildMaterialOverrides();
        return reboundCount;
    }

    int DocumentSceneBase::RebuildMaterialOverrides() {
        int rebuiltCount = 0;
        MaterialRuntimeBuilder materialBuilder{};

        // Material override は scene load / refresh 時だけ再構築する。
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
    void DocumentSceneBase::RegisterDefaultSystems() {
        systemScheduler_.AddSystem(std::make_unique<AnimationSystem>());
        systemScheduler_.AddSystem(std::make_unique<RenderSubmissionSystem>());
    }
    void DocumentSceneBase::RegisterDefaultComponentTypes() {
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
        return true;
    }
    bool DocumentSceneBase::DrawDebugHelpers() const {
        return SERVICES::IsEditorUIEnabled();
    }
    bool DocumentSceneBase::UseEnvironmentLighting() const {
        return environmentLightingEnabled_;
    }

} // namespace HIKARI
