#pragma once

#include <string>

#include "HIKARI_DocumentSceneBase.h"

namespace HIKARI {

    class GameDocumentScene : public DocumentSceneBase {
    public:
        GameDocumentScene(SceneCatalog& sceneCatalog, std::string sceneId);

        const char* GetSceneName() const override;
        void RenderImGui() override;

    };

} // namespace HIKARI
