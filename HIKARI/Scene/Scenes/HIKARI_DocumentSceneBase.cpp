#include "HIKARI_DocumentSceneBase.h"

#include <filesystem>
#include <cctype>
#include <utility>
#include <numbers>
#include <memory>
#include <algorithm>
#include <vector>

#include "HIKARI_3D.h"
#include "HIKARI_Services.h"
#include "Assets/HIKARI_AssetRegistryBuilder.h"
#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/HIKARI_LightDebugDraw.h"
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
		// デフォルトのシーンシステムのリストを作成する。各システムは、名前、アクティブ状態、更新順序、および初期化パラメータを持つ。
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
			// スカイのランタイムリソースバインディングが変更されたかどうかを判断する
            return before.skyAsset != after.skyAsset ||
                   before.mode != after.mode;
        }
      
    }
    
    DocumentSceneBase::DocumentSceneBase(SceneCatalog& sceneCatalog, std::string sceneId)
        : sceneCatalog_(sceneCatalog), sceneId_(std::move(sceneId)) {
    }
	// シーンが開始されるときに呼び出される
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
	// シーンが終了するときに呼び出される
    void DocumentSceneBase::OnExit() {
        systemScheduler_.DetachWorld(world_);
        systemScheduler_.Clear();
        RuntimeSceneContext::SetCurrentWorld(nullptr);
    }
	// シーンの更新を行う。dt には前のフレームからの経過時間が秒単位で渡される。
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
	// シーンの描画を行う
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
		// 環境設定を正規化してから描画に渡す。特に、環境光と点光源の強度は、環境光が無効な場合は 0 にする。
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
		// モデルの描画を行う
        MODELRENDERER::RenderAll(camera_, activeEnvironment);
		// モデルの描画が完了した後に、RenderSubmissionSystem を通じて他のシステムが描画に参加できるようにする
        RENDERER3D::RenderAll(camera_, static_cast<float>(captureW), static_cast<float>(captureH));
        VFX::Render(camera_);
    }
	// ImGui を使ったエディタ UI の描画を行う
    void DocumentSceneBase::RenderImGui() {
        if (!SERVICES::IsEditorUIEnabled()) {
            world_.RenderImGui();
        }
        componentGizmoRenderer_.DrawScreenSpaceGizmos(world_, componentGizmoState_, selectedGizmoObjectId_);
    }
	// シーンの ID を取得する
    const std::string& DocumentSceneBase::GetSceneId() const {
        return sceneId_;
    }
	// シーンのファイルパスを取得する
    const std::string& DocumentSceneBase::GetScenePath() const {
        return scenePath_;
    }
	// シーンカタログへの参照を取得する
    SceneCatalog& DocumentSceneBase::GetSceneCatalog() {
        return sceneCatalog_;
    }
	// シーンカタログへの const 参照を取得する
    const SceneCatalog& DocumentSceneBase::GetSceneCatalog() const {
        return sceneCatalog_;
    }
	// シーンの ID を設定する
    void DocumentSceneBase::SetSceneId(std::string sceneId) {
        sceneId_ = std::move(sceneId);
    }
	// シーンのファイルパスを設定する
    void DocumentSceneBase::SetScenePath(std::string scenePath) {
        scenePath_ = std::move(scenePath);
    }
	// シーン内のオブジェクトやシステムを管理する World オブジェクトへの参照を取得する
    World& DocumentSceneBase::GetWorld() {
        return world_;
    }
	// シーン内のオブジェクトやシステムを管理する World オブジェクトへの const 参照を取得する
    const World& DocumentSceneBase::GetWorld() const {
        return world_;
    }
	// シーンのドキュメントデータへの参照を取得する。シーンのドキュメントは、シーン内のオブジェクトや環境設定などのデータを保持する構造体である。
    SceneDocument& DocumentSceneBase::GetSceneDocument() {
        return sceneDocument_;
    }
	// シーンのドキュメントデータへの const 参照を取得する
    const SceneDocument& DocumentSceneBase::GetSceneDocument() const {
        return sceneDocument_;
    }
	// シーンの環境設定への参照を取得する。環境設定には、環境光や空の設定などが含まれる。
    SceneEnvironment& DocumentSceneBase::GetSceneEnvironment() {
        return environment_;
    }
    // シーンの環境設定への const 参照を取得する
    const SceneEnvironment& DocumentSceneBase::GetSceneEnvironment() const {
        return environment_;
    }
	// アセットレジストリへの参照を取得する。アセットレジストリは、プロジェクト内のアセットの情報を管理するクラスである。
    AssetRegistry& DocumentSceneBase::GetAssetRegistry() {
        return assetRegistry_;
    }
	// アセットレジストリへの const 参照を取得する
    AssetDatabase& DocumentSceneBase::GetAssetDatabase() {
        return assetDatabase_;
    }
	// アセットデータベースへの const 参照を取得する。アセットデータベースは、プロジェクト内のアセットの物理的なファイルパスや GUID などの情報を管理するクラスである。
    const AssetDatabase& DocumentSceneBase::GetAssetDatabase() const {
        return assetDatabase_;
    }
	// モデルマネージャへの参照を取得する。モデルマネージャは、3D モデルのアセットを管理し、ロードやアクセスを提供するクラスである。
    ModelManager& DocumentSceneBase::GetModelManager() {
        return modelManager_;
    }
    // スカイマネージャへの参照を取得する。スカイマネージャは、3D シーンの空の表現を管理するクラスである。
    SkyManager& DocumentSceneBase::GetSkyManager() {
        return skyManager_;
    }
	// コンポーネントレジストリへの参照を取得する。コンポーネントレジストリは、シーン内のオブジェクトにアタッチされるコンポーネントの種類やデータ構造を管理するクラスである。
    ComponentRegistry& DocumentSceneBase::GetComponentRegistry() {
        return componentRegistry_;
    }
	// カメラへの参照を取得する。カメラは、シーンの描画に使用される視点を表すクラスである。
    Camera3D& DocumentSceneBase::GetCamera() {
        return camera_;
    }
	// カメラへの const 参照を取得する
    const Camera3D& DocumentSceneBase::GetCamera() const {
        return camera_;
    }
	// デバッグカメラコントローラーへの参照を取得する。デバッグカメラコントローラーは、エディタでシーンを操作するためのカメラコントローラーである。
    DebugCameraController3D& DocumentSceneBase::GetDebugCamera() {
        return debugCamera_;
    }
	// デバッグカメラコントローラーへの const 参照を取得する
    bool& DocumentSceneBase::GetEnvironmentLightingEnabled() {
        return environmentLightingEnabled_;
    }
	// 環境光の有効状態を取得する
    void DocumentSceneBase::SetComponentGizmoState(const ComponentGizmoState& state) {
        componentGizmoState_ = state;
    }
	// ビューポートのオーバーレイ表示の状態を設定する
    void DocumentSceneBase::SetViewportOverlayState(const ViewportOverlayState& state) {
        viewportOverlayState_ = state;
    }
	// 現在選択されているギズモオブジェクトの ID を設定する
    void DocumentSceneBase::SetSelectedGizmoObjectId(SceneObjectId id) {
        selectedGizmoObjectId_ = id;
    }
	// アセットの再読み込みを行う。アセットデータベースをスキャンして最新の状態に更新し、アセットレジストリを再構築する。
    bool DocumentSceneBase::ReloadAssets() {
        if (assetDatabase_.GetProjectRoot().empty()) {
            assetDatabase_.Initialize(std::filesystem::current_path());
        }
        const bool okDatabase = assetDatabase_.ScanAssets(true);

        assetRegistry_.Clear();
        AssetRegistryBuilder assetRegistryBuilder{};
        const bool okRegistry = assetRegistryBuilder.AppendToRegistry(assetDatabase_, assetRegistry_);

        ConfigureModelTextureResolver();
        return okDatabase && okRegistry;
    }
    // モデル材質のテクスチャ参照を AssetDatabase 経由で解決する。
    void DocumentSceneBase::ConfigureModelTextureResolver() {
        // ModelManager は AssetDatabase を直接知らず、上位層から解決関数だけを受け取る。
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
            // glTF/MTL の相対参照は、同名が一意な場合だけ補助的に解決する。
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

	// 現在のシーンドキュメントを再読み込みする。現在のシーンアセットの GUID が有効であれば、そのアセットを開き直す。そうでなければ、スタートアップシーンアセットを開く。
    bool DocumentSceneBase::ReloadSceneDocument() {
        if (currentSceneAssetGuid_.IsValid()) {
            return OpenSceneAssetNow(currentSceneAssetGuid_);
        }
        return OpenStartupSceneAsset();
    }
	// ランタイムのワールドを再構築する。シーンドキュメントの内容に基づいて、ワールド内のオブジェクトやシステムを構築し直す。
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
	// 指定されたシーンアセットの GUID を使って、そのシーンアセットを開くことを要求する。実際のオープン処理は OpenSceneAssetNow で行われる。
    bool DocumentSceneBase::RequestOpenSceneAsset(const AssetGuid& sceneGuid) {
        return OpenSceneAssetNow(sceneGuid);
    }
	// 指定されたシーンアセットの GUID を使って、そのシーンアセットを今すぐ開く。GUID が有効でない場合や、アセットが見つからない場合は false を返す。
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
	// スタートアップシーンアセットを開く。プロジェクト設定でスタートアップシーンの GUID が指定されていればそのシーンを開き、そうでなければプロジェクト内の最初のシーンアセットを開く。
    bool DocumentSceneBase::OpenStartupSceneAsset() {
        if (assetDatabase_.GetProjectRoot().empty()) {
            assetDatabase_.Initialize(std::filesystem::current_path());
        }

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
	// 一時的な空のシーンドキュメントを作成する。これは、保存されていない新しいシーンを表すために使用される。
    bool DocumentSceneBase::CreateTransientEmptySceneDocument() {
        // 一時シーンは Asset ではない。保存先を選ぶまで GUID を持たせない。
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
	// シーンドキュメントに保存されていない変更があるかどうかを返す
    bool DocumentSceneBase::HasUnsavedSceneChanges() const {
        return sceneDocumentDirty_;
    }
	// シーンドキュメントの変更が保存されていない状態を設定する。dirty が true の場合は変更があるとみなし、false の場合は変更がないとみなす。
    void DocumentSceneBase::SetUnsavedSceneChanges(bool dirty) {
        sceneDocumentDirty_ = dirty;
    }
	// 現在のシーンドキュメントをファイルに保存する。現在のシーンアセットの GUID が有効であり、シーンパスが設定されている場合にのみ保存を試みる。保存に成功した場合は true を返し、そうでない場合は false を返す。
    // Environment panel の変更を SceneDocument へ反映し、必要な場合だけ Sky runtime を更新する。
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

        // Sky だけを再登録し、World 全体の再構築は避ける。
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

    // 現在の Scene Asset へ SceneDocument を保存する。
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
	// 現在のシーンドキュメントを、指定されたシーンアセットの GUID を持つファイルに保存する。GUID が有効でない場合や、アセットが見つからない場合は false を返す。保存に成功した場合は true を返す。
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
	// 現在のシーンアセットの GUID を取得する。GUID が有効でない場合は、現在のシーンがアセットとして保存されていないことを意味する。
    const AssetGuid& DocumentSceneBase::GetCurrentSceneAssetGuid() const {
        return currentSceneAssetGuid_;
    }
	// 指定された GUID が現在のシーンアセットの GUID と等しいかどうかを返す。これにより、特定のシーンアセットが現在のシーンとして開かれているかどうかを確認できる。
    bool DocumentSceneBase::IsCurrentSceneAsset(const AssetGuid& guid) const {
        return currentSceneAssetGuid_.IsValid() && currentSceneAssetGuid_ == guid;
    }
	// 現在のシーンの表示名を取得する。現在のシーンアセットの GUID が有効であれば、そのアセットの表示名を返す。そうでなければ、シーンドキュメントの sceneName を返す。
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
	// デフォルトのシーンシステムを登録する。これには、アニメーションシステムやレンダリングサブミッションシステムなどが含まれる。
    void DocumentSceneBase::RegisterDefaultSystems() {
        systemScheduler_.AddSystem(std::make_unique<AnimationSystem>());
        systemScheduler_.AddSystem(std::make_unique<RenderSubmissionSystem>());
    }
	// デフォルトのコンポーネントタイプを登録する
    //これには、モデルコンポーネントやアニメーターコンポーネントなどが含まれる。各コンポーネントタイプは、名前、インスタンス化関数、依存関係、相互排他関係、プロパティの初期化関数などの情報を持つ。
    void DocumentSceneBase::RegisterDefaultComponentTypes() {
		// ModelComponent は、シーン内のオブジェクトに 3D モデルを割り当てるための基本的なコンポーネントである。多くのオブジェクトがモデルを持つ可能性があるため、複数インスタンスを許可する。
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
		// AnimatorComponent は ModelComponent に依存する。ModelComponent がないと AnimatorComponent は意味をなさないため、ModelComponent を必須コンポーネントとして指定する。
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
		// ComponentLinkComponent は、シーン内のオブジェクト同士をリンクするための汎用的なコンポーネントである。特定の依存関係はないが、他のコンポーネントと組み合わせて使用されることが多い。
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

    void DocumentSceneBase::RegisterDefaultSceneCatalogEntries() {
        // SceneCatalog は旧 sceneId 互換型として残すが、通常の Scene 発見は AssetDatabase が担当する。
    }

	// デバッグカメラを使用するかどうかを返す
    bool DocumentSceneBase::UseDebugCamera() const {
        return true;
    }
	// デバッグヘルパー（グリッド、軸、ライトの補助線など）を描画するかどうかを返す。通常はエディタ表示中のみ描画する。
    bool DocumentSceneBase::DrawDebugHelpers() const {
        return SERVICES::IsEditorUIEnabled();
    }
	// 環境光を使用するかどうかを返す
    bool DocumentSceneBase::UseEnvironmentLighting() const {
        return environmentLightingEnabled_;
    }

} // namespace HIKARI
