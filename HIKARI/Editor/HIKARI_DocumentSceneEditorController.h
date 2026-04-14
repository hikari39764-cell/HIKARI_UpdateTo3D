#pragma once

#include <cstdint>
#include <string>

#include "HIKARI_AssetBrowserPanel.h"
#include "HIKARI_DebugCameraPanel.h"
#include "HIKARI_DebugMenuBar.h"
#include "HIKARI_EditorContext.h"
#include "HIKARI_EnvironmentPanel.h"
#include "HIKARI_HierarchyPanel.h"
#include "HIKARI_InspectorPanel.h"
#include "HIKARI_StatsPanel.h"

namespace HIKARI {

    class DocumentSceneBase;
    class GameObject;
    struct SceneObjectData;
    struct SceneObjectId;

    class DocumentSceneEditorController {
    public:
        void Draw(DocumentSceneBase& scene);

    private:
        DebugWindowState debugWindowState_{};
        DebugMenuBar debugMenuBar_{};
        HierarchyPanel hierarchyPanel_{};
        InspectorPanel inspectorPanel_{};
        AssetBrowserPanel assetBrowserPanel_{};
        StatsPanel statsPanel_{};
        EnvironmentPanel environmentPanel_{};
        DebugCameraPanel debugCameraPanel_{};

        EditorSelection selection_{};
        std::string sceneNameEditBuffer_{ "Untitled" };
        std::string saveAsNameBuffer_{ "Untitled" };
        uint64_t nextSceneObjectId_ = 1;
        bool sceneDirty_ = false;

        void MarkSceneDirty();
        bool IsSceneDirty() const;

        SceneObjectData* FindDocumentObjectById(DocumentSceneBase& scene, SceneObjectId id);
        SceneObjectData* FindDocumentObjectByRuntime(DocumentSceneBase& scene, GameObject* runtimeObject);
        GameObject* FindRuntimeObjectByDocumentId(DocumentSceneBase& scene, SceneObjectId id);
        void SyncSelectedObjectBackToDocument(DocumentSceneBase& scene);
        bool RebuildRuntimeWorldWithSelectionSync(DocumentSceneBase& scene);

        void SyncDocumentMeta(DocumentSceneBase& scene);
        void SyncNextSceneObjectId(DocumentSceneBase& scene);
        void DrawDocumentToolbar(DocumentSceneBase& scene);
    };

} // namespace HIKARI
