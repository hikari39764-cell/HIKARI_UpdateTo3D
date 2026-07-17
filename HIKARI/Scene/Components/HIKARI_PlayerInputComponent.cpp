#include "Scene/Components/HIKARI_PlayerInputComponent.h"

#include <algorithm>

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

void PlayerInputComponent::Serialize(nlohmann::json& out) const {
    out["enabled"] = enabled_;
    out["userId"] = userId_;
    out["moveAction"] = moveActionId_;
    out["lookAction"] = lookActionId_;
    out["jumpAction"] = jumpActionId_;
    out["interactAction"] = interactActionId_;
}

void PlayerInputComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = in.value("enabled", enabled_);
    userId_ = in.value("userId", userId_);
    moveActionId_ = in.value("moveAction", moveActionId_);
    lookActionId_ = in.value("lookAction", lookActionId_);
    jumpActionId_ = in.value("jumpAction", jumpActionId_);
    interactActionId_ = in.value("interactAction", interactActionId_);
    ClearCommand();
}

void PlayerInputComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Bool("Enabled", enabled_);
    int userId = static_cast<int>(userId_);
    builder.Int("Input User", userId);
    userId_ = static_cast<uint32_t>((std::max)(userId, 0));
    builder.String("Move Action", moveActionId_);
    builder.String("Look Action", lookActionId_);
    builder.String("Jump Action", jumpActionId_);
    builder.String("Interact Action", interactActionId_);
}

} // namespace HIKARI
