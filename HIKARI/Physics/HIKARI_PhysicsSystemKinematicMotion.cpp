#include "Physics/HIKARI_PhysicsSystem.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "Core/HIKARI_FrameContext.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Physics/HIKARI_PhysicsSceneBridge.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::PHYSICS {
    namespace {
        KinematicMotionState ToMotionState(
            const PhysicsCharacterState& source) noexcept {
            KinematicMotionState result{};
            result.pose = source.pose;
            result.velocity = source.linearVelocity;
            result.groundVelocity = source.groundVelocity;
            result.groundPosition = source.groundPosition;
            result.groundNormal = source.groundNormal;
            result.groundObject = source.groundObject;
            result.groundState = source.groundState;
            result.hitWall = source.hitWall;
            result.hitCeiling = source.hitCeiling;
            result.maximumHitsExceeded = source.maxHitsExceeded;
            return result;
        }

    }

    void PhysicsSystem::ProcessKinematicMotions(
        World& world,
        const FrameContext& frame) {
        if (service_ == nullptr || kinematicMotion_ == nullptr ||
            frame.fixedDt <= 0.0f) {
            return;
        }

        for (auto& [_, binding] : bindings_) {
            KinematicMotionRequest request{};
            const bool hasMove = kinematicMotion_->ConsumeMove(
                binding.object,
                frame.fixedTickIndex,
                request);
            KinematicTeleportRequest teleport{};
            const bool hasTeleport = kinematicMotion_->ConsumeTeleport(
                binding.object,
                teleport);
            if (!hasMove && !hasTeleport) {
                continue;
            }

            const auto setError = [this, &binding](std::string message) {
                kinematicMotion_->SetStatus(
                    binding.object,
                    {
                        KinematicMotionRuntimeHealth::Error,
                        std::move(message)
                    });
            };

            if (binding.effectiveMotionType !=
                    PhysicsMotionType::Kinematic) {
                setError(
                    "Physics Body must use Kinematic motion for controller-driven movement");
                continue;
            }
            if (!service_->GetCapabilities().virtualCharacters) {
                setError(
                    "the active physics backend has no kinematic character solver");
                continue;
            }

            GameObject* object = world.FindObject(binding.object);
            if (object == nullptr || !binding.body.IsValid()) {
                setError("physics body or scene object is unavailable");
                continue;
            }

            if (hasTeleport) {
                if (!service_->SetBodyPose(
                        binding.body,
                        teleport.pose,
                        false) ||
                    !ApplyPhysicsWorldPose(*object, teleport.pose)) {
                    setError("unable to teleport the kinematic body");
                    continue;
                }
                binding.lastPushedPose = teleport.pose;
                binding.hasLastPushedPose = true;
                if (binding.kinematicSolver.IsValid()) {
                    (void)service_->SetCharacterPose(
                        binding.kinematicSolver,
                        teleport.pose);
                    if (teleport.clearVelocity) {
                        (void)service_->SetCharacterVelocity(
                            binding.kinematicSolver,
                            {});
                    }
                }
            }
            if (!hasMove) {
                continue;
            }

            PhysicsBodyState bodyState{};
            if (!service_->TryGetBodyState(binding.body, bodyState)) {
                setError("unable to read the kinematic physics body");
                continue;
            }
            if (hasTeleport) {
                bodyState.pose = teleport.pose;
                if (teleport.clearVelocity) {
                    bodyState.linearVelocity = {};
                    bodyState.angularVelocity = {};
                }
            }

            bool retainedPreviousSolver = false;
            std::string solverError{};
            if (!EnsureKinematicSolver(
                    binding,
                    request.controller,
                    bodyState,
                    retainedPreviousSolver,
                    solverError)) {
                setError(std::move(solverError));
                continue;
            }

            if (!service_->SetCharacterPose(
                    binding.kinematicSolver,
                    bodyState.pose)) {
                setError("unable to synchronize the kinematic motion solver");
                continue;
            }
            if (hasTeleport && teleport.clearVelocity) {
                (void)service_->SetCharacterVelocity(
                    binding.kinematicSolver,
                    {});
            }
            (void)service_->RefreshCharacterContacts(
                binding.kinematicSolver);
            (void)service_->RefreshCharacterGroundVelocity(
                binding.kinematicSolver);

            PhysicsCharacterState before{};
            if (!service_->TryGetCharacterState(
                    binding.kinematicSolver,
                    before)) {
                setError("unable to read the kinematic motion state");
                continue;
            }

            if (request.hasDesiredRotation) {
                PhysicsPose solverPose = before.pose;
                solverPose.rotation = request.desiredRotation;
                if (!service_->SetCharacterPose(
                        binding.kinematicSolver,
                        solverPose)) {
                    setError("unable to rotate the kinematic motion solver");
                    continue;
                }
                before.pose.rotation = request.desiredRotation;
            }

            const MATH::Vec3 gravity = settings_.gravity *
                binding.bodyDesc.gravityScale;
            const bool onGround = before.IsGrounded();
            const float relativeVertical =
                before.linearVelocity.y - before.groundVelocity.y;
            MATH::Vec3 velocity{};
            if (onGround && relativeVertical < 0.1f) {
                velocity = before.groundVelocity +
                    request.horizontalVelocity;
            } else {
                velocity = request.horizontalVelocity;
                velocity.y = before.linearVelocity.y;
            }
            const bool canApplyJump = onGround ||
                request.allowJumpWithoutGroundContact;
            if (request.jumpRequested && canApplyJump &&
                request.jumpSpeed > 0.0f) {
                velocity.y = request.jumpSpeed +
                    (onGround ? before.groundVelocity.y : 0.0f);
            }
            velocity = velocity + gravity * frame.fixedDt;
            if (request.maximumFallSpeed > 0.0f) {
                velocity.y = (std::max)(
                    velocity.y,
                    -request.maximumFallSpeed);
            }
            velocity = velocity +
                kinematicMotion_->ConsumeExternalVelocity(
                    binding.object);

            if (!service_->SetCharacterVelocity(
                    binding.kinematicSolver,
                    velocity)) {
                setError("unable to update kinematic motion velocity");
                continue;
            }
            PhysicsCharacterStepSettings step{};
            step.gravity = gravity;
            step.stepUpHeight = request.controller.stepUpHeight;
            step.stickToFloorDistance =
                request.controller.stickToFloorDistance;
            step.stepForwardTestDistance =
                request.controller.stepForwardTestDistance;
            if (!service_->StepCharacter(
                    binding.kinematicSolver,
                    frame.fixedDt,
                    step)) {
                setError("kinematic motion solve failed");
                continue;
            }

            PhysicsCharacterState after{};
            if (!service_->TryGetCharacterState(
                    binding.kinematicSolver,
                    after)) {
                setError("unable to read the kinematic motion result");
                continue;
            }
            if (!service_->SetKinematicTarget(
                    binding.body,
                    after.pose) ||
                !ApplyPhysicsWorldPose(*object, after.pose)) {
                setError("unable to apply the solved kinematic body pose");
                continue;
            }

            binding.lastPushedPose = after.pose;
            binding.hasLastPushedPose = true;
            binding.previousFixedState = binding.hasFixedState
                ? binding.currentFixedState
                : bodyState;
            binding.currentFixedState = bodyState;
            binding.currentFixedState.pose = after.pose;
            binding.currentFixedState.linearVelocity =
                after.linearVelocity;
            binding.currentFixedState.awake = true;
            binding.hasFixedState = true;

            kinematicMotion_->SetState(
                binding.object,
                ToMotionState(after));
            kinematicMotion_->SetStatus(
                binding.object,
                {
                    retainedPreviousSolver
                        ? KinematicMotionRuntimeHealth::RetainedPrevious
                        : KinematicMotionRuntimeHealth::Ready,
                    retainedPreviousSolver
                        ? "previous kinematic solver retained"
                        : "kinematic motion ready"
                });

            const bool wasGrounded = before.IsGrounded();
            const bool isGrounded = after.IsGrounded();
            if (!wasGrounded && isGrounded) {
                world.FixedEvents().Publish(KinematicBodyLandedEvent{
                    binding.object,
                    after.groundObject,
                    after.groundPosition,
                    after.groundNormal,
                    (std::max)(
                        0.0f,
                        -(before.linearVelocity.y -
                            after.groundVelocity.y))
                });
            } else if (wasGrounded && !isGrounded) {
                world.FixedEvents().Publish(
                    KinematicBodyLeftGroundEvent{
                        binding.object,
                        before.groundObject
                    });
            }
        }
    }

} // namespace HIKARI::PHYSICS
