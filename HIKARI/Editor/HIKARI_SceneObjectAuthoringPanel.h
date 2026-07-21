#pragma once

#include <optional>
#include <string>
#include <vector>

#include "HIKARI_DocumentComponentAuthoringService.h"
#include "Editor/Authoring/HIKARI_CollisionAuthoringDialog.h"
#include "Editor/Authoring/HIKARI_CollisionAuthoringService.h"
#include "Editor/Authoring/HIKARI_PrimitiveCreationDialog.h"
#include "Editor/Authoring/HIKARI_SceneComponentAuthoringSection.h"
#include "Editor/Authoring/HIKARI_SceneObjectAuthoringTypes.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Prefab/HIKARI_PrefabRegistry.h"
#include "Scene/Prefab/HIKARI_PrefabSerializer.h"

namespace HIKARI {

    struct EditorContext;
    class DocumentSceneBase;
    class SelectionSyncService;

    class SceneObjectAuthoringPanel {
    public:
        void Draw(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync);
        void DrawContents(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync);
        std::optional<SceneObjectId> ConsumeOpenCinematicsWorkspaceCameraRequest();
        std::optional<SceneObjectAuthoringHistoryRequest>
            ConsumeHistoryRequest();

    private:
        DocumentComponentAuthoringService componentAuthoringService_{};
        SceneComponentAuthoringSection componentAuthoringSection_{};
        PrefabRegistry prefabRegistry_{};
        PrefabSerializer prefabSerializer_{};
        EDITOR::PrimitiveCreationDialog primitiveCreationDialog_{};
        EDITOR::CollisionAuthoringDialog collisionAuthoringDialog_{};
        EDITOR::CollisionAuthoringService collisionAuthoringService_{};
        std::optional<SceneObjectId> openCinematicsWorkspaceCameraRequest_{};
        std::optional<SceneObjectAuthoringHistoryRequest> historyRequest_{};
    };

} // namespace HIKARI
