#include "HIKARI_PortableObjectToolsPanel.h"

#if defined(HIKARI_ENABLE_IMGUI)
#include <algorithm>
#include <string>
#include <vector>

#include <json.hpp>

#include "imgui.h"

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "HIKARI_Services.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"
#endif

namespace HIKARI::RUNTIME_TOOLS {

#if defined(HIKARI_ENABLE_IMGUI)
    namespace {
        GameObject* FindObjectById(DocumentSceneBase& scene, SceneObjectId id) {
            for (const auto& object : scene.GetWorld().GetObjects()) {
                if (object && object->GetDocumentId() == id) {
                    return object.get();
                }
            }
            return nullptr;
        }

        GameObject* FirstObject(DocumentSceneBase& scene) {
            const auto& objects = scene.GetWorld().GetObjects();
            if (objects.empty() || !objects.front()) {
                return nullptr;
            }
            return objects.front().get();
        }

        std::string SceneLabel(const AssetRecord& record) {
            std::string label = record.displayName.empty()
                ? record.sourcePath.stem().string()
                : record.displayName;
            if (label.empty()) {
                label = record.guid.value;
            }
            return label;
        }

        void DrawSceneSwitcher(
            DocumentSceneBase& scene,
            SceneObjectId& selectedObjectId,
            std::string& lastSceneMessage) {

            ImGui::SeparatorText("Scenes");

            std::vector<const AssetRecord*> sceneRecords =
                scene.GetAssetDatabase().CollectByType(AssetType::Scene);
            std::sort(sceneRecords.begin(), sceneRecords.end(), [](const AssetRecord* lhs, const AssetRecord* rhs) {
                if (!lhs || !rhs) {
                    return lhs < rhs;
                }
                return SceneLabel(*lhs) < SceneLabel(*rhs);
            });

            const AssetGuid& currentGuid = scene.GetCurrentSceneAssetGuid();
            std::string preview = scene.GetCurrentSceneDisplayName();
            if (preview.empty()) {
                preview = currentGuid.IsValid() ? currentGuid.value : scene.GetSceneId();
            }

            bool changedScene = false;
            if (ImGui::BeginCombo("Scene", preview.c_str())) {
                for (const AssetRecord* record : sceneRecords) {
                if (!record || !record->guid.IsValid()) {
                    continue;
                }
                if (!SERVICES::IsRuntimeSceneGuidAllowed(record->guid.value)) {
                    continue;
                }

                std::string label = SceneLabel(*record);
                    label += "##";
                    label += record->guid.value;

                    const bool selected = record->guid == currentGuid;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        const AssetGuid targetGuid = record->guid;
                        const std::string targetLabel = SceneLabel(*record);
                        if (scene.RequestOpenSceneAsset(targetGuid)) {
                            selectedObjectId = {};
                            lastSceneMessage = "Opened scene: " + targetLabel;
                            changedScene = true;
                        } else {
                            lastSceneMessage = "Failed to open scene: " + targetLabel;
                        }
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                    if (changedScene) {
                        break;
                    }
                }
                ImGui::EndCombo();
            }

            if (!lastSceneMessage.empty()) {
                ImGui::TextWrapped("%s", lastSceneMessage.c_str());
            }
        }

        void DrawTransform(GameObject& object) {
            Transform3D& transform = object.Transform();
            ImGui::SeparatorText("Transform");
            ImGui::DragFloat3("Position", &transform.position.x, 0.05f);
            ImGui::DragFloat3("Scale", &transform.scale.x, 0.02f, 0.001f, 1000.0f);
            ImGui::Text("Rotation quat: %.3f, %.3f, %.3f, %.3f",
                transform.rotation.x,
                transform.rotation.y,
                transform.rotation.z,
                transform.rotation.w);
        }

        void DrawComponents(GameObject& object) {
            ImGui::SeparatorText("Components");
            const auto& components = object.GetComponents();
            if (components.empty()) {
                ImGui::TextDisabled("No components");
                return;
            }

            for (const auto& component : components) {
                if (!component) {
                    continue;
                }

                const std::string typeName(component->GetTypeName());
                if (ImGui::TreeNode(typeName.c_str())) {
                    component->RenderImGui();

                    nlohmann::json serialized = nlohmann::json::object();
                    component->Serialize(serialized);
                    ImGui::SeparatorText("Serialized");
                    ImGui::TextWrapped("%s", serialized.dump(2).c_str());

                    ImGui::TreePop();
                }
            }
        }
    }
#endif

    void PortableObjectToolsPanel::Draw(DocumentSceneBase& scene) {
#if defined(HIKARI_ENABLE_IMGUI)
        ImGui::SetNextWindowSize(ImVec2(560.0f, 620.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Object Tools")) {
            ImGui::End();
            return;
        }

        DrawSceneSwitcher(scene, selectedObjectId_, lastSceneMessage_);

        GameObject* selected = FindObjectById(scene, selectedObjectId_);
        if (!selected) {
            selected = FirstObject(scene);
            selectedObjectId_ = selected ? selected->GetDocumentId() : SceneObjectId{};
        }

        ImGui::Text("Scene: %s", scene.GetSceneId().c_str());
        ImGui::Separator();

        const float listWidth = 150.0f;
        if (ImGui::BeginChild("ObjectList", ImVec2(listWidth, 0.0f), true)) {
            for (const auto& object : scene.GetWorld().GetObjects()) {
                if (!object) {
                    continue;
                }
                const bool isSelected = object->GetDocumentId() == selectedObjectId_;
                std::string label = object->GetName();
                label += "##";
                label += std::to_string(object->GetDocumentId().value);
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    selectedObjectId_ = object->GetDocumentId();
                    selected = object.get();
                }
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        if (ImGui::BeginChild("ObjectDetails", ImVec2(0.0f, 0.0f), true)) {
            if (selected) {
                ImGui::Text("Object: %s", selected->GetName().c_str());
                ImGui::Text("Document ID: %llu", static_cast<unsigned long long>(selected->GetDocumentId().value));
                DrawTransform(*selected);
                DrawComponents(*selected);
            } else {
                ImGui::TextDisabled("No objects in scene");
            }
        }
        ImGui::EndChild();

        ImGui::End();
#else
        (void)scene;
#endif
    }

} // namespace HIKARI::RUNTIME_TOOLS
