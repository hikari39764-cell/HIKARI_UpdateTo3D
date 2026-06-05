#pragma once

#include <string>
#include <string_view>

#include "HIKARI_IComponent.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    enum class PlayerMovementState {
        Idle,
        Moving,
    };

    class PlayerControllerComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override { return "PlayerControllerComponent"; }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const;
        const std::string& GetMoveXAxisName() const;
        const std::string& GetMoveYAxisName() const;
        float GetMoveSpeed() const;
        float GetAcceleration() const;
        float GetDeceleration() const;
        float GetTurnSpeed() const;
        float GetInputDeadZone() const;
        bool GetRotateToMove() const;
        bool GetCameraRelativeMovement() const;
        bool GetUseBounds() const;
        float GetMinX() const;
        float GetMaxX() const;
        float GetMinZ() const;
        float GetMaxZ() const;
        bool GetAnimationEnabled() const;
        bool GetAutoSelectAnimationClips() const;
        const std::string& GetIdleClip() const;
        const std::string& GetMoveClip() const;
        PlayerMovementState GetState() const;
        const MATH::Vec3& GetVelocity() const;
        float GetRuntimeAppliedMoveSpeed() const;

        void SetRuntimeState(PlayerMovementState state, const MATH::Vec3& velocity, float appliedMoveSpeed);

    private:
        void ClampSettings();

        bool enabled_ = true;
        std::string moveXAxisName_{ "MoveX" };
        std::string moveYAxisName_{ "MoveY" };
        float moveSpeed_ = 4.0f;
        float acceleration_ = 60.0f;
        float deceleration_ = 72.0f;
        float turnSpeed_ = 12.0f;
        float inputDeadZone_ = 0.08f;
        bool rotateToMove_ = true;
        bool cameraRelativeMovement_ = true;

        bool useBounds_ = true;
        float minX_ = -12.0f;
        float maxX_ = 12.0f;
        float minZ_ = -12.0f;
        float maxZ_ = 12.0f;

        bool animationEnabled_ = true;
        bool autoSelectAnimationClips_ = true;
        std::string idleClip_{};
        std::string moveClip_{};

        PlayerMovementState state_ = PlayerMovementState::Idle;
        MATH::Vec3 velocity_{};
        float runtimeAppliedMoveSpeed_ = 4.0f;
    };

} // namespace HIKARI
