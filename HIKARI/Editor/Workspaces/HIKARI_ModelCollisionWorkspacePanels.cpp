#include "Editor/Workspaces/HIKARI_ModelCollisionWorkspaceController.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string_view>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        const char* ShapeTypeName(
            ASSETS::COLLISION::CollisionGeometryShapeType type) noexcept {

            switch (type) {
            case ASSETS::COLLISION::CollisionGeometryShapeType::Sphere:
                return "Sphere";
            case ASSETS::COLLISION::CollisionGeometryShapeType::Capsule:
                return "Capsule";
            case ASSETS::COLLISION::CollisionGeometryShapeType::ConvexHull:
                return "Convex Hull";
            case ASSETS::COLLISION::CollisionGeometryShapeType::TriangleMesh:
                return "Static Mesh";
            case ASSETS::COLLISION::CollisionGeometryShapeType::Box:
            default:
                return "Box";
            }
        }

        bool ContainsCaseInsensitive(
            std::string_view text,
            std::string_view query) {

            if (query.empty()) {
                return true;
            }
            const std::string lowerText = TEXT::ToLowerAsciiCopy(text);
            const std::string lowerQuery = TEXT::ToLowerAsciiCopy(query);
            return lowerText.find(lowerQuery) != std::string::npos;
        }
