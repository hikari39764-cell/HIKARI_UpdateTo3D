#include "HIKARI_SceneFactory.h"

#include "HIKARI_SceneCatalog.h"
#include "Scenes/HIKARI_GameDocumentScene.h"
#include "Scenes/HIKARI_SandboxScene.h"
#include "Scenes/HIKARI_TitleScene.h"

namespace HIKARI {

    SceneFactory::SceneFactory(SceneCatalog& sceneCatalog)
        : sceneCatalog_(sceneCatalog) {
    }

    std::unique_ptr<IScene> SceneFactory::CreateScene(const std::string& sceneId) const {
        const SceneCatalogEntry* entry = sceneCatalog_.Find(sceneId);
        if (!entry) {
            return nullptr;
        }

        if (entry->sceneType == "SandboxScene") {
            return std::make_unique<SandboxScene>(sceneCatalog_, sceneId);
        }

        if (entry->sceneType == "GameDocumentScene") {
            return std::make_unique<GameDocumentScene>(sceneCatalog_, sceneId);
        }

        if (entry->sceneType == "TitleScene") {
            return std::make_unique<TitleScene>(sceneCatalog_, sceneId);
        }

        return nullptr;
    }

} // namespace HIKARI
