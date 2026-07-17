#include "Scene/Features/HIKARI_CameraRuntimeFeature.h"

#include <array>
#include <memory>

#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_CameraFollowSystem.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

namespace HIKARI {
    namespace {
        ComponentTypePresentation CameraPresentation(
            const char* displayName,
            const char* description) {

            return ComponentTypePresentation{
                displayName,
                "Camera",
                description,
                "Camera"
            };
        }

        class CameraRuntimeFeature final : public IRuntimeFeature {
        public:
            std::string_view GetFeatureId() const noexcept override {
                return RuntimeFeatureIds::Camera;
            }

            std::string_view GetDisplayName() const noexcept override {
                return "Camera";
            }

            std::string_view GetDescription() const noexcept override {
                return "Scene cameras and gameplay camera-follow behavior.";
            }

            std::span<const std::string_view>
                GetComponentTypeNames() const noexcept override {
                static constexpr std::array<std::string_view, 2> names{
                    "CameraComponent",
                    "CameraFollowComponent"
                };
                return names;
            }

            std::span<const std::string_view>
                GetSystemIds() const noexcept override {
                static constexpr std::array<std::string_view, 1> ids{
                    "CameraFollowSystem"
                };
                return ids;
            }

            bool Register(RuntimeFeatureContext& context) override {
                if (context.services.gameplayCamera == nullptr ||
                    context.services.runtimeSceneCameraActive == nullptr) {
                    return false;
                }

                ComponentTypeInfo camera{};
                camera.typeName = "CameraComponent";
                camera.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<CameraComponent>();
                };
                camera.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["verticalFovDegrees"] = 60.0f;
                    properties["nearClip"] = 0.1f;
                    properties["farClip"] = 100.0f;
                };
                camera.presentation = CameraPresentation(
                    "Camera",
                    "Defines a renderable scene camera and its lens.");

                ComponentTypeInfo follow{};
                follow.typeName = "CameraFollowComponent";
                follow.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<CameraFollowComponent>();
                };
                follow.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["targetObjectId"] = 0;
                    properties["useOwnerAsFallbackTarget"] = true;
                    properties["offset"] = nlohmann::json::array({
                        0.0f, 5.5f, -7.5f
                    });
                    properties["lookAtOffset"] = nlohmann::json::array({
                        0.0f, 1.2f, 0.0f
                    });
                    properties["followSmooth"] = 10.0f;
                    properties["lookSmooth"] = 12.0f;
                };
                follow.presentation = CameraPresentation(
                    "Camera Follow",
                    "Drives the gameplay camera from a scene target.");

                Camera3D* gameplayCamera = context.services.gameplayCamera;
                bool* runtimeCameraActive =
                    context.services.runtimeSceneCameraActive;

                bool success = context.componentRegistry.Register(
                    std::move(camera));
                success = context.componentRegistry.Register(
                    std::move(follow)) && success;
                success = context.systemTypeRegistry.Register(SystemTypeInfo{
                    "CameraFollowSystem",
                    [gameplayCamera, runtimeCameraActive](
                        const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return std::make_unique<CameraFollowSystem>(
                            *gameplayCamera,
                            *runtimeCameraActive);
                    },
                    "Camera Follow",
                    "Camera"
                }) && success;
                success = context.componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "CameraFollowComponent",
                        "CameraFollowSystem",
                        190
                    }) && success;
                return success;
            }
        };
    }

    std::unique_ptr<IRuntimeFeature> CreateCameraRuntimeFeature() {
        return std::make_unique<CameraRuntimeFeature>();
    }

} // namespace HIKARI