#endif
    }

    void ModelCollisionWorkspaceController::DrawShapeListWindow() {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Collision Shapes###ModelCollision/Shapes")) {
            ImGui::End();
            return;
        }
        ImGui::Text("%d shapes", static_cast<int>(setup_.shapes.size()));
        if (!selectedShapeIds_.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled(
                "| %d selected",
                static_cast<int>(selectedShapeIds_.size()));
        }
        if (!hiddenShapeIds_.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled(
                "| %d hidden",
                static_cast<int>(hiddenShapeIds_.size()));
        }
        ImGui::SameLine();
        ImGui::Checkbox("Generated only", &showGeneratedOnly_);
        if (ImGui::Button("+ Shape")) {
            ImGui::OpenPopup("AddCollisionShape");
        }
        if (ImGui::BeginPopup("AddCollisionShape")) {
            if (ImGui::MenuItem("Box")) {
                AddShape(ASSETS::COLLISION::CollisionGeometryShapeType::Box);
            }
            if (ImGui::MenuItem("Sphere")) {
                AddShape(ASSETS::COLLISION::CollisionGeometryShapeType::Sphere);
            }
            if (ImGui::MenuItem("Capsule")) {
                AddShape(ASSETS::COLLISION::CollisionGeometryShapeType::Capsule);
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        const bool hasSelection = !selectedShapeIds_.empty();
        if (ImGui::Button("Selection...")) {
            ImGui::OpenPopup("CollisionSelectionActions");
        }
        if (ImGui::BeginPopup("CollisionSelectionActions")) {
            if (ImGui::MenuItem("Select Visible")) {
                selectedShapeIds_.clear();
                selectedShapeId_ = 0u;
                for (const ASSETS::COLLISION::ModelCollisionShape& shape :
                        setup_.shapes) {
                    if ((showGeneratedOnly_ && !shape.generated) ||
                        !ShouldDrawShape(shape.id)) {
                        continue;
                    }
                    selectedShapeIds_.insert(shape.id);
                    if (selectedShapeId_ == 0u) {
                        selectedShapeId_ = shape.id;
                    }
                }
                selectionMode_ =
                    ModelCollisionSelectionMode::CollisionShapes;
            }
            if (ImGui::MenuItem("Clear Selection", nullptr, false, hasSelection)) {
                selectedShapeIds_.clear();
                selectedShapeId_ = 0u;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection)) {
                DuplicateSelectedShapes();
            }
            if (ImGui::MenuItem("Hide Selected", "H", false, hasSelection)) {
                SetSelectedShapesHidden(true);
            }
            if (ImGui::MenuItem("Hide Unselected", nullptr, false, hasSelection)) {
                HideUnselectedShapes();
            }
            if (ImGui::MenuItem("Show All", "Shift+H")) {
                ShowAllShapes();
            }
            if (ImGui::MenuItem(
                    AreAllSelectedShapesLocked() ? "Unlock" : "Lock",
                    "L",
                    false,
                    hasSelection)) {
                SetSelectedShapesLocked(!AreAllSelectedShapesLocked());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Delete", false, hasSelection)) {
                DeleteSelectedShapes();
            }
            ImGui::EndPopup();
        }
        ImGui::Separator();

        if (ImGui::BeginChild("##CollisionShapeList")) {
            for (ASSETS::COLLISION::ModelCollisionShape& shape :
                    setup_.shapes) {
                if (showGeneratedOnly_ && !shape.generated) {
                    continue;
                }
                ImGui::PushID(static_cast<int>(shape.id));
                const bool hidden = IsShapeHidden(shape.id);
                const bool locked = IsShapeLocked(shape.id);
                if (ImGui::SmallButton(hidden ? "-" : "o")) {
                    if (hidden) {
                        hiddenShapeIds_.erase(shape.id);
                    } else {
                        hiddenShapeIds_.insert(shape.id);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        hidden ? "Show collision shape" : "Hide collision shape");
                }
                ImGui::SameLine();
                bool enabled = shape.enabled;
                if (ImGui::Checkbox("##Enabled", &enabled)) {
                    shape.enabled = enabled;
                    CommitEdit(enabled
                        ? "Enable Collision Shape"
                        : "Disable Collision Shape");
                }
                ImGui::SameLine();
                const std::string label = shape.name + "  [" +
                    ShapeTypeName(shape.type) + "]" +
                    (shape.generated ? "  Auto" : "") +
                    (hidden ? "  Hidden" : "") +
                    (locked ? "  Locked" : "");
                if (hidden) {
                    ImGui::PushStyleColor(
                        ImGuiCol_Text,
                        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                }
                if (ImGui::Selectable(
                        label.c_str(),
                        IsShapeSelected(shape.id),
                        ImGuiSelectableFlags_AllowDoubleClick)) {
                    const ImGuiIO& io = ImGui::GetIO();
                    selectionMode_ =
                        ModelCollisionSelectionMode::CollisionShapes;
                    SelectShape(shape.id, io.KeyCtrl || io.KeyShift);
                    if (ImGui::IsMouseDoubleClicked(
                            ImGuiMouseButton_Left)) {
                        FocusSelection();
                    }
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right) &&
                    !IsShapeSelected(shape.id)) {
                    SelectShape(shape.id, false);
                }
                bool shapeListChanged = false;
                if (ImGui::BeginPopupContextItem("ShapeContext")) {
                    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                        DuplicateSelectedShapes();
                        shapeListChanged = true;
                    }
                    if (ImGui::MenuItem(hidden ? "Show" : "Hide", "H")) {
                        SetSelectedShapesHidden(!hidden);
                    }
                    if (ImGui::MenuItem(locked ? "Unlock" : "Lock", "L")) {
                        SetSelectedShapesLocked(!locked);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete", "Delete")) {
                        DeleteSelectedShapes();
                        shapeListChanged = true;
                    }
                    ImGui::EndPopup();
                }
                if (hidden) {
                    ImGui::PopStyleColor();
                }
                ImGui::PopID();
                if (shapeListChanged) {
                    break;
                }
            }
        }
        ImGui::EndChild();
        ImGui::End();
