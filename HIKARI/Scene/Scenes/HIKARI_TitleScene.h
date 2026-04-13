#pragma once

#include "HIKARI_GameDocumentScene.h"

namespace HIKARI {

    class TitleScene final : public GameDocumentScene {
    public:
        TitleScene(SceneCatalog& sceneCatalog, std::string sceneId)
            : GameDocumentScene(sceneCatalog, std::move(sceneId)) {
        }

        const char* GetSceneName() const override { return "TitleScene"; }
    };

} // namespace HIKARI
