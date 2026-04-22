#include "HIKARI_VfxPlayerComponent.h"

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {

    void VfxPlayerComponent::OnAttach() {
        if (slots_.empty()) {
            slots_.push_back(VfxSlot{});
        }
    }

    void VfxPlayerComponent::Update(float dt) {
        (void)dt;
        if (!enabled_) {
            if (stopOnDisable_) {
                Stop();
            }
            return;
        }

        for (auto it = playing_.begin(); it != playing_.end();) {
            if (!VFX::IsAlive(it->second)) {
                it = playing_.erase(it);
            } else {
                ++it;
            }
        }

        for (const VfxSlot& slot : slots_) {
            if (slot.autoPlay && !IsPlaying(slot.slotName)) {
                PlaySlot(slot.slotName);
            }
        }

        for (auto& [slotName, handle] : playing_) {
            VFX::SetVisible(handle, visible_);
            if (attachmentMode_ == VfxAttachmentMode::OwnerTransform || attachmentMode_ == VfxAttachmentMode::OwnerOrigin) {
                Transform3D t = GetOwner() ? GetOwner()->Transform() : Transform3D{};
                t.position = t.position + localOffset_;
                t.scale = { t.scale.x * localScale_.x * localScaleUniform_, t.scale.y * localScale_.y * localScaleUniform_, t.scale.z * localScale_.z * localScaleUniform_ };
                VFX::SetTransform(handle, t);
            }
            (void)slotName;
        }
    }

    bool VfxPlayerComponent::Play() {
        return PlaySlot("Default");
    }

    bool VfxPlayerComponent::PlaySlot(const std::string& slotName) {
        VfxSlot* slot = FindSlot(slotName);
        if (!slot || slot->effectAssetId.empty()) {
            return false;
        }

        if (IsPlaying(slotName)) {
            if (!slot->restartIfAlreadyPlaying) {
                return true;
            }
            StopSlot(slotName);
        }

        VFX::VfxHandle handle = VFX::Play(slot->effectAssetId);
        if (handle == VFX::kInvalidVfxHandle) {
            return false;
        }

        playing_[slotName] = handle;
        return true;
    }

    void VfxPlayerComponent::Stop() {
        for (const auto& [_, handle] : playing_) {
            VFX::Stop(handle);
        }
        playing_.clear();
    }

    void VfxPlayerComponent::StopSlot(const std::string& slotName) {
        auto it = playing_.find(slotName);
        if (it == playing_.end()) {
            return;
        }

        VFX::Stop(it->second);
        playing_.erase(it);
    }

    void VfxPlayerComponent::RestartSlot(const std::string& slotName) {
        StopSlot(slotName);
        PlaySlot(slotName);
    }

    bool VfxPlayerComponent::IsPlaying(const std::string& slotName) const {
        auto it = playing_.find(slotName);
        return (it != playing_.end()) && VFX::IsAlive(it->second);
    }

    const VfxSlot* VfxPlayerComponent::FindSlot(const std::string& slotName) const {
        for (const VfxSlot& slot : slots_) {
            if (slot.slotName == slotName) {
                return &slot;
            }
        }
        return nullptr;
    }

    VfxSlot* VfxPlayerComponent::FindSlot(const std::string& slotName) {
        for (VfxSlot& slot : slots_) {
            if (slot.slotName == slotName) {
                return &slot;
            }
        }
        return nullptr;
    }

    void VfxPlayerComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["visible"] = visible_;
        out["stopOnDisable"] = stopOnDisable_;
        out["stopOnOwnerDestroy"] = stopOnOwnerDestroy_;
        out["attachmentMode"] = static_cast<int>(attachmentMode_);
        out["targetObjectId"] = targetObjectId_.value;
        out["targetSocketName"] = targetSocketName_;
        out["localOffset"] = { { "x", localOffset_.x }, { "y", localOffset_.y }, { "z", localOffset_.z } };
        out["localEulerOffset"] = { { "x", localEulerOffset_.x }, { "y", localEulerOffset_.y }, { "z", localEulerOffset_.z } };
        out["localScale"] = { { "x", localScale_.x }, { "y", localScale_.y }, { "z", localScale_.z } };
        out["localScaleUniform"] = localScaleUniform_;

        out["slots"] = nlohmann::json::array();
        for (const VfxSlot& slot : slots_) {
            out["slots"].push_back({
                { "slotName", slot.slotName },
                { "effectAssetId", slot.effectAssetId },
                { "loop", slot.loop },
                { "autoPlay", slot.autoPlay },
                { "restartIfAlreadyPlaying", slot.restartIfAlreadyPlaying }
            });
        }
    }

    void VfxPlayerComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        visible_ = in.value("visible", visible_);
        stopOnDisable_ = in.value("stopOnDisable", stopOnDisable_);
        stopOnOwnerDestroy_ = in.value("stopOnOwnerDestroy", stopOnOwnerDestroy_);
        attachmentMode_ = static_cast<VfxAttachmentMode>(in.value("attachmentMode", static_cast<int>(attachmentMode_)));
        targetObjectId_.value = in.value("targetObjectId", targetObjectId_.value);
        targetSocketName_ = in.value("targetSocketName", targetSocketName_);

        auto readVec3 = [](const nlohmann::json& obj, MATH::Vec3& out) {
            if (!obj.is_object()) return;
            out.x = obj.value("x", out.x);
            out.y = obj.value("y", out.y);
            out.z = obj.value("z", out.z);
        };

        if (in.contains("localOffset")) readVec3(in["localOffset"], localOffset_);
        if (in.contains("localEulerOffset")) readVec3(in["localEulerOffset"], localEulerOffset_);
        if (in.contains("localScale")) readVec3(in["localScale"], localScale_);
		localScaleUniform_ = in.value("localScaleUniform", localScaleUniform_);

        if (in.contains("slots") && in["slots"].is_array()) {
            slots_.clear();
            for (const auto& node : in["slots"]) {
                VfxSlot slot{};
                slot.slotName = node.value("slotName", slot.slotName);
                slot.effectAssetId = node.value("effectAssetId", slot.effectAssetId);
                slot.loop = node.value("loop", slot.loop);
                slot.autoPlay = node.value("autoPlay", slot.autoPlay);
                slot.restartIfAlreadyPlaying = node.value("restartIfAlreadyPlaying", slot.restartIfAlreadyPlaying);
                slots_.push_back(slot);
            }
        }

        if (slots_.empty()) {
            slots_.push_back(VfxSlot{});
        }
    }

    void VfxPlayerComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.Bool("Enabled", enabled_);
        builder.Bool("Visible", visible_);
        builder.Bool("Stop On Disable", stopOnDisable_);
        builder.Bool("Stop On Owner Destroy", stopOnOwnerDestroy_);

        int attachment = static_cast<int>(attachmentMode_);
        if (builder.Int("Attachment Mode", attachment)) {
            if (attachment < 0) attachment = 0;
            if (attachment > 4) attachment = 4;
            attachmentMode_ = static_cast<VfxAttachmentMode>(attachment);
        }

        int targetId = static_cast<int>(targetObjectId_.value);
        if (builder.Int("Target ObjectId", targetId)) {
            targetObjectId_.value = static_cast<uint64_t>((targetId < 0) ? 0 : targetId);
        }
        builder.String("Target Socket", targetSocketName_);

        builder.Float("Offset X", localOffset_.x);
        builder.Float("Offset Y", localOffset_.y);
        builder.Float("Offset Z", localOffset_.z);
        builder.Float("Euler X", localEulerOffset_.x);
        builder.Float("Euler Y", localEulerOffset_.y);
        builder.Float("Euler Z", localEulerOffset_.z);
        builder.Float("Scale X", localScale_.x);
        builder.Float("Scale Y", localScale_.y);
        builder.Float("Scale Z", localScale_.z);
        builder.Float("Scale Uniform", localScaleUniform_);

        int slotCount = static_cast<int>(slots_.size());
        if (builder.Int("Slot Count", slotCount)) {
            if (slotCount < 1) slotCount = 1;
            slots_.resize(static_cast<size_t>(slotCount));
            for (size_t i = 0; i < slots_.size(); ++i) {
                if (slots_[i].slotName.empty()) {
                    slots_[i].slotName = "Slot" + std::to_string(i);
                }
            }
        }

        for (size_t i = 0; i < slots_.size(); ++i) {
            VfxSlot& slot = slots_[i];
            builder.String("Slot Name " + std::to_string(i), slot.slotName);
            builder.AssetIdPicker("Effect " + std::to_string(i), AssetType::VfxEffect, slot.effectAssetId);
            builder.Bool("Loop " + std::to_string(i), slot.loop);
            builder.Bool("AutoPlay " + std::to_string(i), slot.autoPlay);
            builder.Bool("Restart " + std::to_string(i), slot.restartIfAlreadyPlaying);
        }

        bool playDefault = false;
        bool stopDefault = false;
        if (builder.Bool("Play Default", playDefault) && playDefault) {
            Play();
        }
        if (builder.Bool("Stop Default", stopDefault) && stopDefault) {
            StopSlot("Default");
        }
    }

} // namespace HIKARI
