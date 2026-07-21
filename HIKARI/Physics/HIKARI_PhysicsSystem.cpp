#include "Physics/HIKARI_PhysicsSystem.h"

#include <string>
#include <unordered_set>
#include <utility>

#include "Core/HIKARI_FrameContext.h"
#include "Core/HIKARI_Logger.h"
#include "Physics/HIKARI_PhysicsSceneBridge.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::PHYSICS {

    PhysicsSystem::PhysicsSystem(PhysicsWorldSettings settings)
        : settings_(settings) {
    }

    void PhysicsSystem::OnWorldAttached(World& world) {
        service_ = world.Services().Find<PhysicsWorldService>();
        runtimePlayState_ =
            world.Services().Find<RuntimePlayStateService>();
        if (service_ == nullptr) {
            HIKARI_LOG_ERROR(
                "[Physics] PhysicsWorldService is not registered");
            return;
        }
        if (!service_->Configure(settings_) ||
            !service_->AttachWorld(world)) {
            HIKARI_LOG_ERROR(
                "[Physics] failed to attach physics world service");
            service_ = nullptr;
            return;
        }
        ReconcileBodies(world);
        HIKARI_LOG_INFO(
            "[Physics] world attached backend=" +
            std::string(service_->GetBackendName()));
    }

    void PhysicsSystem::OnWorldDetached(World&) {
        DestroyBindings();
        if (service_ != nullptr) {
            service_->DetachWorld();
        }
        service_ = nullptr;
        runtimePlayState_ = nullptr;
    }

    void PhysicsSystem::PreFixedUpdate(
        World& world,
        const FrameContext&) {
        if (!ShouldSimulate()) {
            return;
        }
        ReconcileBodies(world);
        PushSceneDrivenPoses(world);
    }

    void PhysicsSystem::FixedUpdate(
        World& world,
        const FrameContext& frame) {
        if (!ShouldSimulate() || service_ == nullptr) {
            return;
        }
        service_->Step(frame.fixedDt);
        for (PhysicsContactEvent& event :
                service_->ConsumeContactEvents()) {
            world.FixedEvents().Publish(std::move(event));
        }
    }

    void PhysicsSystem::PostFixedUpdate(
        World& world,
        const FrameContext&) {
        if (ShouldSimulate()) {
            PullDynamicPoses(world);
        }
    }

    bool PhysicsSystem::ShouldSimulate() const noexcept {
        if (service_ == nullptr || !service_->IsWorldAttached()) {
            return false;
        }
#if defined(HIKARI_WITH_EDITOR)
        return runtimePlayState_ != nullptr &&
            runtimePlayState_->IsActive();
#else
        return true;
#endif
    }

    void PhysicsSystem::ReconcileBodies(World& world) {
        if (service_ == nullptr) {
            return;
        }
        std::unordered_set<uint64_t> liveObjects;
        for (const auto& objectOwner : world.GetObjects()) {
            if (!objectOwner) {
                continue;
            }
            GameObject& object = *objectOwner;
            PhysicsBodyCreateInfo createInfo{};
            MATH::Vec3 worldScale{};
            if (!BuildPhysicsBodyCreateInfo(
                    object,
                    createInfo,
                    worldScale)) {
                continue;
            }

            const uint64_t objectKey =
                object.GetRuntimeHandle().ToValue();
            liveObjects.insert(objectKey);
            const uint64_t signature =
                ComputePhysicsBodyDefinitionSignature(
                    createInfo,
                    worldScale);
            auto found = bindings_.find(objectKey);
            if (found != bindings_.end() &&
                found->second.definitionSignature == signature) {
                continue;
            }
            if (found != bindings_.end()) {
                (void)service_->DestroyBody(found->second.body);
                bindings_.erase(found);
            }

            BodyBinding binding{};
            binding.object = object.GetRuntimeHandle();
            binding.motionType = createInfo.body.motionType;
            binding.definitionSignature = signature;
            binding.body = service_->CreateBody(createInfo);
            binding.lastPushedPose = createInfo.initialPose;
            binding.hasLastPushedPose = true;
            bindings_.emplace(objectKey, binding);
        }

        for (auto it = bindings_.begin(); it != bindings_.end();) {
            if (liveObjects.find(it->first) != liveObjects.end()) {
                ++it;
                continue;
            }
            (void)service_->DestroyBody(it->second.body);
            it = bindings_.erase(it);
        }
    }

    void PhysicsSystem::DestroyBindings() noexcept {
        if (service_ != nullptr) {
            for (const auto& [_, binding] : bindings_) {
                (void)service_->DestroyBody(binding.body);
            }
        }
        bindings_.clear();
    }

    void PhysicsSystem::PushSceneDrivenPoses(World& world) {
        if (service_ == nullptr) {
            return;
        }
        for (auto& [_, binding] : bindings_) {
            if (binding.motionType == PhysicsMotionType::Dynamic ||
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
            const bool pushed = binding.motionType ==
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
        for (const auto& [_, binding] : bindings_) {
            if (binding.motionType != PhysicsMotionType::Dynamic ||
                !binding.body.IsValid()) {
                continue;
            }
            GameObject* object = world.FindObject(binding.object);
            PhysicsBodyState state{};
            if (object != nullptr &&
                service_->TryGetBodyState(binding.body, state)) {
                (void)ApplyPhysicsWorldPose(*object, state.pose);
            }
        }
    }

} // namespace HIKARI::PHYSICS
