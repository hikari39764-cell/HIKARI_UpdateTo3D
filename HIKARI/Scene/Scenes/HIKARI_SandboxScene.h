#pragma once
#include "Editor/HIKARI_AssetBrowserPanel.h"
#include "Editor/HIKARI_DebugCameraPanel.h"
#include "Editor/HIKARI_DebugMenuBar.h"
#include "Editor/HIKARI_DebugWindowState.h"
#include "Editor/HIKARI_EditorSelection.h"
#include "Editor/HIKARI_HierarchyPanel.h"
#include "Editor/HIKARI_InspectorPanel.h"
#include "Editor/HIKARI_LightingPanel.h"
#include "Editor/HIKARI_StatsPanel.h"
#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_DebugCameraController3D.h"
#include "Render3D/HIKARI_ModelManager.h"
#include "Render3D/HIKARI_SceneLighting.h"
#include "Scene/HIKARI_IScene.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    class SandboxScene final : public IScene {
    public:
        void OnEnter() override;
        void OnExit() override;
        void Update(float dt) override;
        void Render() override;
        void RenderImGui() override;
        const char* GetSceneName() const override { return "SandboxScene"; }

    private:
        Camera3D camera_{};
        World world_{};
        ModelManager modelManager_{};
        EditorSelection selection_{};
        DebugCameraController3D debugCamera_{};
        SceneLighting lighting_{};

        DebugWindowState debugWindowState_{};
        DebugMenuBar debugMenuBar_{};
        HierarchyPanel hierarchyPanel_{};
        InspectorPanel inspectorPanel_{};
        AssetBrowserPanel assetBrowserPanel_{};
        StatsPanel statsPanel_{};
        LightingPanel lightingPanel_{};
        DebugCameraPanel debugCameraPanel_{};

        bool lightingEnabled_ = true;
        float spinAngle_ = 0.0f;
    };

} // namespace HIKARI
