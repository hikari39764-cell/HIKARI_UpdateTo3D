#include "Scene/Features/HIKARI_RenderingRuntimeFeature.h"

#include <array>
#include <memory>

#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_AnimationSystem.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

namespace HIKARI {
    namespace {
        ComponentTypePresentation RenderingPresentation(
            const char* displayName,
            const char* description) {

            return ComponentTypePresentation{
                displayName,
                "Rendering",
                description,
                "Rendering"
            };
        }

        class RenderingRuntimeFeature final : public IRuntimeFeature {
        public:
            std::string_view GetFeatureId() const noexcept override {
                return RuntimeFeatureIds::Rendering;
            }

            std::string_view GetDisplayName() const noexcept override {
                return "Rendering";
            }

            std::string_view GetDescription() const noexcept override {
                return "Model rendering and skeletal animation support.";
            }

            std::span<const std::string_view>
                GetComponentTypeNames() const noexcept override {
                static constexpr std::array<std::string_view, 3> names{
                    "ModelComponent",
                    "ProceduralMeshComponent",
                    "AnimatorComponent"
                };
                return names;
            }

            std::span<const std::string_view>
                GetSystemIds() const noexcept override {
                static constexpr std::array<std::string_view, 2> ids{
                    "ModelRenderSystem",
                    "AnimationSystem"
                };
                return ids;
            }

            bool Register(RuntimeFeatureContext& context) override {
                ComponentTypeInfo model{};
                model.typeName = "ModelComponent";
                model.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<ModelComponent>();
                };
                model.optionalComponents = {
                    "ProceduralMeshComponent",
                    "AnimatorComponent"
                };
                model.presentation = RenderingPresentation(
                    "Model",
                    "Controls rendering, materials, shadows, and Material FX.");

                ComponentTypeInfo procedural{};
                procedural.typeName = "ProceduralMeshComponent";
                procedural.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<ProceduralMeshComponent>();
                };
                procedural.requiredComponents = { "ModelComponent" };
                procedural.runtimeApplyPolicy =
                    ComponentRuntimeApplyPolicy::OnEditCommit;
                procedural.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties = {
                        { "kind", "Box" },
                        { "width", 1.0f },
                        { "height", 1.0f },
                        { "depth", 1.0f },
                        { "segmentsX", 10u },
                        { "segmentsY", 10u },
                        { "segmentsZ", 1u },
                        { "sphereSlices", 32u },
                        { "sphereStacks", 16u },
                        { "doubleSided", false },
                        { "generateTangents", true }
                    };
                };
                procedural.presentation = RenderingPresentation(
                    "Procedural Mesh",
                    "Generates editable plane, box, sphere, cylinder, or capsule geometry.");

                ComponentTypeInfo animator{};
                animator.typeName = "AnimatorComponent";
                animator.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<AnimatorComponent>();
                };
                animator.requiredComponents = { "ModelComponent" };
                animator.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties["clip"] = "";
                    properties["timeSec"] = 0.0f;
                    properties["speed"] = 1.0f;
                    properties["loop"] = true;
                    properties["autoPlay"] = true;
                    properties["playing"] = true;
                    properties["finished"] = false;
                };
                animator.presentation = RenderingPresentation(
                    "Animator",
                    "Plays skeletal animation clips on a model.");

                bool success = context.componentRegistry.Register(
                    std::move(model));
                success = context.componentRegistry.Register(
                    std::move(procedural)) && success;
                success = context.componentRegistry.Register(
                    std::move(animator)) && success;
                success = context.systemTypeRegistry.Register(SystemTypeInfo{
                    "ModelRenderSystem",
                    [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return std::make_unique<RenderSubmissionSystem>();
                    },
                    "Model Rendering",
                    "Rendering"
                }) && success;
                success = context.systemTypeRegistry.Register(SystemTypeInfo{
                    "AnimationSystem",
                    [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return std::make_unique<AnimationSystem>();
                    },
                    "Animation",
                    "Rendering"
                }) && success;
                success = context.componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "AnimatorComponent",
                        "AnimationSystem",
                        150
                    }) && success;
                return success;
            }

            void AppendDefaultSceneSystems(
                std::vector<SceneSystemData>& systems) const override {
                systems.push_back(SceneSystemData{
                    "ModelRenderSystem",
                    true,
                    100,
                    nlohmann::json::object()
                });
            }
        };
    }

    std::unique_ptr<IRuntimeFeature> CreateRenderingRuntimeFeature() {
        return std::make_unique<RenderingRuntimeFeature>();
    }

} // namespace HIKARI
