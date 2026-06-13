#pragma once

#include <cstdint>
#include <string>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Debug/HIKARI_RenderDebugView.h"
#include "Runtime/HIKARI_RuntimeResourceRefreshService.h"

namespace HIKARI {

    class GameObject;
    class ModelAsset;

    struct EditorSelection {
        GameObject* selectedObject = nullptr;
        ModelAsset* selectedAsset = nullptr;
        std::string selectedAssetGuid{};
        std::string selectedAssetPath{};
    };

    struct ComponentGizmoState {
        bool showComponentGizmos = true;
        bool showTriggerVolumes = false;
        bool showSpawnPoints = false;
        bool showDoorTransitions = false;
        bool showPlayerBounds = true;
        bool showUIScreenRects = false;
        bool showOnlySelectedObject = false;
    };

    enum class EditorTransformGizmoOperation {
        Translate,
        Rotate,
        Scale,
    };

    enum class EditorTransformGizmoMode {
        World,
        Local,
    };

    struct EditorTransformGizmoState {
        bool enabled = true;
        EditorTransformGizmoOperation operation = EditorTransformGizmoOperation::Translate;
        EditorTransformGizmoMode mode = EditorTransformGizmoMode::World;
        bool snapEnabled = false;
        MATH::Vec3 translateSnap{ 0.5f, 0.5f, 0.5f };
        float rotateSnapDeg = 15.0f;
        float scaleSnap = 0.1f;
    };

    enum class LightProbeVolumeOverlayMode {
        Off,
        BoundsOnly,
        SampledPoints,
        AllPoints,
    };

    enum class ReflectionProbeEditTarget {
        ProbePosition,
        InfluenceBox,
        ProjectionBox,
    };

    struct ViewportOverlayState {
        bool showGrid = true;
        bool showAxis = true;
        bool showLights = true;
        bool showReflectionProbe = true;
        bool showLightProbeVolume = false;
        bool showProbeLabels = true;
        bool showXRayGizmos = false;
        bool editReflectionProbe = false;
        LightProbeVolumeOverlayMode lightProbeVolumeMode = LightProbeVolumeOverlayMode::BoundsOnly;
        ReflectionProbeEditTarget reflectionProbeEditTarget = ReflectionProbeEditTarget::ProbePosition;
    };

    struct ViewportPerformanceState {
        bool disableSsaoInEditorViewport = false;
        bool disableSsaoWhileGizmoActive = true;
    };

    struct ViewportDebugViewState {
        RenderDebugView renderView = RenderDebugView::None;
        bool showLegend = true;
    };

    struct ViewportWindowState {
        bool showGameView = true;
        bool showViewportHud = true;
        bool gameOnlyMode = false;
        float gameViewResolutionScale = 1.0f;
    };

    struct AuthoringWindowState {
        bool showSceneWorkspace = true;
    };

    struct ResourceWindowState {
        bool showAssetBrowser = true;
        bool showEnvironment = true;
        bool showLightingBake = false;
    };

    struct RuntimeDebugWindowState {
        bool showDebugWorkspace = true;
        bool showDebugView = true;
        bool showPerformanceAudit = true;
        bool showValidationLab = true;
    };

    struct DebugWindowState {
        ViewportWindowState viewport{};
        AuthoringWindowState authoring{};
        ResourceWindowState resources{};
        RuntimeDebugWindowState runtime{};
    };

    struct EditorContext {
        DebugWindowState windows{};
        ComponentGizmoState gizmos{};
        EditorTransformGizmoState transformGizmo{};
        ViewportOverlayState overlays{};
        ViewportPerformanceState viewportPerformance{};
        ViewportDebugViewState viewportDebug{};
        EditorSelection selection{};

        std::string sceneNameEditBuffer{ "Untitled" };
        std::string saveAsNameBuffer{ "Untitled" };
        bool saveAsNameOverriddenByUser = false;
        std::string prefabNameBuffer{ "NewPrefab" };
        std::string componentAddStatusMessage{};
        bool componentAddStatusIsError = false;
        uint64_t nextSceneObjectId = 1;
        bool sceneDirty = false;
        RuntimeResourceRefreshReport lastRuntimeRefreshReport{};
    };

} // namespace HIKARI
