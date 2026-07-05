#include "HIKARI_CameraFollowComponent.h"

#include <algorithm>

#include "Core/HIKARI_JsonRead.h"
#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    namespace {
        nlohmann::json ToJson(const MATH::Vec3& value) {
            return nlohmann::json::array({ value.x, value.y, value.z });
        }
    }

    void CameraFollowComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["targetObjectId"] = targetObjectId_.value;
        out["useOwnerAsFallbackTarget"] = useOwnerAsFallbackTarget_;
        out["offset"] = ToJson(offset_);
        out["lookAtOffset"] = ToJson(lookAtOffset_);
        out["followSmooth"] = followSmooth_;
        out["lookSmooth"] = lookSmooth_;
    }

    void CameraFollowComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        targetObjectId_.value = in.value("targetObjectId", targetObjectId_.value);
        useOwnerAsFallbackTarget_ = in.value("useOwnerAsFallbackTarget", useOwnerAsFallbackTarget_);
        if (in.contains("offset")) {
            offset_ = JSONREAD::Vec3Or(in["offset"], offset_);
        }
        if (in.contains("lookAtOffset")) {
            lookAtOffset_ = JSONREAD::Vec3Or(in["lookAtOffset"], lookAtOffset_);
        }
        followSmooth_ = in.value("followSmooth", followSmooth_);
        lookSmooth_ = in.value("lookSmooth", lookSmooth_);
        ResetRuntimeCameraState();
    }

    void CameraFollowComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.Bool("Enabled", enabled_);
        int targetId = static_cast<int>(targetObjectId_.value);
        if (builder.Int("Target ObjectId", targetId)) {
            targetObjectId_.value = static_cast<uint64_t>((std::max)(0, targetId));
            ResetRuntimeCameraState();
        }
        builder.Bool("Use Owner Fallback", useOwnerAsFallbackTarget_);
        builder.Float("Offset X", offset_.x);
        builder.Float("Offset Y", offset_.y);
        builder.Float("Offset Z", offset_.z);
        builder.Float("Look At Offset X", lookAtOffset_.x);
        builder.Float("Look At Offset Y", lookAtOffset_.y);
        builder.Float("Look At Offset Z", lookAtOffset_.z);
        builder.Float("Follow Smooth", followSmooth_);
        builder.Float("Look Smooth", lookSmooth_);
    }

    bool CameraFollowComponent::IsEnabled() const { return enabled_; }
    SceneObjectId CameraFollowComponent::GetTargetObjectId() const { return targetObjectId_; }
    bool CameraFollowComponent::GetUseOwnerAsFallbackTarget() const { return useOwnerAsFallbackTarget_; }
    const MATH::Vec3& CameraFollowComponent::GetOffset() const { return offset_; }
    const MATH::Vec3& CameraFollowComponent::GetLookAtOffset() const { return lookAtOffset_; }
    float CameraFollowComponent::GetFollowSmooth() const { return (std::max)(followSmooth_, 0.0f); }
    float CameraFollowComponent::GetLookSmooth() const { return (std::max)(lookSmooth_, 0.0f); }

    bool CameraFollowComponent::HasRuntimeCameraState() const { return runtimeInitialized_; }
    const MATH::Vec3& CameraFollowComponent::GetRuntimeEye() const { return runtimeEye_; }
    const MATH::Vec3& CameraFollowComponent::GetRuntimeLookAt() const { return runtimeLookAt_; }

    void CameraFollowComponent::SetRuntimeCameraState(const MATH::Vec3& eye, const MATH::Vec3& lookAt) {
        runtimeEye_ = eye;
        runtimeLookAt_ = lookAt;
        runtimeInitialized_ = true;
    }

    void CameraFollowComponent::ResetRuntimeCameraState() {
        runtimeInitialized_ = false;
        runtimeEye_ = {};
        runtimeLookAt_ = {};
    }

} // namespace HIKARI
