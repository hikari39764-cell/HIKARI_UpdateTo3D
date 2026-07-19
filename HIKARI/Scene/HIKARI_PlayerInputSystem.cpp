#include "Scene/HIKARI_PlayerInputSystem.h"

#include "Input/Runtime/HIKARI_InputService.h"
#include "Scene/Components/HIKARI_PlayerInputComponent.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

void PlayerInputSystem::OnWorldAttached(World& world) {
    inputService_ = world.Services().Find<INPUT::InputService>();
}

void PlayerInputSystem::OnWorldDetached(World&) {
    inputService_ = nullptr;
}

void PlayerInputSystem::PreUpdate(World& world, const FrameContext&) {
    const INPUT::InputSnapshot* snapshot = inputService_ != nullptr
        ? &inputService_->GetSnapshot() : nullptr;
    world.ForEachObjectWith<PlayerInputComponent>(
        [snapshot](GameObject&, PlayerInputComponent& input) {
            if (snapshot == nullptr || !input.IsEnabled()) {
                input.ClearCommand();
                return;
            }
            const std::array<float, 2> move = snapshot->GetAxis2D(
                input.GetMoveActionId());
            const std::array<float, 2> look = snapshot->GetAxis2D(
                input.GetLookActionId());
            PlayerCommand command{};
            command.move = { move[0], move[1] };
            command.look = { look[0], look[1] };
            command.jumpPressed = snapshot->IsPressed(input.GetJumpActionId());
            command.jumpHeld = snapshot->IsDown(input.GetJumpActionId());
            command.interactPressed = snapshot->IsPressed(
                input.GetInteractActionId());
            input.SetCommand(command);
        });
}

} // namespace HIKARI
