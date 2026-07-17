#include "Scene/Features/HIKARI_GameplayRuntimeFeature.h"

#include <array>
#include <memory>

#include "Scene/Components/HIKARI_PlayerControllerComponent.h"
#include "Scene/Components/HIKARI_PlayerInputComponent.h"
#include "Scene/Components/HIKARI_SpawnPointComponent.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_PlayerMovementSystem.h"
#include "Scene/HIKARI_PlayerInputSystem.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

namespace HIKARI {
    namespace {
        ComponentTypePresentation GameplayPresentation(
            const char* displayName,
            const char* description) {

            return ComponentTypePresentation{
                displayName,
                "Gameplay",
                description,
                "GameplayBasic"
            };
        }

        class GameplayRuntimeFeature final : public IRuntimeFeature {
        public:
            std::string_view GetFeatureId() const noexcept override {
                return RuntimeFeatureIds::GameplayBasic;
            }

            std::string_view GetDisplayName() const noexcept override {
                return "Gameplay Basic";
            }

            std::string_view GetDescription() const noexcept override {
                return "Character movement, animation-driven control, and spawn points.";
            }

            std::span<const std::string_view>
                GetRequiredFeatureIds() const noexcept override {
                static constexpr std::array<std::string_view, 1> ids{
                    RuntimeFeatureIds::Camera
                };
                return ids;
            }

            std::span<const std::string_view>
                GetComponentTypeNames() const noexcept override {
                static constexpr std::array<std::string_view, 3> names{
                    "PlayerInputComponent",
                    "PlayerControllerComponent",
                    "SpawnPointComponent"
                };
                return names;
            }

            std::span<const std::string_view>
                GetSystemIds() const noexcept override {
                static constexpr std::array<std::string_view, 2> ids{
                    "PlayerInputSystem",
                    "PlayerMovementSystem"
                };
                return ids;
            }

            bool Register(RuntimeFeatureContext& context) override {
                if (context.services.gameplayCamera == nullptr) {
                    return false;
                }

                ComponentTypeInfo controller{};
                controller.typeName = "PlayerControllerComponent";
                controller.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<PlayerControllerComponent>();
                };
                controller.requiredComponents = { "PlayerInputComponent" };
                controller.optionalComponents = {
                    "AnimatorComponent", "CameraFollowComponent" };
                controller.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["moveSpeed"] = 4.0f;
                    properties["acceleration"] = 60.0f;
                    properties["deceleration"] = 72.0f;
                    properties["turnSpeed"] = 12.0f;
                    properties["inputDeadZone"] = 0.08f;
                    properties["rotateToMove"] = true;
                    properties["cameraRelativeMovement"] = true;
                    properties["useBounds"] = true;
                    properties["bounds"] = {
                        { "minX", -12.0f },
                        { "maxX", 12.0f },
                        { "minZ", -12.0f },
                        { "maxZ", 12.0f }
                    };
                    properties["animationEnabled"] = true;
                    properties["autoSelectAnimationClips"] = true;
                    properties["idleClip"] = "";
                    properties["moveClip"] = "";
                };
                controller.presentation = GameplayPresentation(
                    "Player Controller",
                    "Provides camera-relative player movement and animation state.");

                ComponentTypeInfo playerInput{};
                playerInput.typeName = "PlayerInputComponent";
                playerInput.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<PlayerInputComponent>();
                };
                playerInput.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["userId"] = 0;
                    properties["moveAction"] = "Gameplay.Move";
                    properties["lookAction"] = "Gameplay.Look";
                    properties["jumpAction"] = "Gameplay.Jump";
                    properties["interactAction"] = "Gameplay.Interact";
                };
                playerInput.presentation = GameplayPresentation(
                    "Player Input",
                    "Maps project input actions into a per-player command.");

                ComponentTypeInfo spawnPoint{};
                spawnPoint.typeName = "SpawnPointComponent";
                spawnPoint.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<SpawnPointComponent>();
                };
                spawnPoint.initializeDefaults = [](
                    const SceneObjectData& object,
                    nlohmann::json& properties) {
                    properties["spawnPointId"] = object.name.empty()
                        ? "DefaultSpawn"
                        : object.name;
                    properties["enabled"] = true;
                };
                spawnPoint.presentation = GameplayPresentation(
                    "Spawn Point",
                    "Names a reusable spawn location in the scene.");

                Camera3D* gameplayCamera = context.services.gameplayCamera;
                bool success = context.componentRegistry.Register(
                    std::move(playerInput));
                success = context.componentRegistry.Register(
                    std::move(controller));
                success = context.componentRegistry.Register(
                    std::move(spawnPoint)) && success;
                success = context.systemTypeRegistry.Register(SystemTypeInfo{
                    "PlayerInputSystem",
                    [inputService = context.services.inputService](
                        const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return std::make_unique<PlayerInputSystem>(inputService);
                    },
                    "Player Input",
                    "GameplayBasic"
                }) && success;
                success = context.systemTypeRegistry.Register(SystemTypeInfo{
                    "PlayerMovementSystem",
                    [gameplayCamera](
                        const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return std::make_unique<PlayerMovementSystem>(
                            gameplayCamera);
                    },
                    "Player Movement",
                    "GameplayBasic"
                }) && success;
                success = context.componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "PlayerInputComponent",
                        "PlayerInputSystem",
                        100
                    }) && success;
                success = context.componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "PlayerControllerComponent",
                        "PlayerMovementSystem",
                        140
                    }) && success;
                return success;
            }
        };
    }

    std::unique_ptr<IRuntimeFeature> CreateGameplayRuntimeFeature() {
        return std::make_unique<GameplayRuntimeFeature>();
    }

} // namespace HIKARI
