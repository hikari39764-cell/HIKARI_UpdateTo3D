#include "Physics/HIKARI_PhysicsSystem.h"

#include <utility>

#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Physics/HIKARI_PhysicsSceneBridge.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::PHYSICS {
    namespace {
        MATH::Vec3 Lerp(
            const MATH::Vec3& from,
            const MATH::Vec3& to,
            float alpha) noexcept {
            return from * (1.0f - alpha) + to * alpha;
        }

        MATH::Quat NlerpShortest(
            const MATH::Quat& from,
            MATH::Quat to,
            float alpha) noexcept {
            const float dot = from.x * to.x + from.y * to.y +
                from.z * to.z + from.w * to.w;
            if (dot < 0.0f) {
                to.x = -to.x;
                to.y = -to.y;
                to.z = -to.z;
                to.w = -to.w;
            }
            MATH::Quat blended{};
            blended.x = from.x * (1.0f - alpha) + to.x * alpha;
            blended.y = from.y * (1.0f - alpha) + to.y * alpha;
            blended.z = from.z * (1.0f - alpha) + to.z * alpha;
            blended.w = from.w * (1.0f - alpha) + to.w * alpha;
            return MATH::NormalizeQ(blended);
        }
    }

    void PhysicsSystem::PushSceneDrivenPoses(World& world) {
        if (service_ == nullptr) {
            return;
        }
        for (auto& [_, binding] : bindings_) {
            if (binding.effectiveMotionType ==
                    PhysicsMotionType::Dynamic ||
                !binding.body.IsValid()) {
                continue;
            }
            const GameObject* object = world.FindObject(binding.object);
            if (object == nullptr) {
                continue;
            }
            PhysicsPose pose{};
            MATH::Vec3 scale{};
            if (!TryGetPhysicsWorldPoseAndScale(
                    *object,
                    pose,
                    scale) ||
                (binding.hasLastPushedPose &&
                    ArePhysicsPosesNearlyEqual(
                        binding.lastPushedPose,
                        pose))) {
                continue;
            }
            const bool pushed = binding.effectiveMotionType ==
                    PhysicsMotionType::Kinematic
                ? service_->SetKinematicTarget(binding.body, pose)
                : service_->SetBodyPose(binding.body, pose, false);
            if (pushed) {
                binding.lastPushedPose = pose;
                binding.hasLastPushedPose = true;
            }
        }
    }

    void PhysicsSystem::PullDynamicPoses(World& world) {
        if (service_ == nullptr) {
            return;
        }
        for (auto& [_, binding] : bindings_) {
            if (binding.effectiveMotionType !=
                    PhysicsMotionType::Dynamic ||
                !binding.body.IsValid()) {
                continue;
            }
            GameObject* object = world.FindObject(binding.object);
            PhysicsBodyState state{};
            if (object == nullptr ||
                !service_->TryGetBodyState(binding.body, state)) {
                continue;
            }
            if (binding.hasFixedState) {
                binding.previousFixedState = binding.currentFixedState;
            } else {
                binding.previousFixedState = state;
            }
            binding.currentFixedState = state;
            binding.hasFixedState = true;
            (void)ApplyPhysicsWorldPose(*object, state.pose);

            if (runtimeStatus_ != nullptr) {
                const PhysicsBodyRuntimeStatus* existing =
                    runtimeStatus_->FindBodyStatus(binding.object);
                if (existing != nullptr &&
                    existing->awake != state.awake) {
                    PhysicsBodyRuntimeStatus updated = *existing;
                    updated.awake = state.awake;
                    runtimeStatus_->SetBodyStatus(std::move(updated));
                }
            }
        }
    }

    void PhysicsSystem::UpdatePresentationPoses(
        World& world,
        float interpolationAlpha) {
        if (presentationTransforms_ == nullptr) {
            return;
        }
        for (auto& [_, binding] : bindings_) {
            if (binding.effectiveMotionType !=
                    PhysicsMotionType::Dynamic ||
                !binding.hasFixedState) {
                presentationTransforms_->Remove(binding.object);
                continue;
            }
            GameObject* object = world.FindObject(binding.object);
            if (object == nullptr) {
                presentationTransforms_->Remove(binding.object);
                continue;
            }

            PhysicsPose presented{};
            presented.position = Lerp(
                binding.previousFixedState.pose.position,
                binding.currentFixedState.pose.position,
                interpolationAlpha);
            presented.rotation = NlerpShortest(
                binding.previousFixedState.pose.rotation,
                binding.currentFixedState.pose.rotation,
                interpolationAlpha);
            PhysicsPose authoritativePose{};
            MATH::Vec3 worldScale{};
            if (!TryGetPhysicsWorldPoseAndScale(
                    *object,
                    authoritativePose,
                    worldScale)) {
                presentationTransforms_->Remove(binding.object);
                continue;
            }
            presentationTransforms_->SetWorldMatrix(
                binding.object,
                MATH::Mat4::TRS(
                    presented.position,
                    presented.rotation,
                    worldScale));
            object->MarkRenderStateDirty();
        }
    }

} // namespace HIKARI::PHYSICS
