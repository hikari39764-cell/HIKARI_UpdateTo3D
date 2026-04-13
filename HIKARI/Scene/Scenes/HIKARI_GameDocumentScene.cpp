#include "HIKARI_GameDocumentScene.h"

namespace HIKARI {

    GameDocumentScene::GameDocumentScene(SceneCatalog& sceneCatalog, std::string sceneId)
        : DocumentSceneBase(sceneCatalog, std::move(sceneId)) {
    }

    const char* GameDocumentScene::GetSceneName() const {
        return sceneDocument_.sceneName.c_str();
    }

    void GameDocumentScene::RenderImGui() {
        // 正式运行时场景不显示编辑器 UI，仅保留组件的 runtime 调试绘制。
        DocumentSceneBase::RenderImGui();
    }

} // namespace HIKARI