#endif
    }

    void ModelCollisionWorkspaceController::DrawShapeDetailsWindow() {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Shape Details###ModelCollision/Details")) {
            ImGui::End();
            return;
        }
        ASSETS::COLLISION::ModelCollisionShape* shape = SelectedShape();
        if (shape == nullptr) {
            ImGui::TextDisabled("Select or add a collision shape.");
            ImGui::End();
            return;
        }

        if (selectedShapeIds_.size() > 1u) {
            ImGui::TextDisabled(
                "%d shapes selected; editing active shape",
                static_cast<int>(selectedShapeIds_.size()));
        }
        const bool locked = IsShapeLocked(shape->id);
        if (locked) {
            ImGui::TextColored(
                ImVec4(0.58f, 0.78f, 1.0f, 1.0f),
                "Active shape is locked");
        }

        ImGui::BeginDisabled(locked);
        char nameBuffer[160]{};
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", shape->name.c_str());
        if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
            shape->name = nameBuffer;
            detailsEditPending_ = true;
        }
        const bool geometryShape = shape->type == ASSETS::COLLISION::
                CollisionGeometryShapeType::ConvexHull ||
            shape->type == ASSETS::COLLISION::
                CollisionGeometryShapeType::TriangleMesh;
        if (geometryShape) {
            ImGui::Text("Type: %s", ShapeTypeName(shape->type));
            ImGui::TextDisabled(
                "%d vertices | %d triangles",
                static_cast<int>(shape->vertices.size()),
                static_cast<int>(shape->indices.size() / 3u));
        } else {
            int type = static_cast<int>(shape->type);
            const char* types[]{ "Box", "Sphere", "Capsule" };
            if (ImGui::Combo("Type", &type, types, 3)) {
                shape->type = static_cast<
                    ASSETS::COLLISION::CollisionGeometryShapeType>(type);
                shape->radius = (std::max)(shape->radius, 0.001f);
                shape->height = (std::max)(
                    shape->height,
                    shape->radius * 2.0f);
                detailsEditPending_ = true;
            }
        }
        if (ImGui::DragFloat3(
                "Center", &shape->center.x, 0.02f, 0.0f, 0.0f, "%.3f")) {
            detailsEditPending_ = true;
        }
        if (ImGui::DragFloat3(
                "Rotation", &shape->rotationEulerDegrees.x,
                0.25f, 0.0f, 0.0f, "%.2f deg")) {
            detailsEditPending_ = true;
        }
        if (shape->type ==
                ASSETS::COLLISION::CollisionGeometryShapeType::Box) {
            if (ImGui::DragFloat3(
                    "Size", &shape->size.x, 0.02f, 0.001f,
                    1000000.0f, "%.3f")) {
                shape->size.x = (std::max)(shape->size.x, 0.001f);
                shape->size.y = (std::max)(shape->size.y, 0.001f);
                shape->size.z = (std::max)(shape->size.z, 0.001f);
                detailsEditPending_ = true;
            }
        } else if (!geometryShape) {
            if (ImGui::DragFloat(
                    "Radius", &shape->radius, 0.01f, 0.001f,
                    1000000.0f, "%.3f")) {
                shape->radius = (std::max)(shape->radius, 0.001f);
                shape->height = (std::max)(
                    shape->height,
                    shape->radius * 2.0f);
                detailsEditPending_ = true;
            }
            if (shape->type ==
                    ASSETS::COLLISION::CollisionGeometryShapeType::Capsule &&
                ImGui::DragFloat(
                    "Total Height", &shape->height, 0.02f,
                    shape->radius * 2.0f, 1000000.0f, "%.3f")) {
                shape->height = (std::max)(
                    shape->height,
                    shape->radius * 2.0f);
                detailsEditPending_ = true;
            }
        }
        if (detailsEditPending_) {
            shape->generated = false;
            shape->sourceNodeIndices.clear();
            shape->generationMethod.clear();
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        if (shape->generated) {
            ImGui::TextDisabled(
                "Generated from %d source part(s) | %s",
                static_cast<int>(shape->sourceNodeIndices.size()),
                shape->generationMethod.empty()
                    ? "Auto"
                    : shape->generationMethod.c_str());
        } else {
            ImGui::TextDisabled("Manual collision shape");
        }
        if (ImGui::Button("Shape Actions...")) {
            ImGui::OpenPopup("ShapeDetailsActions");
        }
        if (ImGui::BeginPopup("ShapeDetailsActions")) {
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                DuplicateSelectedShapes();
            }
            if (ImGui::MenuItem(shape->enabled ? "Disable" : "Enable")) {
                SetSelectedShapesEnabled(!shape->enabled);
            }
            const bool allHidden = AreAllSelectedShapesHidden();
            if (ImGui::MenuItem(allHidden ? "Show" : "Hide", "H")) {
                SetSelectedShapesHidden(!allHidden);
            }
            const bool allLocked = AreAllSelectedShapesLocked();
            if (ImGui::MenuItem(allLocked ? "Unlock" : "Lock", "L")) {
                SetSelectedShapesLocked(!allLocked);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Delete")) {
                DeleteSelectedShapes();
            }
            ImGui::EndPopup();
        }

        if (detailsEditPending_ && !ImGui::IsAnyItemActive()) {
            CommitEdit("Edit Collision Shape");
            detailsEditPending_ = false;
        }
        ImGui::End();
