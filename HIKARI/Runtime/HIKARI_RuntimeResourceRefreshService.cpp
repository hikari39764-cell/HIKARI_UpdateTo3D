#include "HIKARI_RuntimeResourceRefreshService.h"

#include <utility>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Core/HIKARI_Logger.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4100)
#endif
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace HIKARI {

    RuntimeResourceRefreshReport RuntimeResourceRefreshService::RefreshAsset(
        DocumentSceneBase& scene,
        const AssetId& assetId) {

        RuntimeResourceRefreshReport report{};
        scene.ReloadAssets();

        const AssetRegistry& registry = scene.GetAssetRegistry();
        if (const auto* texture = registry.FindAs<TextureAssetDescriptor>(assetId)) {
            if (RefreshTextureAsset(scene, *texture, report)) {
                ReloadCurrentSceneModelDependencies(scene, report);
            }
        } else if (const auto* sky = registry.FindAs<SkyAssetDescriptor>(assetId)) {
            RefreshSkyAsset(scene, *sky, report);
        } else if (const auto* model = registry.FindAs<ModelAssetDescriptor>(assetId)) {
            RefreshModelAsset(scene, *model, report);
        } else if (const auto* material = registry.FindAs<MaterialAssetDescriptor>(assetId)) {
            RefreshMaterialAsset(scene, *material, report);
        } else {
            ++report.failedCount;
            AppendMessage(report, "Unsupported or missing asset: " + assetId.value);
        }

        return report;
    }

    RuntimeResourceRefreshReport RuntimeResourceRefreshService::RefreshAssets(
        DocumentSceneBase& scene,
        const std::vector<AssetId>& assetIds) {

        RuntimeResourceRefreshReport report{};
        scene.ReloadAssets();

        bool textureTouched = false;
        const AssetRegistry& registry = scene.GetAssetRegistry();
        for (const AssetId& assetId : assetIds) {
            if (const auto* texture = registry.FindAs<TextureAssetDescriptor>(assetId)) {
                textureTouched = RefreshTextureAsset(scene, *texture, report) || textureTouched;
            } else if (const auto* sky = registry.FindAs<SkyAssetDescriptor>(assetId)) {
                RefreshSkyAsset(scene, *sky, report);
            } else if (const auto* model = registry.FindAs<ModelAssetDescriptor>(assetId)) {
                RefreshModelAsset(scene, *model, report);
            } else if (const auto* material = registry.FindAs<MaterialAssetDescriptor>(assetId)) {
                RefreshMaterialAsset(scene, *material, report);
            } else {
                ++report.failedCount;
                AppendMessage(report, "Unsupported or missing asset: " + assetId.value);
            }
        }

        if (textureTouched) {
            ReloadCurrentSceneModelDependencies(scene, report);
        }

        return report;
    }
	/// すべてのリソースをリフレッシュする。モデルの再ロードとスカイの即時差し替えを行い、モデルコンポーネントの再バインドも試みる。
    RuntimeResourceRefreshReport RuntimeResourceRefreshService::RefreshCurrentSceneResources(
        DocumentSceneBase& scene) {

        RuntimeResourceRefreshReport report{};
        scene.ReloadAssets();

        const SceneDependencySet dependencies =
            scene.GetRuntimeBuilder().CollectDependencies(scene.GetSceneDocument());

        const AssetRegistry& registry = scene.GetAssetRegistry();
        for (const std::string& modelId : dependencies.modelAssetIds) {
            if (const auto* model = registry.FindAs<ModelAssetDescriptor>(AssetId{ modelId })) {
                RefreshModelAsset(scene, *model, report);
            } else {
                ++report.failedCount;
                AppendMessage(report, "Missing model dependency: " + modelId);
            }
        }

        for (const std::string& skyId : dependencies.skyAssetIds) {
            if (const auto* sky = registry.FindAs<SkyAssetDescriptor>(AssetId{ skyId })) {
                RefreshSkyAsset(scene, *sky, report);
            } else {
                ++report.failedCount;
                AppendMessage(report, "Missing sky dependency: " + skyId);
            }
        }

        for (const std::string& materialId : dependencies.materialAssetIds) {
            if (const auto* material = registry.FindAs<MaterialAssetDescriptor>(AssetId{ materialId })) {
                RefreshMaterialAsset(scene, *material, report);
            } else {
                ++report.failedCount;
                AppendMessage(report, "Missing material dependency: " + materialId);
            }
        }

        report.modelReboundComponentCount += scene.RebindModelComponents();
        scene.RefreshCurrentSkyRuntime();
        return report;
    }
	// Texture は cache を失効させ、モデル再ロード時に resolver 経由で再取得する。
    bool RuntimeResourceRefreshService::RefreshTextureAsset(
        DocumentSceneBase& scene,
        const TextureAssetDescriptor& descriptor,
        RuntimeResourceRefreshReport& report) {

        if (descriptor.sourcePath.empty()) {
            ++report.failedCount;
            AppendMessage(report, "Texture descriptor sourcePath is empty: " + descriptor.id.value);
            return false;
        }

        // Texture は cache を失効させ、モデル再ロード時に resolver 経由で再取得する。
        if (!scene.RefreshTextureRuntimeByPath(descriptor.sourcePath)) {
            ++report.failedCount;
            AppendMessage(report, "Texture cache invalidation failed: " + descriptor.sourcePath);
            return false;
        }

        ++report.textureInvalidatedCount;
        AppendMessage(report, "Invalidated texture: " + descriptor.sourcePath);
        return true;
    }
	// Sky は即時差し替えを試みる。非アクティブ sky は次回選択時に読む。
    bool RuntimeResourceRefreshService::RefreshSkyAsset(
        DocumentSceneBase& scene,
        const SkyAssetDescriptor& descriptor,
        RuntimeResourceRefreshReport& report) {

        if (descriptor.sourcePath.empty()) {
            ++report.failedCount;
            AppendMessage(report, "Sky descriptor sourcePath is empty: " + descriptor.id.value);
            return false;
        }

        if (scene.GetSceneEnvironment().sky.skyAsset != descriptor.id.value) {
            AppendMessage(report, "Sky asset is not active: " + descriptor.id.value);
            return true;
        }

        // 現在使っている sky だけを即時差し替える。非アクティブ sky は次回選択時に読む。
        if (!scene.RefreshCurrentSkyRuntime()) {
            ++report.failedCount;
            AppendMessage(report, "Sky runtime refresh failed: " + descriptor.id.value);
            return false;
        }

        ++report.skyInvalidatedCount;
        AppendMessage(report, "Refreshed sky: " + descriptor.id.value);
        return true;
    }
	// Model は再ロードを試みる。成功すればモデルコンポーネントの再バインドも行う。
    bool RuntimeResourceRefreshService::RefreshModelAsset(
        DocumentSceneBase& scene,
        const ModelAssetDescriptor& descriptor,
        RuntimeResourceRefreshReport& report) {

        if (!scene.ReloadModelAssetRuntime(descriptor.id)) {
            ++report.failedCount;
            AppendMessage(report, "Failed to reload model: " + descriptor.id.value);
            return false;
        }

        ++report.modelReloadedCount;
        report.modelReboundComponentCount += scene.RebindModelComponents();
        AppendMessage(report, "Reloaded model: " + descriptor.id.value);
        return true;
    }

    bool RuntimeResourceRefreshService::RefreshMaterialAsset(
        DocumentSceneBase& scene,
        const MaterialAssetDescriptor& descriptor,
        RuntimeResourceRefreshReport& report) {

        const int rebuilt = scene.RebuildMaterialOverrides();
        ++report.materialReloadedCount;
        report.materialReboundComponentCount += rebuilt;
        AppendMessage(report, "Rebuilt material override: " + descriptor.id.value);
        return true;
    }
	// 現在のシーンで使われているモデルの依存関係を再評価し、必要に応じてモデルを再ロードする。Texture は cache を失効させるだけなので、個別の再ロードは行わない。
    void RuntimeResourceRefreshService::ReloadCurrentSceneModelDependencies(
        DocumentSceneBase& scene,
        RuntimeResourceRefreshReport& report) {

        const SceneDependencySet dependencies =
            scene.GetRuntimeBuilder().CollectDependencies(scene.GetSceneDocument());

        const AssetRegistry& registry = scene.GetAssetRegistry();
        for (const std::string& modelId : dependencies.modelAssetIds) {
            if (const auto* model = registry.FindAs<ModelAssetDescriptor>(AssetId{ modelId })) {
                RefreshModelAsset(scene, *model, report);
            }
        }
    }
	// リフレッシュの過程で発生したメッセージをレポートに追加し、同時にログにも出す。
    void RuntimeResourceRefreshService::AppendMessage(
        RuntimeResourceRefreshReport& report,
        std::string message) const {

        HIKARI_LOG_INFO("[RuntimeRefresh] " + message);
        report.messages.push_back(std::move(message));
    }

} // namespace HIKARI
