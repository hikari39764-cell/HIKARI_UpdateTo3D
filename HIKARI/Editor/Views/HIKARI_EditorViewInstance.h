#pragma once

#include <cstdint>

#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

    enum class EditorViewRole : uint8_t {
        Game,
        Overview,
    };

    enum class EditorViewCameraSourceKind : uint8_t {
        SceneDirector,
        OwnedEditorCamera,
        SceneCameraObject,
    };

    enum class EditorViewExecutionMode : uint8_t {
        PrimaryFullQuality,
        LightweightEditorOverview,
    };

    struct EditorViewportExtent {
        uint32_t width = 0;
        uint32_t height = 0;

        bool IsValid() const noexcept {
            return width > 0 && height > 0;
        }

        float Aspect() const noexcept {
            return height > 0
                ? static_cast<float>(width) / static_cast<float>(height)
                : 1.0f;
        }
    };

    struct EditorViewInteractionState {
        bool visible = false;
        bool focused = false;
        bool hovered = false;
        bool mouseCaptured = false;
        bool keyboardActive = false;
        bool gizmoCaptured = false;
    };

    struct EditorViewCameraBinding {
        EditorViewCameraSourceKind kind = EditorViewCameraSourceKind::SceneDirector;
        SceneObjectId sceneObjectId{};
    };

    struct EditorViewInstance {
        RENDER3D::RenderViewId renderViewId{};
        EditorViewRole role = EditorViewRole::Game;
        RENDER3D::RenderViewPurpose purpose = RENDER3D::RenderViewPurpose::EditorScene;
        EditorViewCameraBinding cameraBinding{};
        EditorViewExecutionMode execution = EditorViewExecutionMode::PrimaryFullQuality;
        EditorViewportExtent extent{};
        EditorViewInteractionState interaction{};

        MATH::Vec2 overviewCenterXZ{};
        float overviewUnitsPerScreen = 40.0f;
    };

    inline constexpr RENDER3D::RenderViewId kEditorOverviewRenderViewId{ 2 };

} // namespace HIKARI::EDITOR
