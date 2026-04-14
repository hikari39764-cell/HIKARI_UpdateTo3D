#include "HIKARI_SandboxScene.h"

#include <utility>

namespace HIKARI {

    SandboxScene::SandboxScene(SceneCatalog& sceneCatalog, std::string sceneId)
        : DocumentSceneBase(sceneCatalog, std::move(sceneId)) {
    }

} // namespace HIKARI
