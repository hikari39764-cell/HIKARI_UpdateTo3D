#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "HIKARI_IComponent.h"
#include "Vfx/HIKARI_VfxSystem.h"

namespace HIKARI {

    enum class VfxAttachmentMode {
        OwnerOrigin,
        OwnerTransform,
        TargetObject,
        NamedSocket,
        WorldSpace
    };

    struct VfxSlot {
        std::string slotName = "Default";
        std::string effectAssetId{};
        bool loop = false;
        bool autoPlay = false;
        bool restartIfAlreadyPlaying = true;
    };

    class VfxPlayerComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override { return "VfxPlayerComponent"; }

        void OnAttach() override;
        void Update(float dt) override;
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool Play();
        bool PlaySlot(const std::string& slotName);
        void Stop();
        void StopSlot(const std::string& slotName);
        void RestartSlot(const std::string& slotName);
        bool IsPlaying(const std::string& slotName) const;

    private:
        const VfxSlot* FindSlot(const std::string& slotName) const;
        VfxSlot* FindSlot(const std::string& slotName);

        bool enabled_ = true;
        bool visible_ = true;
        bool stopOnDisable_ = true;
        bool stopOnOwnerDestroy_ = true;

        VfxAttachmentMode attachmentMode_ = VfxAttachmentMode::OwnerTransform;
        SceneObjectId targetObjectId_{};
        std::string targetSocketName_{};

        MATH::Vec3 localOffset_{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 localEulerOffset_{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 localScale_{ 1.0f, 1.0f, 1.0f };

        std::vector<VfxSlot> slots_{};
        std::unordered_map<std::string, VFX::VfxHandle> playing_{};
    };

} // namespace HIKARI
