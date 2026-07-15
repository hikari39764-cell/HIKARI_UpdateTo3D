#pragma once

#include <optional>

#include "HIKARI_ComponentDocumentEditor.h"
#include "HIKARI_DocumentComponentAuthoringService.h"
#include "Editor/Inspectors/HIKARI_ImGuiInspectorBuilder.h"
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

    private:
        ComponentDocumentEditor componentDocumentEditor_{};
        DocumentComponentAuthoringService componentAuthoringService_{};
        ImGuiInspectorBuilder componentInspectorBuilder_{};
        PrefabRegistry prefabRegistry_{};
        PrefabSerializer prefabSerializer_{};
        std::optional<SceneObjectId> openCinematicsWorkspaceCameraRequest_{};
        bool deferredComponentRebuild_ = false;
    };

} // namespace HIKARI
