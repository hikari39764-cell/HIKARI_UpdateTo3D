#include "HIKARI_EngineApp.h"

#include <fstream>
#include <memory>

#include <json.hpp>

#include "HIKARI_Services.h"

namespace HIKARI {

    namespace {
        struct StartupConfig {
            std::string startupMode{ "debug" };
            std::string startupSceneId{ "Sandbox" };
        };

        StartupConfig LoadStartupConfig(const char* path) {
            StartupConfig cfg{};
            std::ifstream ifs(path);
            if (!ifs.is_open()) {
                return cfg;
            }

            nlohmann::json root = nlohmann::json::parse(ifs, nullptr, false);
            if (root.is_discarded() || !root.is_object()) {
                return cfg;
            }

            cfg.startupMode = root.value("startupMode", cfg.startupMode);
            cfg.startupSceneId = root.value("startupSceneId", cfg.startupSceneId);
            return cfg;
        }
    }

    EngineApp::EngineApp()
        : sceneFactory_(sceneCatalog_),
        sceneTransitionBus_(sceneManager_, sceneCatalog_, sceneFactory_) {
    }

    bool EngineApp::Initialize() {
        sceneCatalog_.Register(SceneCatalogEntry{ "Sandbox", "SandboxScene", "Data/scenes/scene_sandbox.json", true, "Sandbox" });
        sceneCatalog_.Register(SceneCatalogEntry{ "Title", "TitleScene", "Data/scenes/scene_title.json", true, "Title" });
        sceneCatalog_.Register(SceneCatalogEntry{ "Empty", "GameDocumentScene", "Data/scenes/scene_empty.json", true, "Empty" });

        const StartupConfig startup = LoadStartupConfig("Data/project.json");

        std::string targetSceneId = startup.startupSceneId;
        if (startup.startupMode == "debug") {
            targetSceneId = "Sandbox";
        }

        std::unique_ptr<IScene> initialScene = sceneFactory_.CreateScene(targetSceneId);
        if (!initialScene) {
            initialScene = sceneFactory_.CreateScene("Sandbox");
        }
        if (!initialScene) {
            return false;
        }

        RuntimeSceneContext::SetTransitionBus(&sceneTransitionBus_);
        sceneManager_.ChangeScene(std::move(initialScene));
        return true;
    }

    void EngineApp::Update(float dt) {
        sceneManager_.Update(dt);
        sceneTransitionBus_.Update();
    }

    void EngineApp::Render() {
        sceneManager_.Render();
    }

    void EngineApp::RenderImGui() {
#if defined(_DEBUG)
        if (!SERVICES::IsEditorUIEnabled()) {
            return;
        }
        sceneManager_.RenderImGui();
#endif
    }

    void EngineApp::Shutdown() {
        RuntimeSceneContext::SetTransitionBus(nullptr);
        sceneManager_.ChangeScene(nullptr);
        sceneManager_.Update(0.0f);
    }

} // namespace HIKARI
