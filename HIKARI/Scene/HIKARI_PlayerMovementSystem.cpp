#include "HIKARI_PlayerMovementSystem.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <initializer_list>
#include <string>
#include <string_view>

#include "Core/HIKARI_FrameContext.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/Components/HIKARI_PlayerControllerComponent.h"
#include "Scene/Components/HIKARI_PlayerInputComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    namespace {
        constexpr float kPi = 3.1415926535f;
        constexpr float kTwoPi = kPi * 2.0f;

        float Clamp(float value, float minValue, float maxValue) {
            return (std::min)((std::max)(value, minValue), maxValue);
        }

        float WrapAngle(float angle) {
            while (angle > kPi) {
                angle -= kTwoPi;
            }
            while (angle < -kPi) {
                angle += kTwoPi;
            }
            return angle;
        }

        float MoveTowards(float current, float target, float maxDelta) {
            const float delta = target - current;
            if (std::abs(delta) <= maxDelta) {
                return target;
            }
            return current + (delta > 0.0f ? maxDelta : -maxDelta);
        }

        MATH::Vec3 MoveVectorTowards(const MATH::Vec3& current, const MATH::Vec3& target, float maxDelta) {
            const MATH::Vec3 delta = target - current;
            const float distance = MATH::Length(delta);
            if (distance <= maxDelta || distance <= 1e-5f) {
                return target;
            }
            return current + delta * (maxDelta / distance);
        }

        MATH::Vec3 FlattenAndNormalize(const MATH::Vec3& value) {
            return MATH::Normalize({ value.x, 0.0f, value.z });
        }

        float ExtractHorizontalYaw(const MATH::Quat& rotation) {
            const MATH::Mat4 rotationMatrix = MATH::Mat4::Rotate(MATH::NormalizeQ(rotation));
            const MATH::Vec3 forward = FlattenAndNormalize({
                rotationMatrix.m[2][0],
                rotationMatrix.m[2][1],
                rotationMatrix.m[2][2]
            });
            if (MATH::Length(forward) <= 1e-5f) {
                return 0.0f;
            }
            return std::atan2(forward.x, forward.z);
        }

        MATH::Vec3 ResolveMoveDirection(
            const PlayerControllerComponent& player,
            const Camera3D* camera,
            float inputX,
            float inputY) {

            MATH::Vec3 inputDirection{ inputX, 0.0f, inputY };
            const float inputLength = MATH::Length(inputDirection);
            if (inputLength <= player.GetInputDeadZone()) {
                return {};
            }
            if (inputLength > 1.0f) {
                inputDirection = inputDirection * (1.0f / inputLength);
            }

            if (!player.GetCameraRelativeMovement() || camera == nullptr) {
                return inputDirection;
            }

            const MATH::Mat4& view = camera->GetView();
            const MATH::Vec3 cameraForward = FlattenAndNormalize({
                -view.m[0][2],
                -view.m[1][2],
                -view.m[2][2]
            });
            const MATH::Vec3 cameraRight = FlattenAndNormalize({
                view.m[0][0],
                view.m[1][0],
                view.m[2][0]
            });

            MATH::Vec3 worldDirection =
                cameraRight * inputDirection.x +
                cameraForward * inputDirection.z;
            const float worldLength = MATH::Length(worldDirection);
            if (worldLength <= 1e-5f) {
                return inputDirection;
            }
            return worldLength > 1.0f
                ? worldDirection * (1.0f / worldLength)
                : worldDirection;
        }

        std::string ToLowerCopy(std::string_view value) {
            std::string out{ value };
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        bool ContainsAny(std::string_view value, std::initializer_list<std::string_view> keywords) {
            const std::string lower = ToLowerCopy(value);
            for (std::string_view keyword : keywords) {
                if (lower.find(keyword) != std::string::npos) {
                    return true;
                }
            }
            return false;
        }

        const AnimationClip* FindClipByKeywords(
            const ModelAsset& asset,
            std::initializer_list<std::string_view> keywords) {

            for (const AnimationClip& clip : asset.animations) {
                if (ContainsAny(clip.name, keywords)) {
                    return &clip;
                }
            }
            return nullptr;
        }

        std::string ResolveAnimationClip(
            const PlayerControllerComponent& player,
            const ModelAsset* asset,
            PlayerMovementState state) {

            const std::string& configured =
                state == PlayerMovementState::Moving ? player.GetMoveClip() : player.GetIdleClip();
            if (!configured.empty()) {
                return configured;
            }

            if (!player.GetAutoSelectAnimationClips() || asset == nullptr || asset->animations.empty()) {
                return {};
            }

            const AnimationClip* clip = nullptr;
            if (state == PlayerMovementState::Moving) {
                clip = FindClipByKeywords(*asset, { "walk", "run", "move", "locomotion" });
                if (clip == nullptr) {
                    clip = asset->GetAnimationClip(0);
                }
            } else {
                clip = FindClipByKeywords(*asset, { "idle", "stand", "wait" });
            }
            return clip != nullptr ? clip->name : std::string{};
        }

        void ApplyPlayerAnimation(
            GameObject& object,
            const PlayerControllerComponent& player,
            PlayerMovementState state) {

            if (!player.GetAnimationEnabled()) {
                return;
            }

            AnimatorComponent* animator = object.GetComponent<AnimatorComponent>();
            if (animator == nullptr) {
                return;
            }

            const ModelComponent* model = object.GetComponent<ModelComponent>();
            const ModelAsset* asset = model != nullptr ? model->GetModelAsset() : nullptr;
            const std::string desiredClip = ResolveAnimationClip(player, asset, state);
            if (desiredClip.empty()) {
                if (state == PlayerMovementState::Idle) {
                    animator->Pause();
                }
                return;
            }

            const bool changedClip = animator->GetClip() != desiredClip;
            if (changedClip) {
                animator->Play(desiredClip, true, true);
            } else if (!animator->IsPlaying()) {
                animator->Resume();
            }
        }

        void UpdateRotationTowards(GameObject& object, const MATH::Vec3& moveDirection, float turnSpeed, float dt) {
            if (MATH::Length(moveDirection) <= 1e-5f) {
                return;
            }

            const float targetYaw = std::atan2(moveDirection.x, moveDirection.z);
            const float currentYaw = ExtractHorizontalYaw(object.Transform().rotation);
            const float delta = WrapAngle(targetYaw - currentYaw);
            const float maxStep = turnSpeed > 0.0f ? turnSpeed * dt : std::abs(delta);
            const float nextYaw = currentYaw + MoveTowards(0.0f, delta, maxStep);
            object.Transform().rotation = MATH::Quat::FromEulerXYZ(0.0f, nextYaw, 0.0f);
        }
    }

    PlayerMovementSystem::PlayerMovementSystem(const Camera3D* camera)
        : camera_(camera) {
    }

    void PlayerMovementSystem::Update(World& world, const FrameContext& frame) {
        const float dt = (std::max)(frame.gameDt, 0.0f);
        const Camera3D* camera = camera_;

        world.ForEachObjectWith<PlayerControllerComponent>(
            [dt, camera](GameObject& object, PlayerControllerComponent& player) {
                MATH::Vec3 velocity = player.GetVelocity();
                PlayerMovementState state = PlayerMovementState::Idle;
                const float moveSpeed = player.GetMoveSpeed();

                if (player.IsEnabled() && dt > 0.0f) {
                    const PlayerInputComponent* playerInput =
                        object.GetComponent<PlayerInputComponent>();
                    const PlayerCommand command = playerInput != nullptr
                        ? playerInput->GetCommand() : PlayerCommand{};
                    const float inputX = command.move.x;
                    const float inputY = command.move.y;

                    const MATH::Vec3 inputDirection = ResolveMoveDirection(player, camera, inputX, inputY);
                    if (MATH::Length(inputDirection) > 1e-5f) {
                        const MATH::Vec3 targetVelocity = inputDirection * moveSpeed;
                        const bool speedChanged =
                            std::abs(player.GetRuntimeAppliedMoveSpeed() - moveSpeed) > 0.001f;
                        const float velocityLength = MATH::Length({ velocity.x, 0.0f, velocity.z });
                        const bool reversing =
                            velocityLength > 0.02f &&
                            MATH::Dot(FlattenAndNormalize(velocity), inputDirection) < -0.25f;

                        if (speedChanged || reversing) {
                            velocity = targetVelocity;
                        } else {
                            velocity = MoveVectorTowards(velocity, targetVelocity, player.GetAcceleration() * dt);
                        }
                        state = PlayerMovementState::Moving;
                    } else {
                        velocity = MoveVectorTowards(velocity, {}, player.GetDeceleration() * dt);
                        state = MATH::Length(velocity) > 0.02f ? PlayerMovementState::Moving : PlayerMovementState::Idle;
                    }

                    object.Transform().position = object.Transform().position + velocity * dt;
                    if (player.GetUseBounds()) {
                        object.Transform().position.x =
                            Clamp(object.Transform().position.x, player.GetMinX(), player.GetMaxX());
                        object.Transform().position.z =
                            Clamp(object.Transform().position.z, player.GetMinZ(), player.GetMaxZ());
                    }

                    if (player.GetRotateToMove() && state == PlayerMovementState::Moving) {
                        const MATH::Vec3 facingDirection = MATH::Normalize({ velocity.x, 0.0f, velocity.z });
                        UpdateRotationTowards(object, facingDirection, player.GetTurnSpeed(), dt);
                    }
                } else {
                    velocity = MoveVectorTowards(velocity, {}, player.GetDeceleration() * dt);
                }

                player.SetRuntimeState(state, velocity, moveSpeed);
                ApplyPlayerAnimation(object, player, state);
            });
    }

} // namespace HIKARI
