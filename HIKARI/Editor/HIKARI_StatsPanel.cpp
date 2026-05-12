#include "HIKARI_StatsPanel.h"
#include "HIKARI_EditorSelection.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Core/HIKARI_ModelManager.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Scene/HIKARI_World.h"
#if defined(_DEBUG)
#include "imgui.h"
#endif
#include "Scene/HIKARI_GameObject.h"
namespace HIKARI {

    void StatsPanel::Draw(const char* sceneName, const World& world, const ModelManager& modelManager, const EditorSelection& selection, const Camera3D& camera) const {
#if defined(_DEBUG)
        if (!ImGui::Begin("Data Monitor")) {
            ImGui::End();
            return;
        }

        DrawContents(sceneName, world, modelManager, selection, camera);

        ImGui::End();
#else
        (void)sceneName;
        (void)world;
        (void)modelManager;
        (void)selection;
        (void)camera;
#endif
    }

    void StatsPanel::DrawContents(const char* sceneName, const World& world, const ModelManager& modelManager, const EditorSelection& selection, const Camera3D& camera) const {
#if defined(_DEBUG)
        if (ImGui::TreeNodeEx("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Scene: %s", sceneName ? sceneName : "<none>");
            ImGui::Text("World Objects: %zu", world.GetObjects().size());
            ImGui::Text("Selected Object: %s", selection.selectedObject ? selection.selectedObject->GetName().c_str() : "<none>");
            ImGui::Text("Selected Asset: %s", selection.selectedAsset ? selection.selectedAsset->GetName().c_str() : "<none>");
            const MATH::Vec3 cameraPos = camera.GetPosition();
            ImGui::Text("Camera Pos: (%.2f, %.2f, %.2f)", cameraPos.x, cameraPos.y, cameraPos.z);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Assets", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Model Assets: %zu", modelManager.GetAssets().size());
            ImGui::Text("Loaded Models: %zu", modelManager.CountLoadedAssets());
            ImGui::Text("Failed Models: %zu", modelManager.CountFailedAssets());
            size_t texturedMaterialCount = 0;
            for (const auto& asset : modelManager.GetAssets()) {
                if (asset && asset->GetMaterial() && asset->GetMaterial()->HasBaseColorTexture()) {
                    ++texturedMaterialCount;
                }
            }
            ImGui::Text("Textured Materials: %zu", texturedMaterialCount);
            ImGui::TreePop();
        }

        const MODELRENDERER::ModelRendererDebugStats& modelStats = MODELRENDERER::GetDebugStats();
        const MESHRENDERER::MeshRendererDebugStats& meshStats = MESHRENDERER::GetDebugStats();
        if (ImGui::TreeNodeEx("Render", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Submitted Model Items: %zu", modelStats.submittedModelItemCount);
            ImGui::Text("Static / Skinned Draw Items: %zu / %zu", meshStats.staticDrawItemCount, meshStats.skinnedDrawItemCount);
            ImGui::Text("Wire Draw Items / GPU Draws: %zu / %zu", meshStats.wireDrawItemCount, meshStats.wireGpuDrawCount);
            ImGui::Text("Skinned GPU Draws: %zu", meshStats.skinnedGpuDrawCount);
            ImGui::Text("PSO Cache Hit / Miss: %zu / %zu", meshStats.psoCacheHitCount, meshStats.psoCacheMissCount);
            ImGui::Text("Texture Cache Hit / Miss: %zu / %zu", meshStats.materialTextureCacheHitCount, meshStats.materialTextureCacheMissCount);
            ImGui::Text("NormalMapped Primitives: %zu", meshStats.normalMappedPrimitiveCount);
            ImGui::Text("NormalTexture Cache Hit / Miss: %zu / %zu", meshStats.normalTextureCacheHitCount, meshStats.normalTextureCacheMissCount);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Animation / Skinning")) {
            ImGui::Text("Animated Local Builds: %zu", modelStats.animatedLocalBuildCount);
            ImGui::Text("Pose Updated / Reused: %zu / %zu", modelStats.poseUpdatedCount, modelStats.poseReusedCount);
            ImGui::Text("Joint Palette Builds: %zu", modelStats.jointPaletteBuildCount);
            ImGui::Text("Uploaded Joints: %zu", meshStats.uploadedJointCount);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Lighting")) {
            ImGui::Text("Directional Enabled: %s", meshStats.directionalEnabled ? "Yes" : "No");
            ImGui::Text("Directional Intensity: %.3f", meshStats.directionalIntensity);
            ImGui::Text("Ambient Intensity: %.3f", meshStats.ambientIntensity);
            ImGui::Text("Point Lights Total / Uploaded / Clamped: %zu / %zu / %zu",
                meshStats.pointLightTotalCount,
                meshStats.pointLightUploadedCount,
                meshStats.pointLightClampedCount);
            ImGui::TreePop();
        }
#else
        (void)sceneName;
        (void)world;
        (void)modelManager;
        (void)selection;
        (void)camera;
#endif
    }

} // namespace HIKARI
