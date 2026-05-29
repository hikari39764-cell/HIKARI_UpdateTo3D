#pragma once

#include <string>

#include "HIKARI_DocumentSceneBase.h"

namespace HIKARI {

    class GameDocumentScene : public DocumentSceneBase {
    public:
        explicit GameDocumentScene(std::string sceneId);

        const char* GetSceneName() const override;
        void RenderImGui() override;

    };

} // namespace HIKARI
