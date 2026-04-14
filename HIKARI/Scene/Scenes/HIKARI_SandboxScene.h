#pragma once

#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

namespace HIKARI {

    class SandboxScene final : public DocumentSceneBase {
    public:
        SandboxScene(SceneCatalog& sceneCatalog, std::string sceneId = "Sandbox");

        const char* GetSceneName() const override { return "SandboxScene"; }

    private:
        bool DrawDebugHelpers() const override { return true; }
    };

} // namespace HIKARI
