#include "HIKARI_ComponentLinkComponent.h"

#include "Editor/HIKARI_IInspectorBuilder.h"
#include "HIKARI_Input.h"
#include "HIKARI_VfxPlayerComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    static GameObject* FindObjectById(World* world, SceneObjectId id) {
        if (!world || id.value == 0) {
            return nullptr;
        }
        for (const auto& object : world->GetObjects()) {
            if (object && object->GetDocumentId() == id) {
                return object.get();
            }
        }
        return nullptr;
    }

    void ComponentLinkComponent::Update(float dt) {
        (void)dt;
        World* world = RuntimeSceneContext::GetCurrentWorld();
        for (const ComponentLink& link : links_) {
            if (!HINPUT::IsPressed(link.sourceEventName)) {
                continue;
            }

            GameObject* target = FindObjectById(world, link.targetObjectId);
            if (!target) {
                target = GetOwner();
            }
            if (!target) {
                continue;
            }

            auto* vfx = target->GetComponent<VfxPlayerComponent>();
            if (!vfx) {
                continue;
            }

            if (link.targetActionName == "Play") {
                vfx->Play();
            } else if (link.targetActionName == "Stop") {
                vfx->Stop();
            } else if (link.targetActionName == "Restart") {
                vfx->RestartSlot(link.stringArg0.empty() ? "Default" : link.stringArg0);
            } else if (link.targetActionName == "PlaySlot") {
                vfx->PlaySlot(link.stringArg0.empty() ? "Default" : link.stringArg0);
            }
        }
    }

    void ComponentLinkComponent::Serialize(nlohmann::json& out) const {
        out["links"] = nlohmann::json::array();
        for (const ComponentLink& link : links_) {
            out["links"].push_back({
                { "sourceObjectId", link.sourceObjectId.value },
                { "sourceComponentType", link.sourceComponentType },
                { "sourceEventName", link.sourceEventName },
                { "targetObjectId", link.targetObjectId.value },
                { "targetComponentType", link.targetComponentType },
                { "targetActionName", link.targetActionName },
                { "stringArg0", link.stringArg0 }
            });
        }
    }

    void ComponentLinkComponent::Deserialize(const nlohmann::json& in) {
        links_.clear();
        if (!in.contains("links") || !in["links"].is_array()) {
            return;
        }

        for (const auto& node : in["links"]) {
            ComponentLink link{};
            link.sourceObjectId.value = node.value("sourceObjectId", 0ULL);
            link.sourceComponentType = node.value("sourceComponentType", "");
            link.sourceEventName = node.value("sourceEventName", "");
            link.targetObjectId.value = node.value("targetObjectId", 0ULL);
            link.targetComponentType = node.value("targetComponentType", "");
            link.targetActionName = node.value("targetActionName", "");
            link.stringArg0 = node.value("stringArg0", "");
            links_.push_back(link);
        }
    }

    void ComponentLinkComponent::BuildInspector(IInspectorBuilder& builder) {
        int count = static_cast<int>(links_.size());
        if (builder.Int("Link Count", count)) {
            if (count < 0) count = 0;
            links_.resize(static_cast<size_t>(count));
        }

        for (size_t i = 0; i < links_.size(); ++i) {
            ComponentLink& link = links_[i];
            builder.String("Event " + std::to_string(i), link.sourceEventName);
            builder.String("Target Action " + std::to_string(i), link.targetActionName);
            builder.String("Arg0 " + std::to_string(i), link.stringArg0);

            int targetId = static_cast<int>(link.targetObjectId.value);
            if (builder.Int("TargetId " + std::to_string(i), targetId)) {
                link.targetObjectId.value = static_cast<uint64_t>((targetId < 0) ? 0 : targetId);
            }
        }
    }

} // namespace HIKARI