#endif
    }

    void ModelCollisionWorkspaceController::DrawSourceModelWindow() {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Source Model###ModelCollision/Source")) {
            ImGui::End();
            return;
        }
        const ModelAsset* model = previewScene_.GetModel();
        if (model == nullptr) {
            ImGui::TextDisabled("No model loaded.");
            ImGui::End();
            return;
        }
        const std::vector<ModelCollisionPreviewNode>& sourceNodes =
            previewScene_.GetSourceNodes();
        ImGui::Text(
            "%d mesh nodes | %d selected",
            static_cast<int>(sourceNodes.size()),
            static_cast<int>(selectedSourceNodes_.size()));
        ImGui::TextDisabled(
            "%d meshes",
            static_cast<int>(model->meshes.size()));
        ImGui::InputTextWithHint(
            "##NodeSearch", "Filter nodes", sourceSearch_.data(),
            sourceSearch_.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Select Filtered")) {
            const std::string_view query(sourceSearch_.data());
            for (const ModelCollisionPreviewNode& node : sourceNodes) {
                if (ContainsCaseInsensitive(node.name, query)) {
                    selectedSourceNodes_.insert(node.nodeIndex);
                    primarySourceNodeIndex_ = node.nodeIndex;
                }
            }
            selectionMode_ = ModelCollisionSelectionMode::SourceNodes;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear Selection")) {
            selectedSourceNodes_.clear();
            primarySourceNodeIndex_ = -1;
        }
        if (!selectedSourceNodes_.empty() && ImGui::Button("Frame Selected")) {
            selectionMode_ = ModelCollisionSelectionMode::SourceNodes;
            FocusSelection();
        }
        ImGui::Separator();
        if (ImGui::BeginChild("##SourceNodeList")) {
            const std::string_view query(sourceSearch_.data());
            for (const ModelCollisionPreviewNode& node : sourceNodes) {
                if (!ContainsCaseInsensitive(node.name, query)) {
                    continue;
                }
                ImGui::PushID(node.nodeIndex);
                const std::string label = node.name + "  [mesh " +
                    std::to_string(node.meshIndex) + "]";
                if (ImGui::Selectable(
                        label.c_str(),
                        selectedSourceNodes_.contains(node.nodeIndex),
                        ImGuiSelectableFlags_AllowDoubleClick)) {
                    const ImGuiIO& io = ImGui::GetIO();
                    selectionMode_ = ModelCollisionSelectionMode::SourceNodes;
                    SelectSourceNode(
                        node.nodeIndex,
                        io.KeyCtrl || io.KeyShift);
                    if (ImGui::IsMouseDoubleClicked(
                            ImGuiMouseButton_Left)) {
                        FocusSelection();
                    }
                }
                if (sourceNodeScrollRequest_ == node.nodeIndex) {
                    ImGui::SetScrollHereY(0.5f);
                    sourceNodeScrollRequest_ = -1;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::End();
#endif
    }

    void ModelCollisionWorkspaceController::DrawAutoGenerateWindow() {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Auto Generate###ModelCollision/Generate")) {
            ImGui::End();
            return;
        }
        PollGenerationTask();
        if (generationPending_) {
            const std::optional<AssetTaskSnapshot> task =
                assetTaskService_ != nullptr
                    ? assetTaskService_->FindSnapshot(
                        generationTaskId_)
                    : std::nullopt;
            ImGui::TextColored(
                ImVec4(0.40f, 0.82f, 0.92f, 1.0f),
                "%s",
                task
                    ? task->progress.stage.c_str()
                    : "Generating collision...");
            if (task && task->progress.determinate) {
                ImGui::ProgressBar(
                    task->progress.normalized,
                    ImVec2(-1.0f, 22.0f));
                ImGui::TextDisabled(
                    "%llu / %llu source groups  |  %.2f s",
                    static_cast<unsigned long long>(
                        task->progress.completedUnits),
                    static_cast<unsigned long long>(
                        task->progress.totalUnits),
                    task->elapsedSeconds);
            } else {
                ImGui::ProgressBar(
                    0.0f,
                    ImVec2(-1.0f, 22.0f),
                    "Working...");
                if (task) {
                    ImGui::TextDisabled(
                        "%.2f s elapsed",
                        task->elapsedSeconds);
                }
            }
            ImGui::TextDisabled(
                "The editor remains usable. CoACD cancellation takes effect after its current native solve returns.");
            const bool cancelRequested =
                task && task->cancellationRequested;
            ImGui::BeginDisabled(cancelRequested);
            if (ImGui::Button(
                    cancelRequested
                        ? "Cancel Requested"
                        : "Cancel Generation")) {
                if (assetTaskService_ != nullptr) {
                    (void)assetTaskService_->RequestCancel(
                        generationTaskId_);
                }
                statusMessage_ =
                    "cancel requested; the current geometry batch will finish safely";
            }
            ImGui::EndDisabled();
            if (!statusMessage_.empty()) {
                ImGui::TextWrapped("%s", statusMessage_.c_str());
            }
            ImGui::End();
            return;
        }
        if (generationDraft_.has_value()) {
            const auto& result = generationDraft_->result;
            ImGui::TextColored(
                ImVec4(0.42f, 0.90f, 0.58f, 1.0f),
                "Generation Draft Ready");
            ImGui::Text(
                "%u generated | %u source groups | %u replaced",
                result.generatedCount,
                result.candidateCount,
                result.removedGeneratedCount);
            if (result.truncated) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.68f, 0.28f, 1.0f),
                    "The draft reached the configured shape budget.");
            }
            ImGui::TextWrapped(
                "The viewport is previewing this draft. The current document has not changed. Apply records one undoable edit; Discard leaves it untouched.");
            if (ImGui::Button("Apply Draft", ImVec2(135.0f, 32.0f))) {
                ApplyGenerationDraft();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard Draft", ImVec2(135.0f, 32.0f))) {
                DiscardGenerationDraft();
            }
            ImGui::End();
            return;
        }
        const char* targets[]{
            "Whole Model",
            "Selected Parts Combined",
            "Nearby Selected Groups",
            "Each Selected Part"
        };
        int target = static_cast<int>(generationTarget_);
        if (ImGui::Combo("Target", &target, targets, 4)) {
            generationTarget_ = static_cast<
                ASSETS::COLLISION::ModelCollisionGenerationTarget>(target);
        }
        const char* methods[]{
            "Fitted Box",
            "Fitted Sphere",
            "Fitted Capsule",
            "Single Convex Hull",
            "Convex Decomposition (CoACD)",
            "Static Triangle Mesh"
        };
        int method = static_cast<int>(generationMethod_);
        if (ImGui::Combo("Method", &method, methods, 6)) {
            generationMethod_ = static_cast<
                ASSETS::COLLISION::ModelCollisionGenerationMethod>(method);
        }
        ImGui::Checkbox(
            "Replace previously generated shapes",
            &replaceGeneratedShapes_);
        const bool requiresSourceSelection = generationTarget_ !=
            ASSETS::COLLISION::ModelCollisionGenerationTarget::WholeModel;
        if (generationTarget_ == ASSETS::COLLISION::
                ModelCollisionGenerationTarget::SelectedNodesSpatialGroups) {
            ImGui::DragFloat(
                "Merge Distance",
                &generationMergeDistance_,
                0.01f,
                0.0f,
                100000.0f,
                "%.3f");
            generationMergeDistance_ = (std::max)(
                generationMergeDistance_,
                0.0f);
        }
        const bool convexHull = generationMethod_ == ASSETS::COLLISION::
            ModelCollisionGenerationMethod::ConvexHull;
        const bool decomposition = generationMethod_ == ASSETS::COLLISION::
            ModelCollisionGenerationMethod::ConvexDecomposition;
        const bool triangleMesh = generationMethod_ == ASSETS::COLLISION::
            ModelCollisionGenerationMethod::TriangleMesh;
        const bool primitiveMethod = generationMethod_ == ASSETS::COLLISION::
                ModelCollisionGenerationMethod::Box ||
            generationMethod_ == ASSETS::COLLISION::
                ModelCollisionGenerationMethod::Sphere ||
            generationMethod_ == ASSETS::COLLISION::
                ModelCollisionGenerationMethod::Capsule;
        if (primitiveMethod && generationTarget_ == ASSETS::COLLISION::
                ModelCollisionGenerationTarget::SelectedNodesSpatialGroups) {
            ImGui::DragFloat(
                "Max Merge Inflation",
                &generationAccuracy_,
                0.01f,
                0.0f,
                4.0f,
                "%.2f x");
            generationAccuracy_ = (std::clamp)(
                generationAccuracy_, 0.0f, 4.0f);
        }
        if (convexHull || decomposition) {
            ImGui::InputInt(
                "Max Hull Vertices",
                &generationHullVertexBudget_);
            generationHullVertexBudget_ = (std::clamp)(
                generationHullVertexBudget_, 16, 256);
        }
        if (decomposition) {
            ImGui::DragFloat(
                "Concavity Threshold",
                &generationAccuracy_,
                0.0025f,
                0.001f,
                1.0f,
                "%.4f");
            generationAccuracy_ = (std::clamp)(
                generationAccuracy_, 0.001f, 1.0f);
            ImGui::Checkbox("Preserve Separate Gaps", &generationPreserveGaps_);
        }
        if (triangleMesh) {
            ImGui::TextDisabled("Static bodies only");
            ImGui::InputInt(
                "Triangle Budget",
                &generationTriangleBudget_);
            generationTriangleBudget_ = (std::clamp)(
                generationTriangleBudget_, 1, 16000000);
        } else if (decomposition || generationTarget_ == ASSETS::COLLISION::
                ModelCollisionGenerationTarget::SelectedNodesIndividually ||
            generationTarget_ == ASSETS::COLLISION::
                ModelCollisionGenerationTarget::SelectedNodesSpatialGroups) {
            ImGui::InputInt("Shape Budget", &generationBudget_);
            generationBudget_ = (std::clamp)(generationBudget_, 1, 4096);
        }
        if (requiresSourceSelection) {
            const ImVec4 color = selectedSourceNodes_.empty()
                ? ImVec4(1.0f, 0.62f, 0.30f, 1.0f)
                : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::TextColored(
                color,
                "%d source part(s) selected",
                static_cast<int>(selectedSourceNodes_.size()));
        }
        ImGui::Separator();
        ImGui::BeginDisabled(
            !previewScene_.IsReady() ||
            (requiresSourceSelection && selectedSourceNodes_.empty()));
        if (ImGui::Button("Generate Collision", ImVec2(-1.0f, 34.0f))) {
            GenerateShapes();
        }
        ImGui::EndDisabled();
        if (!statusMessage_.empty()) {
            ImGui::TextWrapped("%s", statusMessage_.c_str());
        }
        ImGui::End();
