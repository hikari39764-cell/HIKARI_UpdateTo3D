#pragma once

#include <string>
#include <string_view>

#include "HIKARI_IComponent.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

struct PlayerCommand {
    MATH::Vec2 move{};
    MATH::Vec2 look{};
    bool jumpPressed = false;
    bool jumpHeld = false;
    bool interactPressed = false;
};

class PlayerInputComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override {
        return "PlayerInputComponent";
    }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void BuildInspector(IInspectorBuilder& builder) override;

    bool IsEnabled() const noexcept { return enabled_; }
    uint32_t GetUserId() const noexcept { return userId_; }
    const std::string& GetMoveActionId() const noexcept { return moveActionId_; }
    const std::string& GetLookActionId() const noexcept { return lookActionId_; }
    const std::string& GetJumpActionId() const noexcept { return jumpActionId_; }
    const std::string& GetInteractActionId() const noexcept { return interactActionId_; }
    const PlayerCommand& GetCommand() const noexcept { return command_; }
    void SetCommand(PlayerCommand command) noexcept { command_ = command; }
    void ClearCommand() noexcept { command_ = {}; }

private:
    bool enabled_ = true;
    uint32_t userId_ = 0;
    std::string moveActionId_{ "Gameplay.Move" };
    std::string lookActionId_{ "Gameplay.Look" };
    std::string jumpActionId_{ "Gameplay.Jump" };
    std::string interactActionId_{ "Gameplay.Interact" };
    PlayerCommand command_{};
};

} // namespace HIKARI
