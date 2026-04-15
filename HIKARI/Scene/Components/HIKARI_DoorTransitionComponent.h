#pragma once

#include <string>

#include "HIKARI_IComponent.h"

namespace HIKARI {

    class DoorTransitionComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override { return "DoorTransitionComponent"; }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const;
        const std::string& GetTargetSceneId() const;
        const std::string& GetTargetSpawnPointId() const;

    private:
        bool enabled_ = true;
        std::string targetSceneId_{};
        std::string targetSpawnPointId_{};
        bool requireInteractKey_ = true;
    };

} // namespace HIKARI
