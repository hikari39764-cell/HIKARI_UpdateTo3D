#include "HIKARI_StatsPanel.h"
#include "HIKARI_EditorSelection.h"
#include "HIKARI_DxTexture.h"
#include "HIKARI_Services.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Core/HIKARI_ModelManager.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/HIKARI_World.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
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
        const MODELRENDERER::ModelRendererFrameStats& modelFrameStats = modelStats.frame;
        const MODELRENDERER::ModelRendererCacheStats& modelCacheStats = modelStats.cache;
        const RENDER3D::RUNTIME::SceneRenderCache::Stats& sceneRenderCacheStats =
            RenderSubmissionSystem::GetSceneRenderCacheStats();
        const MESHRENDERER::MeshRendererDebugStats& meshStats = MESHRENDERER::GetDebugStats();
        if (ImGui::TreeNodeEx("Render", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("ModelRenderer Frame: %s", MODELRENDERER::ToString(modelStats.frameKind));
            ImGui::Text("Submitted Model Items: %u", modelFrameStats.submittedModelItemCount);
            if (ImGui::TreeNodeEx("RenderModel Cache", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("Requests: %u", modelCacheStats.renderModelCacheRequestCount);
                ImGui::Text("Hits / Misses: %u / %u",
                    modelCacheStats.renderModelCacheHitCount,
                    modelCacheStats.renderModelCacheMissCount);
                ImGui::Text("Cached Models: %u", modelCacheStats.renderModelCachedModelCount);
                ImGui::Text("Cached Surfaces: %u", modelCacheStats.renderModelCachedSurfaceCount);
                ImGui::Text("Invalid Cached Models: %u", modelCacheStats.renderModelCacheInvalidCount);
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("RenderModel Frame", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("Valid / Invalid Requests: %u / %u",
                    modelFrameStats.renderModelValidRequestCount,
                    modelFrameStats.renderModelInvalidRequestCount);
                ImGui::Text("Requested Surfaces: %u", modelFrameStats.renderModelRequestedSurfaceCount);
                ImGui::Text("Structured Nodes Submitted / Culled: %u / %u",
                    modelFrameStats.structuredNodeSubmittedCount,
                    modelFrameStats.structuredNodeCulledCount);
                ImGui::Text("Structured Missing Bounds: %u", modelFrameStats.structuredCullBoundsMissingCount);
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Scene Render Cache", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("Objects: %u", sceneRenderCacheStats.renderObjectCount);
                ImGui::Text("Visible / Hidden: %u / %u",
                    sceneRenderCacheStats.visibleObjectCount,
                    sceneRenderCacheStats.hiddenObjectCount);
                ImGui::Text("Static / Dynamic: %u / %u",
                    sceneRenderCacheStats.staticObjectCount,
                    sceneRenderCacheStats.dynamicObjectCount);
                ImGui::Text("Dirty: %u", sceneRenderCacheStats.dirtyObjectCount);
                ImGui::Text("Inserted / Updated / Removed: %u / %u / %u",
                    sceneRenderCacheStats.insertedCount,
                    sceneRenderCacheStats.updatedCount,
                    sceneRenderCacheStats.removedCount);
                ImGui::Text("Invalid Desc: %u", sceneRenderCacheStats.invalidDescCount);
                ImGui::Text("RenderModel Valid / Invalid: %u / %u",
                    sceneRenderCacheStats.renderModelValidCount,
                    sceneRenderCacheStats.renderModelInvalidCount);
                ImGui::TreePop();
            }
            ImGui::Text("Static / Skinned Draw Items: %zu / %zu", meshStats.staticDrawItemCount, meshStats.skinnedDrawItemCount);
            ImGui::Text("Wire Draw Items / GPU Draws: %zu / %zu", meshStats.wireDrawItemCount, meshStats.wireGpuDrawCount);
            ImGui::Text("Skinned GPU Draws: %zu", meshStats.skinnedGpuDrawCount);
            ImGui::Text("PSO Cache Hit / Miss: %zu / %zu", meshStats.psoCacheHitCount, meshStats.psoCacheMissCount);
            ImGui::Text("Texture Cache Hit / Miss: %zu / %zu", meshStats.materialTextureCacheHitCount, meshStats.materialTextureCacheMissCount);
            ImGui::Text("NormalMapped Primitives: %zu", meshStats.normalMappedPrimitiveCount);
            ImGui::Text("NormalTexture Cache Hit / Miss: %zu / %zu", meshStats.normalTextureCacheHitCount, meshStats.normalTextureCacheMissCount);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("GPU Resources", ImGuiTreeNodeFlags_DefaultOpen)) {
            const UINT used = DXTEX::DxTextureManager::GetUsedDescriptorCount();
            const UINT freeCount = DXTEX::DxTextureManager::GetFreeDescriptorCount();
            const UINT maxCount = DXTEX::DxTextureManager::GetMaxDescriptorCount();

            ImGui::Text("Texture Descriptors: %u / %u", used, maxCount);
            ImGui::Text("Free Texture Descriptors: %u", freeCount);
            ImGui::Text("SRV Heap Capacity: %u", GFX::DESCRIPTOR::kSrvHeapCapacity);
            ImGui::Text("System SRV Reserved: %u", GFX::DESCRIPTOR::kSystemSrvReservedCount);
            ImGui::Text(
                "Deferred Releases Pending: %zu",
                SERVICES::gCore.GetPendingDeferredReleaseCount());
            ImGui::Text("SceneColor Ready: %s", POST::PostSystem::IsSceneColorReady() ? "Yes" : "No");
            ImGui::Text("SceneColor Size: %d x %d",
                POST::PostSystem::GetSceneColorWidth(),
                POST::PostSystem::GetSceneColorHeight());
            ImGui::Text("SceneColor For Water: %s", POST::PostSystem::IsSceneColorReady() ? "Available" : "Unavailable");
            ImGui::Text("Water Refraction: SceneColor t8 when renderPhase=SceneDepth");
            const IBL::IblEnvironmentData& iblData = IBL::GetEnvironmentData();
            ImGui::Text("IBL Valid: %s", iblData.valid ? "Yes" : "No");
            ImGui::Text("IBL Irradiance / Prefiltered / BRDF: %s / %s / %s",
                iblData.hasIrradiance ? "Yes" : "No",
                iblData.hasPrefiltered ? "Yes" : "No",
                iblData.hasBrdfLut ? "Yes" : "No");
            ImGui::Text("IBL Prefiltered Mips: %u", iblData.prefilteredMipCount);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("PIX")) {
            ImGui::Text("Compiled In: %s", GFX::PIX::IsCompiledIn() ? "Yes" : "No");
            ImGui::Text("Capturer Loaded: %s", GFX::PIX::IsCapturerLoaded() ? "Yes" : "No");
            bool eventMarkersEnabled = GFX::PIX::AreEventMarkersEnabled();
            if (ImGui::Checkbox("Event Markers", &eventMarkersEnabled)) {
                GFX::PIX::SetEventMarkersEnabled(eventMarkersEnabled);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Capture file generation remains available. Enable this only when you need PIX timeline markers.");
            }
            ImGui::TextWrapped("Status: %s", GFX::PIX::GetLastStatusMessage().c_str());
            if (GFX::PIX::HasLastCapture()) {
                ImGui::TextWrapped("Last Capture: %s", GFX::PIX::GetLastCapturePath().generic_string().c_str());
            }
            if (ImGui::Button("Capture Next Frame")) {
                GFX::PIX::CaptureNextFrames(1, true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Open Last") && GFX::PIX::HasLastCapture()) {
                GFX::PIX::OpenLastCaptureInPix();
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Animation / Skinning")) {
            ImGui::Text("Animated Local Builds: %u", modelFrameStats.animatedLocalBuildCount);
            ImGui::Text("Pose Updated / Reused: %u / %u", modelCacheStats.poseUpdatedCount, modelCacheStats.poseReusedCount);
            ImGui::Text("Joint Palette Builds: %u", modelFrameStats.jointPaletteBuildCount);
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
