#include "HIKARI_TriggerVolumeComponent.h"

#include "Editor/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    void TriggerVolumeComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["boxSize"] = {
            { "x", boxSizeX_ },
            { "y", boxSizeY_ },
            { "z", boxSizeZ_ }
        };
    }

    void TriggerVolumeComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);

        if (in.contains("boxSize") && in["boxSize"].is_object()) {
            const nlohmann::json& boxSize = in["boxSize"];
            boxSizeX_ = boxSize.value("x", boxSizeX_);
            boxSizeY_ = boxSize.value("y", boxSizeY_);
            boxSizeZ_ = boxSize.value("z", boxSizeZ_);
        }
    }

    void TriggerVolumeComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.Bool("Enabled", enabled_);
        builder.Float("Box Size X", boxSizeX_);
        builder.Float("Box Size Y", boxSizeY_);
        builder.Float("Box Size Z", boxSizeZ_);
    }

} // namespace HIKARI
