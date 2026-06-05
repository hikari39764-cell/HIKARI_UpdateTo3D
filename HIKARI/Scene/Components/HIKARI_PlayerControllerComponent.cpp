#include "HIKARI_PlayerControllerComponent.h"

#include <algorithm>

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    void PlayerControllerComponent::ClampSettings() {
        moveSpeed_ = (std::max)(moveSpeed_, 0.0f);
        acceleration_ = (std::max)(acceleration_, 1.0f);
        deceleration_ = (std::max)(deceleration_, 1.0f);
        turnSpeed_ = (std::max)(turnSpeed_, 0.0f);
        inputDeadZone_ = (std::clamp)(inputDeadZone_, 0.0f, 0.95f);

        if (minX_ > maxX_) {
            std::swap(minX_, maxX_);
        }
        if (minZ_ > maxZ_) {
            std::swap(minZ_, maxZ_);
        }
    }

    void PlayerControllerComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["moveXAxisName"] = moveXAxisName_;
        out["moveYAxisName"] = moveYAxisName_;
        out["moveSpeed"] = moveSpeed_;
        out["acceleration"] = acceleration_;
        out["deceleration"] = deceleration_;
        out["turnSpeed"] = turnSpeed_;
        out["inputDeadZone"] = inputDeadZone_;
        out["rotateToMove"] = rotateToMove_;
        out["cameraRelativeMovement"] = cameraRelativeMovement_;
        out["useBounds"] = useBounds_;
        out["bounds"] = {
            { "minX", minX_ },
            { "maxX", maxX_ },
            { "minZ", minZ_ },
            { "maxZ", maxZ_ }
        };
        out["animationEnabled"] = animationEnabled_;
        out["autoSelectAnimationClips"] = autoSelectAnimationClips_;
        out["idleClip"] = idleClip_;
        out["moveClip"] = moveClip_;
    }

    void PlayerControllerComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        moveXAxisName_ = in.value("moveXAxisName", moveXAxisName_);
        moveYAxisName_ = in.value("moveYAxisName", moveYAxisName_);
        moveSpeed_ = in.value("moveSpeed", moveSpeed_);
        acceleration_ = in.value("acceleration", acceleration_);
        deceleration_ = in.value("deceleration", deceleration_);
        turnSpeed_ = in.value("turnSpeed", turnSpeed_);
        inputDeadZone_ = in.value("inputDeadZone", inputDeadZone_);
        rotateToMove_ = in.value("rotateToMove", rotateToMove_);
        cameraRelativeMovement_ = in.value("cameraRelativeMovement", cameraRelativeMovement_);
        useBounds_ = in.value("useBounds", useBounds_);
        if (in.contains("bounds") && in["bounds"].is_object()) {
            const nlohmann::json& bounds = in["bounds"];
            minX_ = bounds.value("minX", minX_);
            maxX_ = bounds.value("maxX", maxX_);
            minZ_ = bounds.value("minZ", minZ_);
            maxZ_ = bounds.value("maxZ", maxZ_);
        } else {
            minX_ = in.value("minX", minX_);
            maxX_ = in.value("maxX", maxX_);
            minZ_ = in.value("minZ", minZ_);
            maxZ_ = in.value("maxZ", maxZ_);
        }
        animationEnabled_ = in.value("animationEnabled", animationEnabled_);
        autoSelectAnimationClips_ = in.value("autoSelectAnimationClips", autoSelectAnimationClips_);
        idleClip_ = in.value("idleClip", idleClip_);
        moveClip_ = in.value("moveClip", moveClip_);
        ClampSettings();
        runtimeAppliedMoveSpeed_ = moveSpeed_;
    }

    void PlayerControllerComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.Bool("Enabled", enabled_);
        builder.String("Move X Axis", moveXAxisName_);
        builder.String("Move Y Axis", moveYAxisName_);
        builder.Float("Move Speed", moveSpeed_);
        builder.Float("Acceleration", acceleration_);
        builder.Float("Deceleration", deceleration_);
        builder.Float("Turn Speed", turnSpeed_);
        builder.Float("Input Dead Zone", inputDeadZone_);
        builder.Bool("Rotate To Move", rotateToMove_);
        builder.Bool("Camera Relative Movement", cameraRelativeMovement_);
        builder.Bool("Use Bounds", useBounds_);
        builder.Float("Bounds Min X", minX_);
        builder.Float("Bounds Max X", maxX_);
        builder.Float("Bounds Min Z", minZ_);
        builder.Float("Bounds Max Z", maxZ_);
        builder.Bool("Animation Enabled", animationEnabled_);
        builder.Bool("Auto Select Animation Clips", autoSelectAnimationClips_);
        builder.String("Idle Clip", idleClip_);
        builder.String("Move Clip", moveClip_);
        ClampSettings();
    }

    bool PlayerControllerComponent::IsEnabled() const { return enabled_; }
    const std::string& PlayerControllerComponent::GetMoveXAxisName() const { return moveXAxisName_; }
    const std::string& PlayerControllerComponent::GetMoveYAxisName() const { return moveYAxisName_; }
    float PlayerControllerComponent::GetMoveSpeed() const { return (std::max)(moveSpeed_, 0.0f); }
    float PlayerControllerComponent::GetAcceleration() const { return (std::max)(acceleration_, 0.0f); }
    float PlayerControllerComponent::GetDeceleration() const { return (std::max)(deceleration_, 0.0f); }
    float PlayerControllerComponent::GetTurnSpeed() const { return (std::max)(turnSpeed_, 0.0f); }
    float PlayerControllerComponent::GetInputDeadZone() const { return (std::clamp)(inputDeadZone_, 0.0f, 0.95f); }
    bool PlayerControllerComponent::GetRotateToMove() const { return rotateToMove_; }
    bool PlayerControllerComponent::GetCameraRelativeMovement() const { return cameraRelativeMovement_; }
    bool PlayerControllerComponent::GetUseBounds() const { return useBounds_; }
    float PlayerControllerComponent::GetMinX() const { return (std::min)(minX_, maxX_); }
    float PlayerControllerComponent::GetMaxX() const { return (std::max)(minX_, maxX_); }
    float PlayerControllerComponent::GetMinZ() const { return (std::min)(minZ_, maxZ_); }
    float PlayerControllerComponent::GetMaxZ() const { return (std::max)(minZ_, maxZ_); }
    bool PlayerControllerComponent::GetAnimationEnabled() const { return animationEnabled_; }
    bool PlayerControllerComponent::GetAutoSelectAnimationClips() const { return autoSelectAnimationClips_; }
    const std::string& PlayerControllerComponent::GetIdleClip() const { return idleClip_; }
    const std::string& PlayerControllerComponent::GetMoveClip() const { return moveClip_; }
    PlayerMovementState PlayerControllerComponent::GetState() const { return state_; }
    const MATH::Vec3& PlayerControllerComponent::GetVelocity() const { return velocity_; }
    float PlayerControllerComponent::GetRuntimeAppliedMoveSpeed() const { return runtimeAppliedMoveSpeed_; }

    void PlayerControllerComponent::SetRuntimeState(
        PlayerMovementState state,
        const MATH::Vec3& velocity,
        float appliedMoveSpeed) {
        state_ = state;
        velocity_ = velocity;
        runtimeAppliedMoveSpeed_ = appliedMoveSpeed;
    }

} // namespace HIKARI
