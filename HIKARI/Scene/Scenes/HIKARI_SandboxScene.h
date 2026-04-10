#pragma once
#include "Editor/HIKARI_AssetBrowserPanel.h"
#include "Editor/HIKARI_EditorSelection.h"
#include "Editor/HIKARI_HierarchyPanel.h"
#include "Editor/HIKARI_InspectorPanel.h"
#include "Editor/HIKARI_StatsPanel.h"
#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_ModelManager.h"
#include "Scene/HIKARI_IScene.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_GameObject.h"

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

        HierarchyPanel hierarchyPanel_{};
        InspectorPanel inspectorPanel_{};
        AssetBrowserPanel assetBrowserPanel_{};
        StatsPanel statsPanel_{};

        float spinAngle_ = 0.0f;
    };

} // namespace HIKARI
