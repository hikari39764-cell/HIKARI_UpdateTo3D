#include "Editor/SystemAuthoring/HIKARI_BuiltInSystemAuthoring.h"

#include <array>
#include <utility>

#include "Editor/SystemAuthoring/HIKARI_SystemAuthoringRegistry.h"

namespace HIKARI::EDITOR {
    namespace {

        SystemAuthoringDescriptor RuntimeOnly(
            const char* systemId,
            const char* description,
            const char* hint) {

            SystemAuthoringDescriptor descriptor{};
            descriptor.systemId = systemId;
            descriptor.description = description;
            descriptor.scope = SystemAuthoringScope::RuntimeOnly;
            descriptor.configurationHint = hint;
            return descriptor;
        }

        SystemAuthoringDescriptor ComponentOwned(
            const char* systemId,
            const char* description,
            const char* hint) {

            SystemAuthoringDescriptor descriptor{};
            descriptor.systemId = systemId;
            descriptor.description = description;
            descriptor.scope = SystemAuthoringScope::ComponentOwned;
            descriptor.configurationHint = hint;
            return descriptor;
        }

        SystemAuthoringDescriptor ProjectOwned(
            const char* systemId,
            const char* description,
            const char* hint) {

            SystemAuthoringDescriptor descriptor{};
            descriptor.systemId = systemId;
            descriptor.description = description;
            descriptor.scope = SystemAuthoringScope::ProjectSettings;
            descriptor.configurationHint = hint;
            return descriptor;
        }
    }

    void RegisterBuiltInSystemAuthoring(
        SystemAuthoringRegistry& registry) {

        std::array<SystemAuthoringDescriptor, 6> descriptors{
            ProjectOwned(
                "ModelRenderSystem",
                "Submits scene models to the renderer.",
                "Rendering quality and pipeline choices are configured in the Quality panel."),
            ComponentOwned(
                "AnimationSystem",
                "Advances object animation state.",
                "Animation clips, speed, looping, and autoplay are configured on Animator components."),
            ComponentOwned(
                "CameraFollowSystem",
                "Updates gameplay cameras that follow scene targets.",
                "Target, offset, and smoothing are configured on Camera Follow components."),
            ComponentOwned(
                "PlayerInputSystem",
                "Maps project input actions into per-player commands.",
                "Action names are configured on Player Input components; physical bindings are edited in Project Input."),
            ComponentOwned(
                "PlayerMovementSystem",
                "Applies player commands to controlled scene objects.",
                "Movement, rotation, bounds, and animation behavior are configured on Player Controller components."),
            ComponentOwned(
                "SequencePlayerSystem",
                "Runs reusable Sequence assets during gameplay.",
                "Sequence assets, playback policy, and binding slots are configured on Sequence Player components."),
        };

        for (SystemAuthoringDescriptor& descriptor : descriptors) {
            registry.Register(std::move(descriptor));
        }
    }

} // namespace HIKARI::EDITOR
