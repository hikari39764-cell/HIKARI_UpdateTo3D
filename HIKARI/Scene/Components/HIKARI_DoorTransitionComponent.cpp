#include "HIKARI_DoorTransitionComponent.h"

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    void DoorTransitionComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["targetSceneAssetGuid"] = targetSceneAssetGuid_;
        out["requireInteractKey"] = requireInteractKey_;
    }

    void DoorTransitionComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        targetSceneAssetGuid_ = in.value("targetSceneAssetGuid", targetSceneAssetGuid_);
        requireInteractKey_ = in.value("requireInteractKey", requireInteractKey_);
    }

    void DoorTransitionComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.Bool("Enabled", enabled_);
        builder.SceneIdPicker("Target Scene Asset", targetSceneAssetGuid_);
        builder.Bool("Require Interact Key", requireInteractKey_);
    }

    bool DoorTransitionComponent::IsEnabled() const {
        return enabled_;
    }

    const std::string& DoorTransitionComponent::GetTargetSceneAssetGuid() const {
        return targetSceneAssetGuid_;
    }

} // namespace HIKARI
