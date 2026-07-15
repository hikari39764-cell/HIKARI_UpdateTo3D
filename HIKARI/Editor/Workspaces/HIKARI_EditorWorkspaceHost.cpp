#include "HIKARI_EditorWorkspaceHost.h"

#include <utility>

namespace HIKARI::EDITOR {

    EditorWorkspaceHost::EditorWorkspaceHost() {
        cinematicsGameView_.renderViewId = RENDER3D::kPrimaryRenderViewId;
        cinematicsGameView_.role = EditorViewRole::Game;
        cinematicsGameView_.purpose = RENDER3D::RenderViewPurpose::Game;
        cinematicsGameView_.cameraBinding.kind = EditorViewCameraSourceKind::SceneDirector;
        cinematicsGameView_.execution = EditorViewExecutionMode::PrimaryFullQuality;

        cinematicsOverviewView_.renderViewId = kEditorOverviewRenderViewId;
        cinematicsOverviewView_.role = EditorViewRole::Overview;
        cinematicsOverviewView_.purpose = RENDER3D::RenderViewPurpose::EditorScene;
        cinematicsOverviewView_.cameraBinding.kind = EditorViewCameraSourceKind::OwnedEditorCamera;
        cinematicsOverviewView_.execution = EditorViewExecutionMode::LightweightEditorOverview;
    }

    bool EditorWorkspaceHost::RequestOpen(EditorWorkspaceOpenRequest request) {
        if (request.targetCameraObjectId &&
            request.targetCameraObjectId->value == 0) {
            request.targetCameraObjectId.reset();
        }
        pendingOpenRequest_ = std::move(request);
        return true;
    }

    std::optional<EditorWorkspaceActivation> EditorWorkspaceHost::ApplyPending() {
        if (!pendingOpenRequest_) {
            return std::nullopt;
        }

        EditorWorkspaceOpenRequest request = std::move(*pendingOpenRequest_);
        pendingOpenRequest_.reset();

        EditorWorkspaceActivation activation{};
        activation.previous = activeWorkspace_;
        activation.current = request.workspaceId;
        activation.targetCameraObjectId = request.targetCameraObjectId;
        activeWorkspace_ = request.workspaceId;

        return activation;
    }

    EditorWorkspaceId EditorWorkspaceHost::GetActive() const noexcept {
        return activeWorkspace_;
    }

    bool EditorWorkspaceHost::IsActive(EditorWorkspaceId workspaceId) const noexcept {
        return activeWorkspace_ == workspaceId;
    }

    void EditorWorkspaceHost::RequestResetActiveLayout() noexcept {
        if (activeWorkspace_ == EditorWorkspaceId::Cinematics) {
            resetCinematicsLayoutRequested_ = true;
        } else {
            resetSceneLayoutRequested_ = true;
        }
    }

    bool EditorWorkspaceHost::ConsumeReset(EditorWorkspaceId workspaceId) noexcept {
        bool* requested = workspaceId == EditorWorkspaceId::Cinematics
            ? &resetCinematicsLayoutRequested_
            : &resetSceneLayoutRequested_;
        const bool result = *requested;
        *requested = false;
        return result;
    }

    EditorViewInstance& EditorWorkspaceHost::GetCinematicsGameView() noexcept {
        return cinematicsGameView_;
    }

    const EditorViewInstance& EditorWorkspaceHost::GetCinematicsGameView() const noexcept {
        return cinematicsGameView_;
    }

    EditorViewInstance& EditorWorkspaceHost::GetCinematicsOverviewView() noexcept {
        return cinematicsOverviewView_;
    }

    const EditorViewInstance& EditorWorkspaceHost::GetCinematicsOverviewView() const noexcept {
        return cinematicsOverviewView_;
    }

} // namespace HIKARI::EDITOR
