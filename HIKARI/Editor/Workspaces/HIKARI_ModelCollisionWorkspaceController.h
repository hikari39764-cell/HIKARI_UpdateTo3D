#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>

#include "Assets/Collision/HIKARI_ModelCollisionGenerator.h"
#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"
#include "Assets/HIKARI_AssetGuid.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Editor/Views/HIKARI_EditorDirectorCameraController.h"
#include "Editor/Views/HIKARI_EditorViewInputRouter.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspace.h"
#include "Editor/Workspaces/HIKARI_ModelCollisionHistory.h"
#include "Editor/Workspaces/HIKARI_ModelCollisionPreviewScene.h"

namespace HIKARI {
    class AssetDatabase;
    class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

    class EditorWorkspaceHost;

    enum class ModelCollisionSelectionMode : uint8_t {
        CollisionShapes,
        SourceNodes,
    };

    struct ModelCollisionWorkspaceResult {
        bool exitToSceneRequested = false;
        std::string statusMessage{};
    };

    class ModelCollisionWorkspaceController {
    public:
        void ApplyWorkspaceActivation(
            DocumentSceneBase& scene,
            const EditorWorkspaceActivation& activation,
            EditorWorkspaceHost& workspaceHost);

        void DrawDockSpace(bool resetDefaultDockLayout) const;
        ModelCollisionWorkspaceResult Draw(
            DocumentSceneBase& scene,
            EditorWorkspaceHost& workspaceHost);

        bool IsEditingModel() const noexcept;
        bool IsDocumentDirty() const noexcept;
        bool CanUndo() const noexcept;
        bool CanRedo() const noexcept;
        bool SaveDocument(DocumentSceneBase& scene, std::string& outMessage);
        bool Undo(std::string& outMessage);
        bool Redo(std::string& outMessage);

    private:
        bool OpenModel(
            AssetDatabase& assetDatabase,
            const AssetGuid& guid,
            std::string& outMessage);
        void ClosePreviewRequest(EditorWorkspaceHost& workspaceHost) const;
        void FitPreviewCamera();
        void FocusPreviewBounds(const Bounds& bounds);
        void FocusSelection();
        void CommitEdit(std::string label);
        void NormalizeShapeSelection() noexcept;
        void SelectFirstShape() noexcept;
        void SelectShape(uint64_t shapeId, bool additive) noexcept;
        void SelectSourceNode(int32_t nodeIndex, bool additive) noexcept;
        bool IsShapeSelected(uint64_t shapeId) const noexcept;
        bool IsShapeHidden(uint64_t shapeId) const noexcept;
        bool IsShapeLocked(uint64_t shapeId) const noexcept;
        bool AreAllSelectedShapesHidden() const noexcept;
        bool AreAllSelectedShapesLocked() const noexcept;

        ASSETS::COLLISION::ModelCollisionShape* SelectedShape() noexcept;
        const ASSETS::COLLISION::ModelCollisionShape*
            SelectedShape() const noexcept;
        void AddShape(ASSETS::COLLISION::CollisionGeometryShapeType type);
        void DuplicateSelectedShapes();
        void DeleteSelectedShapes();
        void SetSelectedShapesEnabled(bool enabled);
        void SetSelectedShapesHidden(bool hidden) noexcept;
        void SetSelectedShapesLocked(bool locked) noexcept;
        void GenerateShapes();

        void DrawPreviewWindow(
            DocumentSceneBase& scene,
            EditorWorkspaceHost& workspaceHost,
            ModelCollisionWorkspaceResult& result);
        void DrawShapeListWindow();
        void DrawShapeDetailsWindow();
        void DrawSourceModelWindow();
        void DrawAutoGenerateWindow();
        void DrawPendingModelOpenModal(
            DocumentSceneBase& scene,
            ModelCollisionWorkspaceResult& result);
        void DrawPendingCloseModal(
            DocumentSceneBase& scene,
            ModelCollisionWorkspaceResult& result);

        AssetGuid modelGuid_{};
        AssetGuid pendingModelGuid_{};
        std::string modelDisplayName_{};
        std::filesystem::path setupPath_{};
        ASSETS::COLLISION::ModelCollisionSetup setup_{};
        ModelCollisionHistory history_{};
        ModelCollisionPreviewScene previewScene_{};

        EditorDirectorCameraController cameraController_{};
        EditorViewInputRouter inputRouter_{};
        EditorTransformGizmo transformGizmo_{};
        EditorTransformGizmoState gizmoState_{};
        uint64_t cameraRevision_ = 1u;
        uint64_t selectedShapeId_ = 0u;
        uint64_t hoveredShapeId_ = 0u;
        int32_t primarySourceNodeIndex_ = -1;
        int32_t hoveredSourceNodeIndex_ = -1;
        int32_t sourceNodeScrollRequest_ = -1;
        ModelCollisionSelectionMode selectionMode_ =
            ModelCollisionSelectionMode::CollisionShapes;
        std::unordered_set<uint64_t> selectedShapeIds_{};
        std::unordered_set<uint64_t> hiddenShapeIds_{};
        std::unordered_set<uint64_t> lockedShapeIds_{};
        bool cameraCutPending_ = true;
        bool showModel_ = true;
        bool showCollision_ = true;
        bool showGeneratedOnly_ = false;
        bool gizmoEditActive_ = false;
        bool gizmoEditChanged_ = false;
        bool detailsEditPending_ = false;
        bool closeRequested_ = false;

        ASSETS::COLLISION::ModelCollisionGenerationTarget generationTarget_ =
            ASSETS::COLLISION::ModelCollisionGenerationTarget::WholeModel;
        ASSETS::COLLISION::CollisionGeometryShapeType generationShapeType_ =
            ASSETS::COLLISION::CollisionGeometryShapeType::Box;
        bool replaceGeneratedShapes_ = true;
        int generationBudget_ = 512;
        std::unordered_set<int32_t> selectedSourceNodes_{};
        std::array<char, 128> sourceSearch_{};
        std::string statusMessage_{};
    };

} // namespace HIKARI::EDITOR
