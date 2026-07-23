#include "Editor/Controllers/HIKARI_DocumentSceneEditorController.h"

#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"
#include "Editor/Authoring/HIKARI_EditorObjectPlacement.h"
#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/History/HIKARI_SceneObjectsHistoryCommand.h"
#include "Editor/Selection/HIKARI_SceneSelectionTransform.h"
#include "Editor/History/HIKARI_SceneSystemsHistoryCommand.h"
#include "Editor/SystemAuthoring/HIKARI_BuiltInSystemAuthoring.h"
#include "Editor/Menus/HIKARI_EditorDocumentMenu.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Tools/HIKARI_BuiltInEditorTools.h"
#include "Editor/Viewport/HIKARI_ViewportAuthoringToolbar.h"
#include "Editor/Widgets/HIKARI_MaterialTextureSlotWidget.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Core/HIKARI_Logger.h"
#include "HIKARI_Services.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Lighting/HIKARI_SceneLightingRuntimeData.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Settings/HIKARI_RenderQualityProfileStore.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Runtime/HIKARI_RuntimeResourceRefreshService.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRenderer.h"
#include "Scene/Debug/HIKARI_BuiltInComponentGizmoProviders.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <utility>
#include <json.hpp>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI {

    namespace {
        std::string SummarizeRuntimeRefreshReport(const RuntimeResourceRefreshReport& report) {
            return "Runtime refresh: texture " + std::to_string(report.textureInvalidatedCount) +
                ", sky " + std::to_string(report.skyInvalidatedCount) +
                ", model " + std::to_string(report.modelReloadedCount) +
                ", rebound " + std::to_string(report.modelReboundComponentCount) +
                ", material " + std::to_string(report.materialReloadedCount) +
                ", mat rebound " + std::to_string(report.materialReboundComponentCount) +
                ", failed " + std::to_string(report.failedCount);
        }

#if defined(HIKARI_WITH_EDITOR)
        bool CouldMutateEditorDocumentThisFrame() {
            const ImGuiIO& io = ImGui::GetIO();
            return ImGui::IsAnyItemActive() || io.WantTextInput ||
                io.KeyCtrl ||
                ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
                ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
        }

        void HandleTransformGizmoShortcuts(EditorTransformGizmoState& state, bool gameViewFocused) {
            if (!EDITOR::CanUseEditorShortcut(
                    EDITOR::EditorShortcutScope::Viewport,
                    gameViewFocused)) {
                return;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Q) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                state.enabled = false;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_W)) {
                state.enabled = true;
                state.operation = EditorTransformGizmoOperation::Translate;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E)) {
                state.enabled = true;
                state.operation = EditorTransformGizmoOperation::Rotate;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                state.enabled = true;
                state.operation = EditorTransformGizmoOperation::Scale;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_X)) {
                state.mode = state.mode == EditorTransformGizmoMode::World
                    ? EditorTransformGizmoMode::Local
                    : EditorTransformGizmoMode::World;
            }
        }

        const char* LightProbeVolumeOverlayModeName(LightProbeVolumeOverlayMode mode) {
            switch (mode) {
            case LightProbeVolumeOverlayMode::BoundsOnly: return "Bounds Only";
            case LightProbeVolumeOverlayMode::SampledPoints: return "Sampled Points";
            case LightProbeVolumeOverlayMode::AllPoints: return "All Points";
            case LightProbeVolumeOverlayMode::Off:
            default:
                return "Off";
            }
        }

        const char* ReflectionProbeEditTargetName(ReflectionProbeEditTarget target) {
            switch (target) {
            case ReflectionProbeEditTarget::InfluenceBox: return "Influence Box";
            case ReflectionProbeEditTarget::ProjectionBox: return "Projection Box";
            case ReflectionProbeEditTarget::ProbePosition:
            default:
                return "Probe Center";
            }
        }

        bool DrawLightProbeVolumeModeCombo(LightProbeVolumeOverlayMode& mode) {
            bool changed = false;
            if (ImGui::BeginCombo("Light Probe Volume Mode", LightProbeVolumeOverlayModeName(mode))) {
                const LightProbeVolumeOverlayMode modes[] = {
                    LightProbeVolumeOverlayMode::Off,
                    LightProbeVolumeOverlayMode::BoundsOnly,
                    LightProbeVolumeOverlayMode::SampledPoints,
                    LightProbeVolumeOverlayMode::AllPoints,
                };
                for (LightProbeVolumeOverlayMode candidate : modes) {
                    if (ImGui::Selectable(
                            LightProbeVolumeOverlayModeName(candidate),
                            mode == candidate)) {
                        mode = candidate;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        bool DrawReflectionProbeEditTargetCombo(ReflectionProbeEditTarget& target) {
            bool changed = false;
            if (ImGui::BeginCombo("Reflection Probe Edit", ReflectionProbeEditTargetName(target))) {
                const ReflectionProbeEditTarget targets[] = {
                    ReflectionProbeEditTarget::ProbePosition,
                    ReflectionProbeEditTarget::InfluenceBox,
                    ReflectionProbeEditTarget::ProjectionBox,
                };
                for (ReflectionProbeEditTarget candidate : targets) {
                    if (ImGui::Selectable(
                            ReflectionProbeEditTargetName(candidate),
                            target == candidate)) {
                        target = candidate;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        void DrawViewportDebugOptions(
            ViewportOverlayState& overlays,
            ViewportPerformanceState& performance) {

            ImGui::Checkbox("Grid", &overlays.showGrid);
            ImGui::Checkbox("Axis", &overlays.showAxis);
            ImGui::Checkbox("Lights", &overlays.showLights);
            ImGui::Checkbox("Reflection Probe", &overlays.showReflectionProbe);
            ImGui::Checkbox("Light Probe Volume", &overlays.showLightProbeVolume);
            DrawLightProbeVolumeModeCombo(overlays.lightProbeVolumeMode);
            ImGui::Checkbox("Debug Labels", &overlays.showProbeLabels);
            ImGui::Checkbox("XRay Gizmos", &overlays.showXRayGizmos);
            ImGui::Separator();
            ImGui::Checkbox("Edit Reflection Probe", &overlays.editReflectionProbe);
            DrawReflectionProbeEditTargetCombo(overlays.reflectionProbeEditTarget);
            ImGui::Separator();
            ImGui::Checkbox("Disable SSAO In Editor", &performance.disableSsaoInEditorViewport);
        }

        struct RenderDebugViewOption {
            RenderDebugView view = RenderDebugView::None;
            const char* label = "Lit";
        };

        constexpr RenderDebugViewOption kRenderDebugViewOptions[] = {
            { RenderDebugView::None, "Lit" },
            { RenderDebugView::BaseColor, "Base Color" },
            { RenderDebugView::Normal, "Normal" },
            { RenderDebugView::Tangent, "Tangent" },
            { RenderDebugView::Roughness, "Roughness" },
            { RenderDebugView::Metallic, "Metallic" },
            { RenderDebugView::Occlusion, "Occlusion" },
            { RenderDebugView::Shadow, "Shadow" },
            { RenderDebugView::NdotL, "NdotL" },
            { RenderDebugView::Emissive, "Emissive" },
            { RenderDebugView::SceneDepth, "Scene Depth" },
            { RenderDebugView::SceneColor, "Scene Color" },
            { RenderDebugView::MotionVectors, "Motion Vectors" },
            { RenderDebugView::TemporalHistoryWeight, "TAA History Weight" },
            { RenderDebugView::TemporalDepthRejection, "TAA Depth Rejection" },
            { RenderDebugView::TemporalReactiveMask, "TAA Reactive Mask" },
            { RenderDebugView::TemporalTransparencyMask, "TAA Transparency Mask" },
            { RenderDebugView::TemporalDisocclusion, "TAA Disocclusion" },
            { RenderDebugView::TemporalInvalidDepthMotion, "Invalid Depth / Motion" },
            { RenderDebugView::VolumetricScattering, "Volumetric Scattering" },
            { RenderDebugView::VolumetricTransmittance, "Volumetric Transmittance" },
            { RenderDebugView::VolumetricDepthSlice, "Volumetric Depth Slice" },
            { RenderDebugView::MeshletId, "Meshlet ID" },
            { RenderDebugView::ClusterId, "Cluster ID" },
            { RenderDebugView::SurfaceId, "Surface ID" },
            { RenderDebugView::LodLevel, "LOD Level" },
            { RenderDebugView::LodHeat, "LOD Heat" },
            { RenderDebugView::DrawBucket, "Draw Bucket" },
        };

        int RenderDebugViewOptionIndex(RenderDebugView view) {
            for (int i = 0; i < static_cast<int>(std::size(kRenderDebugViewOptions)); ++i) {
                if (kRenderDebugViewOptions[i].view == view) {
                    return i;
                }
            }
            return 0;
        }

        bool DrawRenderDebugViewCombo(const char* label, ViewportDebugViewState& debugView, float width) {
            const int currentIndex = RenderDebugViewOptionIndex(debugView.renderView);
            int selectedIndex = currentIndex;
            bool changed = false;
            ImGui::SetNextItemWidth(width);
            if (ImGui::BeginCombo(label, kRenderDebugViewOptions[currentIndex].label, ImGuiComboFlags_NoArrowButton)) {
                ImGui::SeparatorText("Shading");
                for (int i = 0; i < static_cast<int>(std::size(kRenderDebugViewOptions)); ++i) {
                    const RenderDebugView option = kRenderDebugViewOptions[i].view;
                    if (option == RenderDebugView::SceneDepth) {
                        ImGui::SeparatorText("Screen Space");
                    }
                    if (option == RenderDebugView::MotionVectors) {
                        ImGui::SeparatorText("Temporal");
                    }
                    if (option == RenderDebugView::VolumetricScattering) {
                        ImGui::SeparatorText("Volumetric");
                    }
                    if (option == RenderDebugView::MeshletId) {
                        ImGui::SeparatorText("Meshlet");
                    }
                    const bool selected = (i == selectedIndex);
                    if (ImGui::Selectable(kRenderDebugViewOptions[i].label, selected)) {
                        selectedIndex = i;
                        changed = true;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (changed) {
                debugView.renderView = kRenderDebugViewOptions[selectedIndex].view;
            }
            return changed;
        }

        bool ProjectWorldToViewport(
            const Camera3D& camera,
            const MATH::Vec3& worldPosition,
            const ImVec2& viewportOrigin,
            const ImVec2& viewportSize,
            ImVec2& outScreenPosition) {

            const MATH::Vec4 clip = camera.GetViewProj().TransformPoint({
                worldPosition.x,
                worldPosition.y,
                worldPosition.z,
                1.0f
            });
            if (std::fabs(clip.w) <= 1.0e-5f) {
                return false;
            }

            const float invW = 1.0f / clip.w;
            const float ndcX = clip.x * invW;
            const float ndcY = clip.y * invW;
            const float ndcZ = clip.z * invW;
            if (ndcZ < 0.0f || ndcZ > 1.0f) {
                return false;
            }

            outScreenPosition.x = viewportOrigin.x + (ndcX * 0.5f + 0.5f) * viewportSize.x;
            outScreenPosition.y = viewportOrigin.y + (-ndcY * 0.5f + 0.5f) * viewportSize.y;
            return outScreenPosition.x >= viewportOrigin.x &&
                outScreenPosition.x <= viewportOrigin.x + viewportSize.x &&
                outScreenPosition.y >= viewportOrigin.y &&
                outScreenPosition.y <= viewportOrigin.y + viewportSize.y;
        }

        void DrawViewportLabel(
            ImDrawList* drawList,
            const ImVec2& position,
            const char* text,
            ImU32 color) {

            if (drawList == nullptr || text == nullptr || text[0] == '\0') {
                return;
            }

            const ImVec2 padding{ 6.0f, 4.0f };
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            const ImVec2 min{ position.x + 8.0f, position.y - 8.0f };
            const ImVec2 max{ min.x + textSize.x + padding.x * 2.0f, min.y + textSize.y + padding.y * 2.0f };
            drawList->AddRectFilled(min, max, IM_COL32(9, 13, 18, 205), 3.0f);
            drawList->AddRect(min, max, color, 3.0f, 0, 1.0f);
            drawList->AddText(ImVec2(min.x + padding.x, min.y + padding.y), color, text);
        }

        void DrawReflectionProbeLabels(
            DocumentSceneBase& scene,
            const ViewportOverlayState& overlays,
            const ImVec2& viewportOrigin,
            const ImVec2& viewportSize) {

            const SceneEnvironment& environment = scene.GetSceneEnvironment();
            const ReflectionProbeSettings& probe = environment.reflectionProbe;
            if (!overlays.showReflectionProbe || !overlays.showProbeLabels || !probe.enabled) {
                return;
            }

            const REFLECTION::ReflectionProbeRuntimeData& runtimeProbe = REFLECTION::GetActiveProbe();
            const RENDER3D::LIGHTING::SceneLightingRuntimeData& lighting =
                RENDER3D::LIGHTING::GetLastLightingRuntimeData();
            const char* runtimeState = runtimeProbe.valid
                ? (lighting.source == RENDER3D::LIGHTING::LightingRuntimeSource::BakedRuntime ? "Baked" : "Authoring")
                : "Invalid";

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 screen{};
            if (ProjectWorldToViewport(scene.GetCamera(), probe.position, viewportOrigin, viewportSize, screen)) {
                drawList->AddCircleFilled(screen, 4.0f, IM_COL32(97, 230, 168, 230));
                char label[160]{};
                std::snprintf(
                    label,
                    sizeof(label),
                    "Reflection Probe\n%s\nInfluence: %s\nProjection: %s",
                    runtimeState,
                    probe.influenceShape == ReflectionProbeInfluenceShape::Box ? "Box" : "Sphere",
                    probe.projectionShape == ReflectionProbeProjectionShape::Box ? "Box" : "Infinite");
                DrawViewportLabel(drawList, screen, label, IM_COL32(150, 236, 205, 235));
            }

            if (probe.influenceShape == ReflectionProbeInfluenceShape::Box &&
                ProjectWorldToViewport(scene.GetCamera(), probe.influenceBoxCenter, viewportOrigin, viewportSize, screen)) {
                drawList->AddRectFilled(
                    ImVec2(screen.x - 3.0f, screen.y - 3.0f),
                    ImVec2(screen.x + 3.0f, screen.y + 3.0f),
                    IM_COL32(94, 199, 255, 230));
                DrawViewportLabel(drawList, screen, "Influence", IM_COL32(94, 199, 255, 230));
            }

            if (probe.projectionShape == ReflectionProbeProjectionShape::Box &&
                ProjectWorldToViewport(scene.GetCamera(), probe.projectionBoxCenter, viewportOrigin, viewportSize, screen)) {
                drawList->AddRectFilled(
                    ImVec2(screen.x - 3.0f, screen.y - 3.0f),
                    ImVec2(screen.x + 3.0f, screen.y + 3.0f),
                    IM_COL32(255, 209, 102, 230));
                DrawViewportLabel(drawList, screen, "Projection Proxy", IM_COL32(255, 209, 102, 230));
            }
        }

        ImU32 ToImGuiLightColor(const MATH::Vec3& color, int alpha) {
            const auto toByte = [](float value) -> int {
                return static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
            };
            return IM_COL32(toByte(color.x), toByte(color.y), toByte(color.z), alpha);
        }

        void DrawPointLightIcon(ImDrawList* drawList, const ImVec2& center, ImU32 color) {
            constexpr float kRadius = 6.0f;
            drawList->AddCircleFilled(center, kRadius, color, 12);
            drawList->AddCircle(center, kRadius + 2.0f, IM_COL32(255, 244, 204, 230), 12, 1.2f);
            drawList->AddLine(ImVec2(center.x - 11.0f, center.y), ImVec2(center.x - 7.5f, center.y), color, 1.2f);
            drawList->AddLine(ImVec2(center.x + 7.5f, center.y), ImVec2(center.x + 11.0f, center.y), color, 1.2f);
            drawList->AddLine(ImVec2(center.x, center.y - 11.0f), ImVec2(center.x, center.y - 7.5f), color, 1.2f);
            drawList->AddLine(ImVec2(center.x, center.y + 7.5f), ImVec2(center.x, center.y + 11.0f), color, 1.2f);
            drawList->AddText(ImVec2(center.x - 3.5f, center.y - 6.5f), IM_COL32(24, 21, 13, 245), "P");
        }

        void DrawDirectionalLightIcon(ImDrawList* drawList, const ImVec2& center, ImU32 color) {
            constexpr float kRadius = 7.0f;
            drawList->AddCircleFilled(center, kRadius, color, 16);
            drawList->AddCircle(center, kRadius + 2.0f, IM_COL32(255, 248, 214, 230), 16, 1.2f);
            for (int i = 0; i < 8; ++i) {
                const float angle = (static_cast<float>(i) / 8.0f) * 6.28318530718f;
                const ImVec2 from{
                    center.x + std::cos(angle) * 10.0f,
                    center.y + std::sin(angle) * 10.0f
                };
                const ImVec2 to{
                    center.x + std::cos(angle) * 14.0f,
                    center.y + std::sin(angle) * 14.0f
                };
                drawList->AddLine(from, to, color, 1.2f);
            }
        }

        void DrawLightOverlayIcons(
            DocumentSceneBase& scene,
            const ViewportOverlayState& overlays,
            const ImVec2& viewportOrigin,
            const ImVec2& viewportSize) {

            const SceneEnvironment& environment = scene.GetSceneEnvironment();
            if (!overlays.showLights || !environment.showLightDebug) {
                return;
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const Camera3D& camera = scene.GetCamera();

            if (environment.directional.enabled) {
                const MATH::Vec3 origin{ 0.0f, 1.5f, 0.0f };
                ImVec2 screen{};
                if (ProjectWorldToViewport(camera, origin, viewportOrigin, viewportSize, screen)) {
                    const ImU32 color = ToImGuiLightColor(environment.directional.color, 235);
                    DrawDirectionalLightIcon(drawList, screen, color);
                    if (overlays.showProbeLabels) {
                        DrawViewportLabel(drawList, screen, "Directional Light", color);
                    }
                }
            }

            if (!environment.showPointLightMarkers) {
                return;
            }

            constexpr size_t kMaxPointLightIcons = 32u;
            constexpr size_t kMaxPointLightLabels = 8u;
            size_t iconCount = 0u;
            size_t labelCount = 0u;
            for (size_t i = 0; i < environment.pointLights.size(); ++i) {
                const PointLight& light = environment.pointLights[i];
                if (!light.enabled || light.range <= 0.0f) {
                    continue;
                }
                if (iconCount >= kMaxPointLightIcons) {
                    break;
                }

                ImVec2 screen{};
                if (!ProjectWorldToViewport(camera, light.position, viewportOrigin, viewportSize, screen)) {
                    continue;
                }

                const ImU32 color = ToImGuiLightColor(light.color, 235);
                DrawPointLightIcon(drawList, screen, color);
                ++iconCount;
                if (overlays.showProbeLabels && labelCount < kMaxPointLightLabels) {
                    char label[96]{};
                    std::snprintf(
                        label,
                        sizeof(label),
                        "Point Light %zu\nRange %.1f / Int %.1f",
                        i,
                        light.range,
                        light.intensity);
                    DrawViewportLabel(drawList, screen, label, color);
                    ++labelCount;
                }
            }
        }

        void ClampReflectionProbeBoxSize(MATH::Vec3& size) {
            size.x = (std::max)(0.001f, size.x);
            size.y = (std::max)(0.001f, size.y);
            size.z = (std::max)(0.001f, size.z);
        }

        bool CanEditReflectionProbeTarget(
            const ReflectionProbeSettings& probe,
            ReflectionProbeEditTarget target) {

            if (!probe.enabled) {
                return false;
            }
            if (target == ReflectionProbeEditTarget::InfluenceBox) {
                return probe.influenceShape == ReflectionProbeInfluenceShape::Box;
            }
            if (target == ReflectionProbeEditTarget::ProjectionBox) {
                return probe.projectionShape == ReflectionProbeProjectionShape::Box;
            }
            return true;
        }

        TransformData BuildReflectionProbeEditTransform(
            const ReflectionProbeSettings& probe,
            ReflectionProbeEditTarget target) {

            TransformData transform{};
            transform.scale = { 1.0f, 1.0f, 1.0f };
            if (target == ReflectionProbeEditTarget::InfluenceBox) {
                transform.position = probe.influenceBoxCenter;
                transform.scale = probe.influenceBoxSize;
                return transform;
            }
            if (target == ReflectionProbeEditTarget::ProjectionBox) {
                transform.position = probe.projectionBoxCenter;
                transform.scale = probe.projectionBoxSize;
                return transform;
            }
            transform.position = probe.position;
            return transform;
        }

        EditorTransformGizmoState BuildReflectionProbeGizmoState(
            const EditorTransformGizmoState& source,
            ReflectionProbeEditTarget target) {

            EditorTransformGizmoState state = source;
            if (target == ReflectionProbeEditTarget::ProbePosition ||
                state.operation == EditorTransformGizmoOperation::Rotate) {
                state.operation = EditorTransformGizmoOperation::Translate;
            }
            return state;
        }

        void ApplyReflectionProbeEditTransform(
            ReflectionProbeSettings& probe,
            ReflectionProbeEditTarget target,
            const EDITOR::EditorTransformGizmoResult& result,
            EditorTransformGizmoOperation operation) {

            if (target == ReflectionProbeEditTarget::InfluenceBox) {
                probe.influenceBoxCenter = result.transform.position;
                if (operation == EditorTransformGizmoOperation::Scale) {
                    probe.influenceBoxSize = result.transform.scale;
                    ClampReflectionProbeBoxSize(probe.influenceBoxSize);
                }
                return;
            }
            if (target == ReflectionProbeEditTarget::ProjectionBox) {
                probe.projectionBoxCenter = result.transform.position;
                if (operation == EditorTransformGizmoOperation::Scale) {
                    probe.projectionBoxSize = result.transform.scale;
                    ClampReflectionProbeBoxSize(probe.projectionBoxSize);
                }
                return;
            }
            probe.position = result.transform.position;
        }

        void DrawEditorDockSpace(bool resetDefaultDockLayout) {
            ImGuiIO& io = ImGui::GetIO();
            if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
                return;
            }

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const ImGuiID dockspaceId = ImGui::GetID("HIKARI_EditorDockSpace");
            const ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

            static bool initializedDefaultDockLayout = false;
            if (!initializedDefaultDockLayout || resetDefaultDockLayout) {
                const bool needsDefaultLayout = ImGui::DockBuilderGetNode(dockspaceId) == nullptr;
                initializedDefaultDockLayout = true;
                if (!needsDefaultLayout && !resetDefaultDockLayout) {
                    ImGui::DockSpaceOverViewport(dockspaceId, viewport, dockspaceFlags);
                    return;
                }

                ImGui::DockBuilderRemoveNode(dockspaceId);
                ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | dockspaceFlags);
                ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
                ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

                ImGuiID mainNode = dockspaceId;
                ImGuiID leftNode = 0;
                ImGuiID rightNode = 0;
                ImGuiID bottomNode = 0;
                ImGui::DockBuilderSplitNode(
                    mainNode,
                    ImGuiDir_Left,
                    0.19f,
                    &leftNode,
                    &mainNode);
                ImGui::DockBuilderSplitNode(
                    mainNode,
                    ImGuiDir_Down,
                    0.24f,
                    &bottomNode,
                    &mainNode);
                ImGui::DockBuilderSplitNode(
                    mainNode,
                    ImGuiDir_Right,
                    0.28f,
                    &rightNode,
                    &mainNode);

                ImGui::DockBuilderDockWindow("Game View", mainNode);
                ImGui::DockBuilderDockWindow("Scene Workspace", leftNode);
                ImGui::DockBuilderDockWindow("Inspector", rightNode);
                ImGui::DockBuilderDockWindow("Resource Workspace", bottomNode);
                ImGui::DockBuilderDockWindow("Environment", bottomNode);
                ImGui::DockBuilderDockWindow("Quality", bottomNode);
                ImGui::DockBuilderDockWindow("Lighting Bake", bottomNode);
                ImGui::DockBuilderDockWindow("Diagnostics", bottomNode);
                ImGui::DockBuilderDockWindow("Debug View", bottomNode);
                ImGui::DockBuilderDockWindow("Performance Audit", bottomNode);
                ImGui::DockBuilderDockWindow("Renderer Health", bottomNode);

                ImGui::DockBuilderFinish(dockspaceId);
            }

            ImGui::DockSpaceOverViewport(dockspaceId, viewport, dockspaceFlags);
        }

#endif
    }

    DocumentSceneEditorController::DocumentSceneEditorController() {
        EDITOR::RegisterBuiltInEditorTools(toolHost_);
        EDITOR::RegisterBuiltInSystemAuthoring(
            systemAuthoringRegistry_);
    }

    void DocumentSceneEditorController::Draw(
        DocumentSceneBase& scene,
        EDITOR::EditorPlaySession& playSession) {
#if defined(HIKARI_WITH_EDITOR)
        scene.GetAssetDatabase().PumpAssetTasks();
        SyncDocumentHistory(scene);
        context_.selection.RepairObjectSelection(scene.GetWorld());
        context_.gizmos.lockedObjectIds =
            EDITOR::CollectEditorLockedObjectIds(
                scene.GetSceneDocument());
        viewportSelectionService_.SetLockedObjectIds(
            context_.gizmos.lockedObjectIds);
        cinematicsWorkspaceController_.SyncSceneIdentity(
            scene,
            workspaceHost_);
        ConfigureEditorCommands(scene);
        if (commandRouter_.ProcessDocumentShortcuts()) {
            ConfigureEditorCommands(scene);
        }

        documentToolbarController_.SyncDocumentMeta(scene, context_, selectionSync_);
        scene.SetUnsavedSceneChanges(context_.sceneDirty);
        if (context_.selection.selectedObject) {
            scene.SetSelectedGizmoObjectId(context_.selection.selectedObject->GetDocumentId());
        } else {
            scene.SetSelectedGizmoObjectId(SceneObjectId{});
        }
        scene.SetComponentGizmoState(context_.gizmos);
        scene.SetViewportOverlayState(context_.overlays);
        scene.SetViewportPerformanceState(context_.viewportPerformance);
        scene.SetViewportDebugViewState(context_.viewportDebug);

        bool resetDockingLayoutRequested = false;
        debugMenuBar_.Draw(
            context_.windows,
            toolHost_,
            workspaceHost_,
            scene.GetDebugCamera(),
            scene.GetEnvironmentLightingEnabled(),
            resetDockingLayoutRequested,
            commandRouter_);

        if (resetDockingLayoutRequested) {
            workspaceHost_.RequestResetActiveLayout();
        }
        if (const std::optional<EDITOR::EditorWorkspaceActivation> activation =
                workspaceHost_.ApplyPending()) {
            cinematicsWorkspaceController_.ApplyWorkspaceActivation(
                scene,
                *activation,
                context_,
                workspaceHost_);
            modelCollisionWorkspaceController_.ApplyWorkspaceActivation(
                scene,
                *activation,
                workspaceHost_);
            animationStateMachineWorkspaceController_.ApplyWorkspaceActivation(
                scene,
                *activation);
        }

        if (workspaceHost_.IsActive(
                EDITOR::EditorWorkspaceId::AnimationStateMachine)) {
            animationStateMachineWorkspaceController_.DrawDockSpace(
                workspaceHost_.ConsumeReset(
                    EDITOR::EditorWorkspaceId::AnimationStateMachine));
            const EDITOR::AnimationStateMachineWorkspaceResult result =
                animationStateMachineWorkspaceController_.Draw(
                    scene,
                    commandRouter_);
            if (result.exitToSceneRequested) {
                EDITOR::EditorWorkspaceOpenRequest request{};
                request.workspaceId = EDITOR::EditorWorkspaceId::Scene;
                (void)workspaceHost_.RequestOpen(std::move(request));
            }
            if (!result.statusMessage.empty()) {
                viewportDropMessage_ = result.statusMessage;
            }
            DrawPendingSceneOpenModal(scene);
            if (renderQualitySavePending_ && !ImGui::IsAnyItemActive()) {
                (void)SaveRenderQualityProfile(scene);
            }
            return;
        }

        if (workspaceHost_.IsActive(
                EDITOR::EditorWorkspaceId::ModelCollision)) {
            modelCollisionWorkspaceController_.DrawDockSpace(
                workspaceHost_.ConsumeReset(
                    EDITOR::EditorWorkspaceId::ModelCollision));
            const EDITOR::ModelCollisionWorkspaceResult result =
                modelCollisionWorkspaceController_.Draw(
                    scene,
                    workspaceHost_,
                    commandRouter_);
            if (result.exitToSceneRequested) {
                EDITOR::EditorWorkspaceOpenRequest request{};
                request.workspaceId = EDITOR::EditorWorkspaceId::Scene;
                (void)workspaceHost_.RequestOpen(std::move(request));
            }
            if (!result.statusMessage.empty()) {
                viewportDropMessage_ = result.statusMessage;
            }
            DrawPendingSceneOpenModal(scene);
            if (renderQualitySavePending_ && !ImGui::IsAnyItemActive()) {
                (void)SaveRenderQualityProfile(scene);
            }
            return;
        }

        if (workspaceHost_.IsActive(EDITOR::EditorWorkspaceId::Cinematics)) {
            const bool externalDirtyBefore =
                historyExternalDirty_ ||
                ((context_.sceneDirty || scene.HasUnsavedSceneChanges()) &&
                    !documentHistory_.IsDirty());
            std::optional<SceneCinematicsSettings> cinematicsBefore{};
            if (CouldMutateEditorDocumentThisFrame()) {
                cinematicsBefore = scene.GetSceneDocument().cinematics;
            }
            cinematicsWorkspaceController_.DrawDockSpace(
                workspaceHost_.ConsumeReset(
                    EDITOR::EditorWorkspaceId::Cinematics));
            const EDITOR::CinematicsWorkspaceResult result =
                cinematicsWorkspaceController_.Draw(
                    scene,
                    playSession,
                    context_,
                    selectionSync_,
                    workspaceHost_);
            if (result.cinematicsChanged && cinematicsBefore) {
                RecordCinematicsHistory(
                    scene,
                    std::move(*cinematicsBefore),
                    result.timelineEditMergeId,
                    externalDirtyBefore);
            } else if (result.timelineEditMergeId == 0) {
                documentHistory_.SealMerge();
            }
            if (result.saveSceneRequested) {
                (void)commandRouter_.Execute(
                    EDITOR::EditorCommandId::SaveDocument);
            }
            if (result.toggleGamePreviewRequested) {
                ToggleGamePreview(scene, playSession);
            }
            if (!result.statusMessage.empty()) {
                viewportDropMessage_ = result.statusMessage;
            }
            DrawPendingSceneOpenModal(scene);
            if (renderQualitySavePending_ && !ImGui::IsAnyItemActive()) {
                (void)SaveRenderQualityProfile(scene);
            }
            return;
        }

#if defined(HIKARI_WITH_EDITOR)
        DrawEditorDockSpace(
            workspaceHost_.ConsumeReset(EDITOR::EditorWorkspaceId::Scene));
#endif

        if (context_.windows.viewport.showGameView) {
            DrawGameViewportWindow(scene, playSession);
        } else {
            playSession.ReleaseEmbeddedInput();
            viewportTransformHistory_.BeginFrame();
            viewportTransformHistory_.EndFrame(
                scene.GetSceneDocument());
            EDITOR::ClearGameViewportInputRect();
            SERVICES::SetEditorGameViewportSize(0, 0, false);
        }
        if (context_.windows.authoring.showSceneWorkspace) {
            DrawSceneWorkspaceWindow(scene);
        }
        if (context_.windows.authoring.showInspector) {
            DrawInspectorWindow(scene);
        }
        const SceneSystemsPanelResult systemsResult =
            sceneAuthoringUtilityWindows_.Draw(
                scene,
                context_.windows.authoring,
                SERVICES::GetInputService(),
                systemAuthoringRegistry_,
                toolHost_);
        if (systemsResult.changed) {
            historyExternalDirty_ |=
                context_.sceneDirty || scene.HasUnsavedSceneChanges();
            documentHistory_.RecordApplied(
                EDITOR::MakeSceneSystemsHistoryCommand(
                    systemsResult.label,
                    systemsResult.before,
                    scene.GetSceneDocument().systems));
            context_.sceneDirty = true;
            scene.SetUnsavedSceneChanges(true);
            sceneAuthoringUtilityWindows_.SetSystemsRuntimeApplyStatus(
                scene.ApplySystemRuntimeChanges());
        }
        sceneCreationPanel_.DrawDeferredDialogs(
            scene,
            context_,
            selectionSync_);
        sceneInspectorPanel_.DrawDeferredDialogs(
            scene,
            context_,
            selectionSync_,
            sceneObjectCommands_);
        const auto recordSceneObjectHistory =
            [&](std::optional<SceneObjectAuthoringHistoryRequest> history) {
            if (!history) {
                return;
            }
            historyExternalDirty_ |=
                history->dirtyBefore && !documentHistory_.IsDirty();
            documentHistory_.RecordApplied(
                EDITOR::MakeSceneObjectsHistoryCommand(
                    std::move(history->label),
                    std::move(history->beforeObjects),
                    std::move(history->afterObjects),
                    std::move(history->beforeCamera),
                    std::move(history->afterCamera),
                    history->runtimeWorldAffected));
            context_.sceneDirty = true;
            scene.SetUnsavedSceneChanges(true);
        };
        recordSceneObjectHistory(
            sceneCreationPanel_.ConsumeHistoryRequest());
        recordSceneObjectHistory(
            sceneInspectorPanel_.ConsumeHistoryRequest());
        recordSceneObjectHistory(
            viewportTransformHistory_.ConsumeHistoryRequest());
        recordSceneObjectHistory(
            sceneObjectCommands_.ConsumeHistoryRequest());
        if (const std::optional<SceneObjectId> cameraRequest =
                sceneInspectorPanel_.ConsumeOpenCinematicsWorkspaceCameraRequest()) {
            EDITOR::EditorWorkspaceOpenRequest request{};
            request.workspaceId = EDITOR::EditorWorkspaceId::Cinematics;
            request.targetCameraObjectId = *cameraRequest;
            (void)workspaceHost_.RequestOpen(std::move(request));
        }
        if (const std::optional<SceneObjectId> focusRequest =
                sceneInspectorPanel_.ConsumeFocusObjectRequest()) {
            FocusSceneObjects(scene, { *focusRequest });
        }
        if (context_.windows.resources.showAssetBrowser) {
            ProjectSettingsService projectSettings{};
            projectSettings.Load(scene.GetAssetDatabase().GetProjectRoot());
            const ResourceWorkspaceContext resourceContext{
                scene.GetCurrentSceneAssetGuid(),
                projectSettings.GetSettings().startupSceneGuid,
                context_.sceneDirty || scene.HasUnsavedSceneChanges()
            };
            resourceWorkspacePanel_.Draw(
                scene.GetAssetDatabase(),
                scene.GetAssetRegistry(),
                scene.GetSceneDocument(),
                context_.selection,
                resourceContext);

            const std::string activatedModelCollisionGuid =
                resourceWorkspacePanel_.ConsumeActivatedModelCollisionGuid();
            if (!activatedModelCollisionGuid.empty()) {
                EDITOR::EditorWorkspaceOpenRequest request{};
                request.workspaceId =
                    EDITOR::EditorWorkspaceId::ModelCollision;
                request.modelAssetGuid =
                    AssetGuid{ activatedModelCollisionGuid };
                (void)workspaceHost_.RequestOpen(std::move(request));
            }

            const std::string activatedAnimationStateMachineGuid =
                resourceWorkspacePanel_.
                    ConsumeActivatedAnimationStateMachineGuid();
            if (!activatedAnimationStateMachineGuid.empty()) {
                EDITOR::EditorWorkspaceOpenRequest request{};
                request.workspaceId =
                    EDITOR::EditorWorkspaceId::AnimationStateMachine;
                request.animationStateMachineAssetGuid =
                    AssetGuid{ activatedAnimationStateMachineGuid };
                (void)workspaceHost_.RequestOpen(std::move(request));
            }

            const std::string saveSceneAsGuid = resourceWorkspacePanel_.ConsumeSaveSceneAsGuid();
            if (!saveSceneAsGuid.empty()) {
                if (scene.SaveCurrentSceneDocumentAs(AssetGuid{ saveSceneAsGuid })) {
                    context_.sceneDirty = false;
                    scene.SetUnsavedSceneChanges(false);
                    viewportDropMessage_ = "Scene saved to selected asset";
                    HIKARI_LOG_INFO("[SceneAsset] save scene: " + saveSceneAsGuid);
                } else {
                    viewportDropMessage_ = "Scene save target failed";
                    HIKARI_LOG_WARN("[SceneAsset] save scene failed: " + saveSceneAsGuid);
                }
            }

            const std::string activatedSceneGuid = resourceWorkspacePanel_.ConsumeActivatedSceneGuid();
            if (!activatedSceneGuid.empty()) {
                pendingSceneOpenGuid_ = AssetGuid{ activatedSceneGuid };
                if (context_.sceneDirty || scene.HasUnsavedSceneChanges()) {
                    ImGui::OpenPopup("Unsaved Scene Changes");
                } else {
                    OpenSceneAssetFromEditor(scene, pendingSceneOpenGuid_);
                    pendingSceneOpenGuid_ = {};
                }
            }

            RuntimeResourceRefreshService refreshService{};
            auto applyRefreshReport = [this](RuntimeResourceRefreshReport report) {
                context_.lastRuntimeRefreshReport = std::move(report);
                viewportDropMessage_ = SummarizeRuntimeRefreshReport(context_.lastRuntimeRefreshReport);
            };

            if (resourceWorkspacePanel_.ConsumeRefreshCurrentSceneResourcesRequested()) {
                EDITOR::ClearMaterialTextureSlotPreviewCache();
                // 迴ｾ蝨ｨ縺ｮ SceneDocument 縺ｫ蜃ｺ縺ｦ縺上ｋ萓晏ｭ・resource 繧偵∪縺ｨ繧√※蠑ｵ繧顔峩縺吶・
                applyRefreshReport(refreshService.RefreshCurrentSceneResources(scene));
            }

            const std::string refreshRuntimeGuid = resourceWorkspacePanel_.ConsumeRefreshRuntimeAssetGuid();
            if (!refreshRuntimeGuid.empty()) {
                EDITOR::ClearMaterialTextureSlotPreviewCache();
                applyRefreshReport(refreshService.RefreshAsset(scene, AssetId{ refreshRuntimeGuid }));
            }

            const std::string reimportAndRefreshRuntimeGuid =
                resourceWorkspacePanel_.ConsumeReimportAndRefreshRuntimeAssetGuid();
            if (!reimportAndRefreshRuntimeGuid.empty()) {
                EDITOR::ClearMaterialTextureSlotPreviewCache();
                applyRefreshReport(refreshService.RefreshAsset(scene, AssetId{ reimportAndRefreshRuntimeGuid }));
            }

            AssetGuid applyMaterialGuid{};
            PbrMaterialAssetData applyMaterialData{};
            if (resourceWorkspacePanel_.ConsumeApplyRuntimeMaterialRequest(applyMaterialGuid, applyMaterialData)) {
                const int rebuilt = scene.ApplyRuntimeMaterialOverridePreview(
                    applyMaterialGuid,
                    applyMaterialData);
                context_.lastRuntimeRefreshReport = {};
                context_.lastRuntimeRefreshReport.materialReboundComponentCount = rebuilt;
                context_.lastRuntimeRefreshReport.messages.push_back(
                    "Applied material runtime preview: " + applyMaterialGuid.value);
                viewportDropMessage_ = SummarizeRuntimeRefreshReport(context_.lastRuntimeRefreshReport);
            }

            const std::string refreshRuntimeMaterialGuid =
                resourceWorkspacePanel_.ConsumeRefreshRuntimeMaterialGuid();
            if (!refreshRuntimeMaterialGuid.empty()) {
                EDITOR::ClearMaterialTextureSlotPreviewCache();
                applyRefreshReport(refreshService.RefreshAsset(scene, AssetId{ refreshRuntimeMaterialGuid }));
            }
        }
        if (context_.windows.resources.showEnvironment) {
            const EnvironmentPanelResult environmentResult =
                environmentPanel_.Draw(
                    scene.GetSceneEnvironment(),
                    &SKYRENDERER::GetDebugState(),
                    &scene.GetAssetRegistry(),
                    &scene.GetAssetDatabase());
            if (environmentResult.environmentChanged) {
                scene.ApplyEnvironmentRuntimeChanges();
                context_.sceneDirty = true;
            }
            if (environmentResult.openLightingBakeRequested) {
                EDITOR::EditorToolOpenRequest request{};
                request.toolId = EDITOR::kLightingBakeToolId;
                request.target.kind = EDITOR::EditorToolTargetKind::Scene;
                request.target.typeId = "ReflectionProbe";
                request.target.assetGuid =
                    scene.GetCurrentSceneAssetGuid().value;
                (void)toolHost_.RequestOpen(std::move(request));
            }
        }
        if (context_.windows.resources.showQuality) {
            const QualityPanelResult qualityResult =
                qualityPanel_.Draw(scene.GetSceneEnvironment());
            if (qualityResult.environmentChanged) {
                scene.ApplyEnvironmentRuntimeChanges();
                context_.sceneDirty = true;
            }
            renderQualitySavePending_ |= qualityResult.renderQualityChanged;
        }
        EDITOR::EditorToolContext toolContext{ scene, context_ };
        toolHost_.Draw(toolContext);
        if (context_.windows.runtime.showDebugWorkspace) {
            DrawDebugWorkspaceWindow(scene);
        }
        if (context_.windows.runtime.showDebugView) {
            DrawDebugViewWindow(scene, context_.windows.runtime.showDebugView);
        }
        if (context_.windows.runtime.showPerformanceAudit) {
            performanceAuditPanel_.Draw(context_.windows.runtime.showPerformanceAudit);
        }
        if (context_.windows.runtime.showValidationLab) {
            validationLabPanel_.Draw(context_, context_.windows.runtime.showValidationLab);
        }
        DrawPendingSceneOpenModal(scene);
        if (renderQualitySavePending_ && !ImGui::IsAnyItemActive()) {
            (void)SaveRenderQualityProfile(scene);
        }
#else
        (void)scene;
        (void)playSession;
#endif
    }


    bool DocumentSceneEditorController::SaveRenderQualityProfile(
        DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        std::string errorMessage{};
        if (!RENDER3D::SaveRenderQualityProfile(
                scene.GetAssetDatabase().GetProjectRoot(),
                RENDER3D::GetRenderQualitySettings(),
                &errorMessage)) {
            viewportDropMessage_ = errorMessage;
            return false;
        }

        renderQualitySavePending_ = false;
        return true;
#else
        (void)scene;
        return false;
#endif
    }

    void DocumentSceneEditorController::ToggleGamePreview(
        DocumentSceneBase& scene,
        EDITOR::EditorPlaySession& playSession) {
#if defined(HIKARI_WITH_EDITOR)
        if (playSession.IsRunning() ||
            playSession.GetState() == EDITOR::EditorPlayState::Starting) {
            playSession.RequestStop();
            viewportDropMessage_ = playSession.GetStatusMessage();
            return;
        }

        if (!PrepareGamePreview(scene)) {
            return;
        }
        playSession.RequestEmbeddedStart();
        viewportDropMessage_ = playSession.GetStatusMessage();
#else
        (void)scene;
        (void)playSession;
#endif
    }

    void DocumentSceneEditorController::LaunchWindowedGamePreview(
        DocumentSceneBase& scene,
        EDITOR::EditorPlaySession& playSession) {
#if defined(HIKARI_WITH_EDITOR)
        if (playSession.IsRunning() ||
            playSession.GetState() == EDITOR::EditorPlayState::Starting) {
            viewportDropMessage_ = "Stop the active Play session first.";
            return;
        }
        if (!PrepareGamePreview(scene)) {
            return;
        }
        playSession.RequestWindowedStart();
        viewportDropMessage_ = playSession.GetStatusMessage();
#else
        (void)scene;
        (void)playSession;
#endif
    }

    bool DocumentSceneEditorController::PrepareGamePreview(
        DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        cinematicsWorkspaceController_.PrepareForRuntimePlay();
        const AssetGuid& sceneGuid = scene.GetCurrentSceneAssetGuid();
        if (!sceneGuid.IsValid()) {
            viewportDropMessage_ =
                "Play requires the current scene to be saved as an asset";
            return false;
        }

        scene.GetSceneDocument().environment = scene.GetSceneEnvironment();
        if (!scene.SaveCurrentSceneDocument()) {
            viewportDropMessage_ = "Could not save the current scene for Play";
            return false;
        }

        context_.sceneDirty = false;
        scene.SetUnsavedSceneChanges(false);
        if (!SaveRenderQualityProfile(scene)) {
            return false;
        }
        return true;
#else
        (void)scene;
        return false;
#endif
    }

    void DocumentSceneEditorController::DrawGameViewportWindow(
        DocumentSceneBase& scene,
        EDITOR::EditorPlaySession& playSession) {
#if defined(HIKARI_WITH_EDITOR)
        viewportTransformHistory_.BeginFrame();
        bool open = context_.windows.viewport.showGameView;
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoCollapse;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (!ImGui::Begin("Game View", &open, flags)) {
            context_.windows.viewport.showGameView = open;
            playSession.ReleaseEmbeddedInput();
            EDITOR::ClearGameViewportInputRect();
            SERVICES::SetEditorGameViewportSize(0, 0, false);
            viewportTransformHistory_.EndFrame(
                scene.GetSceneDocument());
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }
        context_.windows.viewport.showGameView = open;
        if (!open) {
            playSession.ReleaseEmbeddedInput();
        }

        const bool selectedObjectIsCamera =
            context_.selection.selectedObject != nullptr &&
            context_.selection.selectedObject->GetComponent<CameraComponent>() != nullptr;
        const std::string activatedSequenceGuid =
            resourceWorkspacePanel_.ConsumeActivatedSequenceGuid();
        if (!activatedSequenceGuid.empty()) {
            EDITOR::EditorWorkspaceOpenRequest request{};
            request.workspaceId = EDITOR::EditorWorkspaceId::Cinematics;
            request.sequenceAssetGuid = AssetGuid{ activatedSequenceGuid };
            (void)workspaceHost_.RequestOpen(std::move(request));
        }

        const float toolbarHeight = context_.windows.viewport.showViewportHud ? 38.0f : 0.0f;
        if (toolbarHeight > 0.0f) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.065f, 0.080f, 1.0f));
            if (ImGui::BeginChild("##GameViewToolbar", ImVec2(0.0f, toolbarHeight), false, ImGuiWindowFlags_NoScrollbar)) {
                ImGui::SetCursorPosY(8.0f);
                ImGui::Dummy(ImVec2(8.0f, 0.0f));
                ImGui::SameLine();
                const bool previewRunning = playSession.IsRunning();
                if (EDITOR::IconButton(
                        previewRunning
                            ? EDITOR::EditorGlyph::Stop
                            : EDITOR::EditorGlyph::Play,
                        "EmbeddedPlay",
                        previewRunning
                            ? EDITOR::EditorButtonTone::Danger
                            : EDITOR::EditorButtonTone::Primary,
                        ImVec2(26.0f, 26.0f),
                        previewRunning
                            ? "Stop Play"
                            : "Play in Game View")) {
                    ToggleGamePreview(scene, playSession);
                }
                ImGui::SameLine(0.0f, 3.0f);
                if (ImGui::ArrowButton("##PlayModeMenu", ImGuiDir_Down)) {
                    ImGui::OpenPopup("Play Mode Menu");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Play options");
                }
                if (ImGui::BeginPopup("Play Mode Menu")) {
                    if (ImGui::MenuItem(
                            "Play in New Window",
                            nullptr,
                            false,
                            !previewRunning)) {
                        LaunchWindowedGamePreview(scene, playSession);
                    }
                    ImGui::EndPopup();
                }
                if (playSession.IsEmbeddedRunning()) {
                    ImGui::SameLine();
                    if (EDITOR::IconButton(
                            playSession.IsPaused()
                                ? EDITOR::EditorGlyph::Play
                                : EDITOR::EditorGlyph::Pause,
                            "EmbeddedPlayPause",
                            playSession.IsPaused()
                                ? EDITOR::EditorButtonTone::Primary
                                : EDITOR::EditorButtonTone::Neutral,
                            ImVec2(26.0f, 26.0f),
                            playSession.IsPaused()
                                ? "Resume Play"
                                : "Pause Play")) {
                        playSession.TogglePause();
                    }
                }
                if (playSession.GetState() != EDITOR::EditorPlayState::Stopped) {
                    ImGui::SameLine();
                    EDITOR::StatusText(
                        playSession.IsPaused()
                            ? "Paused"
                            : (previewRunning ? "Play Running" : "Play Status"),
                        previewRunning
                            ? EDITOR::EditorStatusTone::Ready
                            : (playSession.GetState() == EDITOR::EditorPlayState::Failed
                                ? EDITOR::EditorStatusTone::Error
                                : EDITOR::EditorStatusTone::Normal));
                    if (ImGui::IsItemHovered() &&
                        !playSession.GetStatusMessage().empty()) {
                        ImGui::SetTooltip(
                            "%s",
                            playSession.GetStatusMessage().c_str());
                    }
                }
                if (!previewRunning) {
                    ImGui::SameLine(0.0f, 8.0f);
                    const EDITOR::ViewportAuthoringToolbarResult authoringToolbar =
                        EDITOR::DrawViewportAuthoringToolbar(
                            context_,
                            selectedObjectIsCamera);
                    if (authoringToolbar.settingsRequested) {
                        ImGui::OpenPopup("ViewportToolSettingsPopup");
                    }
                    if (ImGui::BeginPopup("ViewportToolSettingsPopup")) {
                        ImGui::SeparatorText("Viewport");
                        DrawViewportDebugOptions(
                            context_.overlays,
                            context_.viewportPerformance);
                        ImGui::SeparatorText("Gizmos");
                        ImGui::Checkbox(
                            "Only Selected Object",
                            &context_.gizmos.showOnlySelectedObject);
                        const ComponentGizmoRegistry& gizmoRegistry =
                            scene.GetComponentGizmoRegistry();
                        for (const ComponentGizmoProvider& provider :
                            gizmoRegistry.GetProviders()) {
                            bool visible = context_.gizmos.IsProviderVisible(
                                provider.providerId,
                                provider.defaultVisible);
                            if (ImGui::Checkbox(
                                    provider.displayName.c_str(),
                                    &visible)) {
                                context_.gizmos.SetProviderVisible(
                                    provider.providerId,
                                    visible);
                            }
                        }
                        ImGui::SeparatorText("Snap");
                        ImGui::DragFloat3(
                            "Translate",
                            &context_.transformGizmo.translateSnap.x,
                            0.05f,
                            0.001f,
                            100.0f);
                        ImGui::DragFloat(
                            "Rotate",
                            &context_.transformGizmo.rotateSnapDeg,
                            0.5f,
                            0.1f,
                            180.0f,
                            "%.1f deg");
                        ImGui::DragFloat(
                            "Scale",
                            &context_.transformGizmo.scaleSnap,
                            0.01f,
                            0.001f,
                            10.0f);
                        ImGui::EndPopup();
                    }

                    ImGui::SameLine();
                    RENDER3D::RenderQualitySettings qualitySettings =
                        RENDER3D::GetRenderQualitySettings();
                    EDITOR::ToolbarLabel("Render");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(118.0f);
                    if (ImGui::BeginCombo(
                        "##GameViewRenderResolution",
                        RENDER3D::RenderResolutionPresetLabel(qualitySettings.sceneResolution),
                        ImGuiComboFlags_NoArrowButton)) {
                        constexpr RENDER3D::RenderResolutionPreset presets[] = {
                            RENDER3D::RenderResolutionPreset::Viewport,
                            RENDER3D::RenderResolutionPreset::P720,
                            RENDER3D::RenderResolutionPreset::P1080,
                            RENDER3D::RenderResolutionPreset::P1440,
                            RENDER3D::RenderResolutionPreset::P2160,
                        };
                        for (RENDER3D::RenderResolutionPreset preset : presets) {
                            const bool selected = qualitySettings.sceneResolution == preset;
                            if (ImGui::Selectable(RENDER3D::RenderResolutionPresetLabel(preset), selected)) {
                                qualitySettings.sceneResolution = preset;
                                RENDER3D::SetRenderQualitySettings(qualitySettings);
                                renderQualitySavePending_ = true;
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::SameLine();
                    EDITOR::ToolbarLabel("View");
                    ImGui::SameLine();
                    DrawRenderDebugViewCombo("##GameViewDebug", context_.viewportDebug, 144.0f);
                    if (context_.viewportDebug.renderView != RenderDebugView::None) {
                        ImGui::SameLine();
                        EDITOR::StatusText(
                            "Diagnostic output",
                            EDITOR::EditorStatusTone::Ready);
                    }
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImVec2 imageSize = ImGui::GetContentRegionAvail();
        imageSize.x = (std::max)(imageSize.x, 1.0f);
        imageSize.y = (std::max)(imageSize.y, 1.0f);

        const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
        const bool gameViewFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (!playSession.IsRunning()) {
            HandleTransformGizmoShortcuts(
                context_.transformGizmo,
                gameViewFocused);
        }
        if (!playSession.IsRunning()) {
            const bool standardCommandExecuted =
                commandRouter_.ProcessViewportShortcuts(
                    gameViewFocused);
            if (!standardCommandExecuted &&
                EDITOR::CanUseEditorShortcut(
                    EDITOR::EditorShortcutScope::Viewport,
                    gameViewFocused) &&
                context_.selection.selectedObject != nullptr &&
                ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
                sceneInspectorPanel_.RequestRename(
                    *context_.selection.selectedObject);
            }
        }
        EDITOR::SetGameViewportInputRect(imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y, gameViewFocused);
        SERVICES::SetEditorGameViewportSize(
            static_cast<int>(imageSize.x + 0.5f),
            static_cast<int>(imageSize.y + 0.5f),
            true);
        auto drawTransformGizmoOverlay = [&]() -> bool {
            bool gizmoCapture = false;
            const EDITOR::EditorViewportRect viewportRect{
                imageOrigin.x,
                imageOrigin.y,
                imageSize.x,
                imageSize.y
            };
            if (!context_.overlays.editReflectionProbe &&
                !ImGui::IsPopupOpen("SceneViewportContextMenu") &&
                context_.selection.selectedObject != nullptr &&
                !EDITOR::IsObjectEditorLocked(
                    scene.GetSceneDocument(),
                    context_.selection.GetActiveObjectId())) {
                EditorTransformGizmoState gizmoState = context_.transformGizmo;
                if (ImGui::IsKeyDown(ImGuiKey_ModCtrl)) {
                    gizmoState.snapEnabled = true;
                }
                const EDITOR::SceneSelectionTransformResult transformResult =
                    EDITOR::DrawSceneSelectionTransformGizmo(
                        scene,
                        context_.selection,
                        transformGizmo_,
                        gizmoState,
                        viewportRect);
                gizmoCapture = transformResult.gizmo.interacting;

                const bool multipleObjects =
                    context_.selection.GetSelectedObjectCount() > 1u;
                const char* historyLabel = multipleObjects
                    ? "Move Objects"
                    : "Move Object";
                switch (gizmoState.operation) {
                case EditorTransformGizmoOperation::Rotate:
                    historyLabel = multipleObjects
                        ? "Rotate Objects"
                        : "Rotate Object";
                    break;
                case EditorTransformGizmoOperation::Scale:
                    historyLabel = multipleObjects
                        ? "Scale Objects"
                        : "Scale Object";
                    break;
                case EditorTransformGizmoOperation::Translate:
                default:
                    break;
                }
                viewportTransformHistory_.ObserveBeforeApply(
                    scene.GetSceneDocument(),
                    context_.selection.selectedObject->GetDocumentId(),
                    historyLabel,
                    transformResult.gizmo.manipulating ||
                        transformResult.gizmo.changed,
                    context_.sceneDirty ||
                        scene.HasUnsavedSceneChanges());

                if (!transformResult.changedObjectIds.empty()) {
                    EDITOR::CommitSceneSelectionTransforms(
                        scene,
                        transformResult);
                    context_.sceneDirty = true;
                    scene.SetUnsavedSceneChanges(true);
                    viewportTransformHistory_.MarkChanged();
                }
            }

            if (context_.overlays.editReflectionProbe &&
                context_.overlays.showReflectionProbe) {
                ReflectionProbeSettings& probe = scene.GetSceneEnvironment().reflectionProbe;
                const ReflectionProbeEditTarget target = context_.overlays.reflectionProbeEditTarget;
                if (CanEditReflectionProbeTarget(probe, target)) {
                    EditorTransformGizmoState gizmoState =
                        BuildReflectionProbeGizmoState(context_.transformGizmo, target);
                    if (ImGui::IsKeyDown(ImGuiKey_ModCtrl)) {
                        gizmoState.snapEnabled = true;
                    }

                    const TransformData targetTransform =
                        BuildReflectionProbeEditTransform(probe, target);
                    const EDITOR::EditorTransformGizmoResult gizmoResult =
                        transformGizmo_.DrawTransform(
                            targetTransform,
                            scene.GetCamera(),
                            gizmoState,
                            viewportRect);
                    gizmoCapture = gizmoCapture || gizmoResult.interacting;

                    if (gizmoResult.changed) {
                        ApplyReflectionProbeEditTransform(
                            probe,
                            target,
                            gizmoResult,
                            gizmoState.operation);
                        scene.GetSceneDocument().environment = scene.GetSceneEnvironment();
                        scene.SetUnsavedSceneChanges(true);
                        scene.SyncReflectionProbeRuntimeFromAuthoring();
                        context_.sceneDirty = true;
                    }
                }
            }

            EDITOR::SetGameViewportGizmoCapture(gizmoCapture);
            return gizmoCapture;
        };

        const EDITOR::SceneViewportRect sceneViewportRect{
            imageOrigin.x,
            imageOrigin.y,
            imageSize.x,
            imageSize.y
        };
        bool viewportImageHovered = false;
        bool gizmoCapture = false;
        const bool ready = POST::PostSystem::IsEditorViewportReady();
        const D3D12_GPU_DESCRIPTOR_HANDLE viewportSrv = POST::PostSystem::GetEditorViewportSrv();
        if (ready && viewportSrv.ptr != 0) {
            const ImTextureID textureId = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(viewportSrv.ptr));
            ImGui::Image(textureId, imageSize);
            viewportImageHovered = ImGui::IsItemHovered();
            // Drop target 縺ｯ viewport image 縺ｮ逶ｴ蠕後↓逋ｻ骭ｲ縺励∝ｾ檎ｶ壹・ overlay item 縺ｫ螂ｪ繧上○縺ｪ縺・・
            if (!playSession.IsRunning()) {
                HandleGameViewportAssetDrop(scene);
                gizmoCapture = drawTransformGizmoOverlay();
            }
        } else {
            const ImVec2 max{ imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y };
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(imageOrigin, max, IM_COL32(8, 10, 13, 255));
            drawList->AddRect(imageOrigin, max, IM_COL32(80, 108, 124, 160), 4.0f, 0, 1.0f);
            drawList->AddText(ImVec2(imageOrigin.x + 16.0f, imageOrigin.y + 16.0f), IM_COL32(190, 205, 215, 255), "Waiting for editor viewport texture");
            ImGui::Dummy(imageSize);
            viewportImageHovered = ImGui::IsItemHovered();
            // Dummy 縺・viewport 蜈ｨ菴薙・ hit rect 縺ｫ縺ｪ繧九◆繧√｛verlay 謠冗判蜑阪↓ drop target 縺ｫ縺吶ｋ縲・
            if (!playSession.IsRunning()) {
                HandleGameViewportAssetDrop(scene);
                gizmoCapture = drawTransformGizmoOverlay();
            }
        }

        const INPUT::MouseCaptureRegion embeddedInputRegion{
            static_cast<int32_t>(std::lround(imageOrigin.x)),
            static_cast<int32_t>(std::lround(imageOrigin.y)),
            static_cast<int32_t>(std::lround(imageOrigin.x + imageSize.x)),
            static_cast<int32_t>(std::lround(imageOrigin.y + imageSize.y)),
        };
        if (playSession.IsEmbeddedRunning()) {
            if (playSession.HasEmbeddedInput()) {
                if (!gameViewFocused ||
                    ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                    playSession.ReleaseEmbeddedInput();
                } else {
                    playSession.UpdateEmbeddedInputRegion(
                        embeddedInputRegion);
                }
            } else if (!playSession.IsPaused() &&
                viewportImageHovered &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                playSession.CaptureEmbeddedInput(
                    embeddedInputRegion);
            }
        }

        const bool authoringInteractionEnabled =
            !playSession.IsRunning() &&
            !context_.overlays.editReflectionProbe;
        const EDITOR::SceneViewportRect blockedViewportRegion{
            -1.0f,
            -1.0f,
            0.0f,
            0.0f
        };
        const EDITOR::SceneViewportInteractionResult interaction =
            viewportSelectionService_.UpdateInput(
                scene.GetCamera(),
                sceneViewportRect,
                blockedViewportRegion,
                authoringInteractionEnabled,
                viewportImageHovered,
                gizmoCapture);
        const bool preserveContextSelection =
            interaction.openContextMenu &&
            interaction.selections.size() == 1u &&
            context_.selection.IsObjectSelected(
                interaction.selections.front());
        if (interaction.selectionChanged &&
            !preserveContextSelection) {
            SelectViewportObjects(
                scene,
                interaction.selections,
                interaction.selectionMode);
        }
        if (interaction.openContextMenu) {
            ImGui::OpenPopup("SceneViewportContextMenu");
        }

        if (!playSession.IsRunning()) {
            for (SceneObjectId objectId :
                context_.selection.GetSelectedObjectIds()) {
                viewportSelectionService_.DrawSelectionOutline(
                    scene.GetCamera(),
                    sceneViewportRect,
                    objectId,
                    ImGui::GetWindowDrawList());
            }
            viewportSelectionService_.DrawMarquee(
                ImGui::GetWindowDrawList());
            DrawReflectionProbeLabels(
                scene,
                context_.overlays,
                imageOrigin,
                imageSize);
            DrawLightOverlayIcons(
                scene,
                context_.overlays,
                imageOrigin,
                imageSize);
            DrawViewportContextMenu(scene);
        }
        viewportTransformHistory_.EndFrame(scene.GetSceneDocument());

        if (playSession.IsEmbeddedRunning()) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 max{
                imageOrigin.x + imageSize.x,
                imageOrigin.y + imageSize.y
            };
            if (playSession.HasEmbeddedInput()) {
                drawList->AddRect(
                    imageOrigin,
                    max,
                    IM_COL32(64, 205, 222, 230),
                    0.0f,
                    0,
                    2.0f);
            }
            const char* hint = playSession.IsPaused()
                ? "Paused"
                : (playSession.HasEmbeddedInput()
                    ? "Game input active  |  Esc releases mouse"
                    : "Click Game View to control");
            const ImVec2 textSize = ImGui::CalcTextSize(hint);
            const ImVec2 textMin{
                imageOrigin.x + 12.0f,
                imageOrigin.y + 12.0f
            };
            const ImVec2 textMax{
                textMin.x + textSize.x + 16.0f,
                textMin.y + textSize.y + 10.0f
            };
            drawList->AddRectFilled(
                textMin,
                textMax,
                IM_COL32(8, 12, 17, 205),
                4.0f);
            drawList->AddText(
                ImVec2(textMin.x + 8.0f, textMin.y + 5.0f),
                IM_COL32(220, 235, 240, 245),
                hint);
        } else if (!viewportDropMessage_.empty()) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 textPos{ imageOrigin.x + 14.0f, imageOrigin.y + imageSize.y - 28.0f };
            drawList->AddText(textPos, IM_COL32(210, 226, 236, 230), viewportDropMessage_.c_str());
        }

        ImGui::End();
        ImGui::PopStyleVar();
#else
        (void)scene;
        (void)playSession;
#endif
    }

    void DocumentSceneEditorController::SelectViewportObjects(
        DocumentSceneBase& scene,
        const std::vector<SceneObjectId>& objectIds,
        EditorObjectSelectionMode mode) {
#if defined(HIKARI_WITH_EDITOR)
        context_.selection.SelectObjectIds(
            scene.GetWorld(),
            objectIds,
            mode);
        scene.SetSelectedGizmoObjectId(
            context_.selection.selectedObject != nullptr
                ? context_.selection.selectedObject->GetDocumentId()
                : SceneObjectId{});
#else
        (void)scene;
        (void)objectIds;
        (void)mode;
#endif
    }

    void DocumentSceneEditorController::FocusSceneObjects(
        DocumentSceneBase& scene,
        const std::vector<SceneObjectId>& objectIds) {

        Bounds selectionBounds = BOUNDS::EmptyBounds();
        bool hasSelectionBounds = false;
        const RENDER3D::RUNTIME::SceneRenderCache& renderCache =
            RenderSubmissionSystem::GetSceneRenderCache();

        for (SceneObjectId objectId : objectIds) {
            const RENDER3D::RUNTIME::SceneRenderObject* renderObject =
                renderCache.Find(
                    RENDER3D::RUNTIME::SceneRenderObjectId{
                        objectId.value
                    });
            if (renderObject != nullptr &&
                BOUNDS::IsUsable(renderObject->desc.worldBounds)) {
                BOUNDS::Encapsulate(
                    selectionBounds,
                    renderObject->desc.worldBounds);
                hasSelectionBounds = true;
                continue;
            }

            const GameObject* object =
                scene.GetWorld().FindObject(objectId);
            if (object == nullptr) {
                continue;
            }
            const MATH::Mat4& worldMatrix =
                object->GetTransform().GetWorldMatrix();
            MATH::Vec3 position{
                worldMatrix.m[3][0],
                worldMatrix.m[3][1],
                worldMatrix.m[3][2]
            };
            MATH::Quat rotation{};
            MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
            (void)MATH::DecomposeTRS(
                worldMatrix,
                position,
                rotation,
                scale);
            const float radius = (std::max)({
                std::abs(scale.x),
                std::abs(scale.y),
                std::abs(scale.z),
                0.5f
            });
            const MATH::Vec3 extent{ radius, radius, radius };
            BOUNDS::Encapsulate(selectionBounds, position - extent);
            BOUNDS::Encapsulate(selectionBounds, position + extent);
            hasSelectionBounds = true;
        }

        if (!hasSelectionBounds ||
            !BOUNDS::IsUsable(selectionBounds)) {
            return;
        }

        const MATH::Vec3 center =
            (selectionBounds.min + selectionBounds.max) * 0.5f;
        const MATH::Vec3 extent =
            (selectionBounds.max - selectionBounds.min) * 0.5f;
        const float radius = (std::max)(
            std::sqrt(
                extent.x * extent.x +
                extent.y * extent.y +
                extent.z * extent.z),
            1.0f);

        DebugCameraController3D& debugCamera =
            scene.GetDebugCamera();
        const float yaw = debugCamera.GetYaw();
        const float pitch = debugCamera.GetPitch();
        const float cp = std::cos(pitch);
        const MATH::Vec3 forward = MATH::Normalize({
            std::sin(yaw) * cp,
            std::sin(pitch),
            std::cos(yaw) * cp
        });
        const float distance = (std::clamp)(
            radius * 2.5f,
            3.0f,
            5000.0f);
        const MATH::Vec3 position =
            center - forward * distance;
        debugCamera.SetPosition(position);
        scene.GetCamera().SetLookAt(position, center);
    }

    void DocumentSceneEditorController::DrawViewportContextMenu(
        DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::BeginPopup("SceneViewportContextMenu")) {
            return;
        }

        const SceneObjectId contextTarget =
            viewportSelectionService_.GetContextTarget();
        GameObject* target = contextTarget.value != 0u
            ? scene.GetWorld().FindObject(contextTarget)
            : nullptr;
        if (target != nullptr) {
            sceneInspectorPanel_.DrawObjectContextMenu(
                scene,
                context_,
                selectionSync_,
                sceneObjectCommands_,
                *target);
        } else {
            ImGui::TextDisabled("Create in Scene");
            sceneCreationPanel_.DrawCreationMenu(
                scene,
                context_,
                selectionSync_,
                sceneObjectCommands_);
        }
        ImGui::EndPopup();
#else
        (void)scene;
#endif
    }

    void DocumentSceneEditorController::HandleGameViewportAssetDrop(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        EDITOR::DroppedAssetPayload payload{};
        if (!EDITOR::AcceptAssetDrop(scene.GetAssetDatabase(), payload) || !payload.record) {
            return;
        }

        switch (payload.record->type) {
        case AssetType::Model: {
            EDITOR::CreateObjectRequest request{};
            request.name = payload.record->displayName.empty()
                ? payload.record->sourcePath.stem().string()
                : payload.record->displayName;
            request.position = EDITOR::ComputeObjectPlacementInView(
                scene.GetCamera(),
                1.0f);

            GameObject* object = EDITOR::CreateModelObject(scene, payload.guid, request);
            context_.selection.SelectObject(scene.GetWorld(), object);
            context_.sceneDirty = true;
            scene.SetUnsavedSceneChanges(true);
            selectionSync_.SyncNextSceneObjectId(scene, context_.nextSceneObjectId);
            viewportDropMessage_ = object
                ? "Model object created: " + object->GetName()
                : "Model drop failed";
            break;
        }
        case AssetType::Scene:
            pendingSceneOpenGuid_ = payload.guid;
            if (context_.sceneDirty || scene.HasUnsavedSceneChanges()) {
                ImGui::OpenPopup("Unsaved Scene Changes");
            } else {
                OpenSceneAssetFromEditor(scene, payload.guid);
            }
            break;
        case AssetType::VfxEffect:
            viewportDropMessage_ = "VFX drop target not implemented yet";
            break;
        case AssetType::Texture:
            viewportDropMessage_ = "Texture viewport drop is not implemented yet";
            break;
        case AssetType::Material:
            if (!context_.selection.selectedObject) {
                viewportDropMessage_ = "Select a model object first, then drop Material";
                break;
            }
            if (auto* modelComponent = context_.selection.selectedObject->GetComponent<ModelComponent>()) {
                modelComponent->SetMaterialOverride(0, payload.guid);
                scene.RebuildMaterialOverrides();
                if (SceneObjectData* documentObject =
                    selectionSync_.FindDocumentObjectByRuntime(scene, context_.selection.selectedObject)) {
                    for (SceneComponentData& component : documentObject->components) {
                        if (component.type != "ModelComponent") {
                            continue;
                        }
                        component.properties["materialOverrides"] = nlohmann::json::array({
                            {
                                { "slot", 0u },
                                { "materialAssetGuid", payload.guid.value }
                            }
                        });
                        break;
                    }
                }
                context_.sceneDirty = true;
                scene.SetUnsavedSceneChanges(true);
                viewportDropMessage_ = "Material assigned: " + payload.record->displayName;
            } else {
                viewportDropMessage_ = "Selected object has no ModelComponent";
            }
            break;
        default:
            viewportDropMessage_ = "This asset type cannot be dropped into Game View yet";
            break;
        }
#else
        (void)scene;
#endif
    }

    void DocumentSceneEditorController::DrawPendingSceneOpenModal(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        bool openModal = true;
        if (!ImGui::BeginPopupModal("Unsaved Scene Changes", &openModal, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        ImGui::TextUnformatted("Current scene has unsaved changes.");
        ImGui::TextDisabled("Save before opening the dropped Scene asset?");
        ImGui::Separator();

        if (ImGui::Button("Save", ImVec2(96.0f, 0.0f))) {
            if (scene.SaveCurrentSceneDocument()) {
                context_.sceneDirty = false;
                OpenSceneAssetFromEditor(scene, pendingSceneOpenGuid_);
                pendingSceneOpenGuid_ = {};
                ImGui::CloseCurrentPopup();
            } else {
                viewportDropMessage_ = "Save failed; scene was not opened";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(96.0f, 0.0f))) {
            context_.sceneDirty = false;
            scene.SetUnsavedSceneChanges(false);
            OpenSceneAssetFromEditor(scene, pendingSceneOpenGuid_);
            pendingSceneOpenGuid_ = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f))) {
            pendingSceneOpenGuid_ = {};
            viewportDropMessage_ = "Scene open cancelled";
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
#else
        (void)scene;
#endif
    }

    bool DocumentSceneEditorController::OpenSceneAssetFromEditor(DocumentSceneBase& scene, const AssetGuid& sceneGuid) {
        if (!sceneGuid.IsValid()) {
            viewportDropMessage_ = "Invalid scene asset";
            return false;
        }

        const bool opened = scene.OpenSceneAssetNow(sceneGuid);
        if (!opened) {
            viewportDropMessage_ = "Scene asset open failed";
            HIKARI_LOG_WARN("[SceneAsset] open scene failed: " + sceneGuid.value);
            return false;
        }

        context_.selection.ClearObjects();
        context_.selection.selectedAsset = nullptr;
        context_.selection.selectedAssetGuid = sceneGuid.value;
        context_.selection.selectedAssetPath.clear();
        context_.sceneDirty = false;
        scene.SetUnsavedSceneChanges(false);
        context_.sceneNameEditBuffer = scene.GetSceneDocument().sceneName;
        context_.saveAsNameBuffer = scene.GetSceneDocument().sceneName;
        selectionSync_.SyncNextSceneObjectId(scene, context_.nextSceneObjectId);
        viewportDropMessage_ = "Scene opened: " + scene.GetSceneDocument().sceneName;
        HIKARI_LOG_INFO("[SceneAsset] open scene: " + sceneGuid.value);
        return true;
    }

    void DocumentSceneEditorController::DrawSceneWorkspaceWindow(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Scene Workspace")) {
            ImGui::End();
            return;
        }

        EDITOR::PanelTitle("Objects");
        ImGui::SameLine();
        const float createButtonWidth = ImGui::GetFrameHeight();
        ImGui::SetCursorPosX((std::max)(
            ImGui::GetCursorPosX(),
            ImGui::GetWindowContentRegionMax().x - createButtonWidth));
        if (EDITOR::IconButton(
                EDITOR::EditorGlyph::Add,
                "CreateSceneObject",
                EDITOR::EditorButtonTone::Quiet,
                ImVec2(createButtonWidth, createButtonWidth),
                "Create Object")) {
            ImGui::OpenPopup("SceneHierarchyCreateMenu");
        }
        if (ImGui::BeginPopup("SceneHierarchyCreateMenu")) {
            sceneCreationPanel_.DrawCreationMenu(
                scene,
                context_,
                selectionSync_,
                sceneObjectCommands_);
            ImGui::EndPopup();
        }

        hierarchyPanel_.DrawContents(
            scene.GetWorld(),
            context_.selection,
            [&](GameObject& object) {
                sceneInspectorPanel_.DrawObjectContextMenu(
                    scene,
                    context_,
                    selectionSync_,
                    sceneObjectCommands_,
                    object);
            },
            [&](const GameObject& object) {
                return EDITOR::IsObjectEditorLocked(
                    scene.GetSceneDocument(),
                    object.GetDocumentId());
            });

        if (ImGui::BeginPopupContextWindow(
                "SceneHierarchyEmptyContext",
                ImGuiPopupFlags_MouseButtonRight |
                    ImGuiPopupFlags_NoOpenOverItems)) {
            sceneCreationPanel_.DrawCreationMenu(
                scene,
                context_,
                selectionSync_,
                sceneObjectCommands_);
            ImGui::EndPopup();
        }

        ImGui::End();
#else
        (void)scene;
#endif
    }

    void DocumentSceneEditorController::DrawInspectorWindow(
        DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        sceneInspectorPanel_.Draw(
            scene,
            context_,
            selectionSync_,
            sceneObjectCommands_,
            &context_.windows.authoring.showInspector);
#else
        (void)scene;
#endif
    }

    void DocumentSceneEditorController::DrawDebugWorkspaceWindow(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Diagnostics")) {
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("DebugWorkspaceTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll)) {
            if (ImGui::BeginTabItem("Stats")) {
                ImGui::SeparatorText("Frame Overview");
                statsPanel_.DrawContents(scene.GetSceneName(), scene.GetWorld(), scene.GetModelManager(), context_.selection, scene.GetCamera());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Time")) {
                ImGui::SeparatorText("Timeline");
                timePanel_.DrawContents();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Runtime Refresh")) {
                const RuntimeResourceRefreshReport& report = context_.lastRuntimeRefreshReport;
                ImGui::SeparatorText("Runtime Resource Refresh");
                ImGui::Text("Texture invalidated: %d", report.textureInvalidatedCount);
                ImGui::Text("Sky refreshed: %d", report.skyInvalidatedCount);
                ImGui::Text("Model reloaded: %d", report.modelReloadedCount);
                ImGui::Text("Components rebound: %d", report.modelReboundComponentCount);
                ImGui::Text("Failed: %d", report.failedCount);
                ImGui::SeparatorText("Messages");
                if (report.messages.empty()) {
                    ImGui::TextDisabled("No runtime refresh has run yet.");
                } else {
                    for (const std::string& message : report.messages) {
                        ImGui::BulletText("%s", message.c_str());
                    }
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
#else
        (void)scene;
#endif
    }

    void DocumentSceneEditorController::DrawDebugViewWindow(DocumentSceneBase& scene, bool& open) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Debug View", &open)) {
            ImGui::End();
            return;
        }

        ImGui::SeparatorText("Global View");
        DrawRenderDebugViewCombo("##DebugViewMode", context_.viewportDebug, 220.0f);
        ImGui::SameLine();
        if (EDITOR::ActionButton(
                "Lit",
                "DebugViewLit",
                context_.viewportDebug.renderView == RenderDebugView::None
                    ? EDITOR::EditorButtonTone::Primary
                    : EDITOR::EditorButtonTone::Neutral)) {
            context_.viewportDebug.renderView = RenderDebugView::None;
        }
        ImGui::Checkbox("Show Legend", &context_.viewportDebug.showLegend);
        ImGui::Checkbox(
            "Freeze Culling Camera",
            &context_.viewportDebug.freezeCullingCamera);

        if (context_.viewportDebug.renderView == RenderDebugView::None) {
            ImGui::TextDisabled("The viewport is using the normal shaded output.");
        } else {
            EDITOR::StatusText(
                ToString(context_.viewportDebug.renderView),
                EDITOR::EditorStatusTone::Ready);
            ImGui::TextDisabled(
                IsGeometryRenderDebugView(context_.viewportDebug.renderView)
                    ? "Meshlet modes are generated by the active GPU meshlet path."
                    : IsScreenSpaceRenderDebugView(context_.viewportDebug.renderView)
                    ? "Screen-space modes visualize resolved frame resources."
                    : "Shading modes reuse the final material/light pixel shader.");
            ImGui::TextDisabled("Temporal upscaling and post effects are bypassed for diagnostic output.");
        }

        ImGui::SeparatorText("Shading");
        if (ImGui::BeginTable("DebugViewShadingModes", 3, ImGuiTableFlags_SizingStretchSame)) {
            const RenderDebugView shadingModes[] = {
                RenderDebugView::BaseColor,
                RenderDebugView::Normal,
                RenderDebugView::Tangent,
                RenderDebugView::LightingOnly,
                RenderDebugView::Roughness,
                RenderDebugView::Metallic,
                RenderDebugView::Occlusion,
                RenderDebugView::Shadow,
                RenderDebugView::NdotL,
                RenderDebugView::Emissive,
                RenderDebugView::SceneDepth,
            };
            for (RenderDebugView mode : shadingModes) {
                ImGui::TableNextColumn();
                const bool selected = context_.viewportDebug.renderView == mode;
                if (EDITOR::ModeButton(ToString(mode), ToString(mode), selected)) {
                    context_.viewportDebug.renderView = mode;
                }
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Screen Space");
        if (ImGui::BeginTable("DebugViewScreenSpaceModes", 3, ImGuiTableFlags_SizingStretchSame)) {
            const RenderDebugView screenSpaceModes[] = {
                RenderDebugView::SceneColor,
                RenderDebugView::MotionVectors,
                RenderDebugView::TemporalHistoryWeight,
                RenderDebugView::TemporalDepthRejection,
                RenderDebugView::TemporalReactiveMask,
                RenderDebugView::TemporalTransparencyMask,
                RenderDebugView::TemporalDisocclusion,
                RenderDebugView::TemporalInvalidDepthMotion,
            };
            for (RenderDebugView mode : screenSpaceModes) {
                ImGui::TableNextColumn();
                const bool selected = context_.viewportDebug.renderView == mode;
                if (EDITOR::ModeButton(ToString(mode), ToString(mode), selected)) {
                    context_.viewportDebug.renderView = mode;
                }
            }
            ImGui::EndTable();
        }
        ImGui::SeparatorText("Volumetric");
        if (ImGui::BeginTable("DebugViewVolumetricModes", 3, ImGuiTableFlags_SizingStretchSame)) {
            const RenderDebugView volumetricModes[] = {
                RenderDebugView::VolumetricScattering,
                RenderDebugView::VolumetricTransmittance,
                RenderDebugView::VolumetricDepthSlice,
            };
            for (RenderDebugView mode : volumetricModes) {
                ImGui::TableNextColumn();
                const bool selected = context_.viewportDebug.renderView == mode;
                if (EDITOR::ModeButton(ToString(mode), ToString(mode), selected)) {
                    context_.viewportDebug.renderView = mode;
                }
            }
            ImGui::EndTable();
        }
        ImGui::SeparatorText("Meshlet");
        if (ImGui::BeginTable("DebugViewMeshletModes", 3, ImGuiTableFlags_SizingStretchSame)) {
            const RenderDebugView meshletModes[] = {
                RenderDebugView::MeshletId,
                RenderDebugView::ClusterId,
                RenderDebugView::SurfaceId,
                RenderDebugView::LodLevel,
                RenderDebugView::LodHeat,
                RenderDebugView::DrawBucket,
            };
            for (RenderDebugView mode : meshletModes) {
                ImGui::TableNextColumn();
                const bool selected = context_.viewportDebug.renderView == mode;
                if (EDITOR::ModeButton(ToString(mode), ToString(mode), selected)) {
                    context_.viewportDebug.renderView = mode;
                }
            }
            ImGui::EndTable();
        }

        if (context_.viewportDebug.showLegend) {
            ImGui::SeparatorText("LOD Legend");
            const ImVec4 lodColors[] = {
                { 0.95f, 0.20f, 0.18f, 1.0f },
                { 0.95f, 0.70f, 0.16f, 1.0f },
                { 0.38f, 0.86f, 0.30f, 1.0f },
                { 0.18f, 0.72f, 0.96f, 1.0f },
                { 0.55f, 0.38f, 0.95f, 1.0f },
            };
            const char* labels[] = { "LOD0", "LOD1", "LOD2", "LOD3", "LOD4+" };
            for (int i = 0; i < 5; ++i) {
                ImGui::ColorButton(labels[i], lodColors[i], ImGuiColorEditFlags_NoTooltip, ImVec2(18.0f, 18.0f));
                ImGui::SameLine();
                ImGui::TextUnformatted(labels[i]);
                if (i < 4) {
                    ImGui::SameLine();
                }
            }
        }

        if (ImGui::CollapsingHeader("Viewport Tools")) {
            DrawViewportDebugOptions(context_.overlays, context_.viewportPerformance);
        }
        if (ImGui::CollapsingHeader("Debug Camera")) {
            debugCameraPanel_.DrawContents(scene.GetDebugCamera());
        }

        ImGui::End();
#else
        (void)scene;
        (void)open;
#endif
    }

} // namespace HIKARI
