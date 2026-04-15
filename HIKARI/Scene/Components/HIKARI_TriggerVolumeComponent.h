#pragma once

#include "HIKARI_IComponent.h"

namespace HIKARI {

    class TriggerVolumeComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override { return "TriggerVolumeComponent"; }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

    private:
        bool enabled_ = true;
        float boxSizeX_ = 1.0f;
        float boxSizeY_ = 1.0f;
        float boxSizeZ_ = 1.0f;
    };

} // namespace HIKARI
