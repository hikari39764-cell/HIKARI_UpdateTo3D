#pragma once

#include <string>
#include <vector>

#include "HIKARI_IComponent.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    struct ComponentLink {
        SceneObjectId sourceObjectId{};
        std::string sourceComponentType{};
        std::string sourceEventName{};

        SceneObjectId targetObjectId{};
        std::string targetComponentType{};
        std::string targetActionName{};

        std::string stringArg0{};
    };

    class ComponentLinkComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override { return "ComponentLinkComponent"; }

        void Update(float dt) override;
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

    private:
        std::vector<ComponentLink> links_{};
    };

} // namespace HIKARI
