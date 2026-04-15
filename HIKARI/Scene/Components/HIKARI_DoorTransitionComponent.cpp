#include "HIKARI_DoorTransitionComponent.h"

#include "Editor/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    void DoorTransitionComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["targetSceneId"] = targetSceneId_;
        out["targetSpawnPointId"] = targetSpawnPointId_;
        out["requireInteractKey"] = requireInteractKey_;
    }

    void DoorTransitionComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        targetSceneId_ = in.value("targetSceneId", targetSceneId_);
        targetSpawnPointId_ = in.value("targetSpawnPointId", targetSpawnPointId_);
        requireInteractKey_ = in.value("requireInteractKey", requireInteractKey_);
    }

    void DoorTransitionComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.Bool("Enabled", enabled_);
        builder.SceneIdPicker("Target Scene ID", targetSceneId_);
        builder.String("Target Spawn Point", targetSpawnPointId_);
        builder.Bool("Require Interact Key", requireInteractKey_);
    }

    bool DoorTransitionComponent::IsEnabled() const {
        return enabled_;
    }

    const std::string& DoorTransitionComponent::GetTargetSceneId() const {
        return targetSceneId_;
    }

    const std::string& DoorTransitionComponent::GetTargetSpawnPointId() const {
        return targetSpawnPointId_;
    }

} // namespace HIKARI