#endif
    }

    void ModelCollisionWorkspaceController::DrawPendingModelOpenModal(
        DocumentSceneBase& scene,
        ModelCollisionWorkspaceResult& result) {
#if defined(HIKARI_WITH_EDITOR)
        (void)result;
        AssetDatabase& assetDatabase = scene.GetAssetDatabase();
        if (!pendingModelGuid_.IsValid()) {
            return;
        }
        ImGui::OpenPopup("Unsaved Model Collision Changes");
        if (!ImGui::BeginPopupModal(
                "Unsaved Model Collision Changes",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::TextWrapped(
            "The current model collision setup has unsaved changes.");
        if (ImGui::Button("Save and Open", ImVec2(130.0f, 0.0f))) {
            std::string message{};
            if (SaveDocument(scene, message)) {
                const AssetGuid next = pendingModelGuid_;
                pendingModelGuid_ = {};
                (void)OpenModel(assetDatabase, next, statusMessage_);
                ImGui::CloseCurrentPopup();
            } else {
                statusMessage_ = std::move(message);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard and Open", ImVec2(140.0f, 0.0f))) {
            const AssetGuid next = pendingModelGuid_;
            pendingModelGuid_ = {};
            (void)OpenModel(assetDatabase, next, statusMessage_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
            pendingModelGuid_ = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
#else
        (void)scene;
        (void)result;
#endif
    }

    void ModelCollisionWorkspaceController::DrawPendingCloseModal(
        DocumentSceneBase& scene,
        ModelCollisionWorkspaceResult& result) {
#if defined(HIKARI_WITH_EDITOR)
        if (!closeRequested_) {
            return;
        }
        ImGui::OpenPopup("Save Model Collision Changes?");
        if (!ImGui::BeginPopupModal(
                "Save Model Collision Changes?",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::TextWrapped(
            "The current model collision setup has unsaved changes.");
        if (ImGui::Button("Save and Exit", ImVec2(125.0f, 0.0f))) {
            if (SaveDocument(scene, statusMessage_)) {
                closeRequested_ = false;
                result.exitToSceneRequested = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard and Exit", ImVec2(135.0f, 0.0f))) {
            closeRequested_ = false;
            result.exitToSceneRequested = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
            closeRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
#else
        (void)scene;
        (void)result;
#endif
    }


} // namespace HIKARI::EDITOR
