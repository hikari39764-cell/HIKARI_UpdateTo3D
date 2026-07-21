#include "Physics/HIKARI_PhysicsWorldService.h"

#include <algorithm>
#include <utility>

namespace HIKARI::PHYSICS {

    PhysicsWorldService::~PhysicsWorldService() {
        DetachWorld();
    }

    bool PhysicsWorldService::InstallBackend(
        std::unique_ptr<IPhysicsWorldBackend> backend) {

        if (!backend || world_ != nullptr) {
            return false;
        }
        backend_ = std::move(backend);
        return true;
    }

    std::unique_ptr<IPhysicsWorldBackend>
        PhysicsWorldService::RemoveBackend() {

        if (world_ != nullptr) {
            return nullptr;
        }
        return std::move(backend_);
    }

    bool PhysicsWorldService::Configure(
        const PhysicsWorldSettings& settings) {

        if (world_ != nullptr) {
            return false;
        }
        settings_ = settings;
        return true;
    }

    const PhysicsWorldSettings&
        PhysicsWorldService::GetSettings() const noexcept {
        return settings_;
    }

    bool PhysicsWorldService::AttachWorld(World& world) {
        if (world_ == &world) {
            return true;
        }
        if (world_ != nullptr) {
            return false;
        }
        if (backend_ && !backend_->Initialize(settings_)) {
            return false;
        }
        world_ = &world;
        contactEvents_.clear();
        return true;
    }

    void PhysicsWorldService::DetachWorld() noexcept {
        if (world_ == nullptr) {
            return;
        }
        if (backend_) {
            backend_->Shutdown();
        }
        world_ = nullptr;
        contactEvents_.clear();
    }

    bool PhysicsWorldService::HasBackend() const noexcept {
        return backend_ != nullptr;
    }

    bool PhysicsWorldService::IsWorldAttached() const noexcept {
        return world_ != nullptr;
    }

    std::string_view PhysicsWorldService::GetBackendName() const noexcept {
        return backend_ ? backend_->GetBackendName() : "None";
    }

    PhysicsBackendCapabilities
        PhysicsWorldService::GetCapabilities() const noexcept {
        return backend_ ? backend_->GetCapabilities()
            : PhysicsBackendCapabilities{};
    }

    PhysicsBodyCreateResult PhysicsWorldService::CreateBody(
        const PhysicsBodyCreateInfo& createInfo) {
        if (!backend_ || world_ == nullptr) {
            PhysicsBodyCreateResult result{};
            result.error = PhysicsErrorCode::WorldUnavailable;
            result.message = "physics world is not available";
            result.recoverable = true;
            return result;
        }
        return backend_->CreateBody(createInfo);
    }

    bool PhysicsWorldService::DestroyBody(PhysicsBodyHandle body) {
        return backend_ && world_ != nullptr && body.IsValid() &&
            backend_->DestroyBody(body);
    }

    bool PhysicsWorldService::SetBodyPose(
        PhysicsBodyHandle body,
        const PhysicsPose& pose,
        bool activate) {
        return backend_ && world_ != nullptr && body.IsValid() &&
            backend_->SetBodyPose(body, pose, activate);
    }

    bool PhysicsWorldService::SetKinematicTarget(
        PhysicsBodyHandle body,
        const PhysicsPose& pose) {
        return backend_ && world_ != nullptr && body.IsValid() &&
            backend_->SetKinematicTarget(body, pose);
    }

    bool PhysicsWorldService::SetBodyVelocity(
        PhysicsBodyHandle body,
        const MATH::Vec3& linearVelocity,
        const MATH::Vec3& angularVelocity) {
        return backend_ && world_ != nullptr && body.IsValid() &&
            backend_->SetBodyVelocity(
                body,
                linearVelocity,
                angularVelocity);
    }

    bool PhysicsWorldService::TryGetBodyState(
        PhysicsBodyHandle body,
        PhysicsBodyState& outState) const {
        return backend_ && world_ != nullptr && body.IsValid() &&
            backend_->TryGetBodyState(body, outState);
    }

    PhysicsStepResult PhysicsWorldService::Step(
        float fixedDeltaSeconds) {
        if (!backend_ || world_ == nullptr ||
            fixedDeltaSeconds <= 0.0f) {
            PhysicsStepResult result{};
            result.error = PhysicsErrorCode::WorldUnavailable;
            result.message = "physics world is not available";
            return result;
        }
        PhysicsStepResult result = backend_->Step(fixedDeltaSeconds);
        backend_->DrainContactEvents(contactEvents_);
        if (!settings_.emitPersistContactEvents) {
            contactEvents_.erase(
                std::remove_if(
                    contactEvents_.begin(),
                    contactEvents_.end(),
                    [](const PhysicsContactEvent& event) {
                        return event.phase ==
                            PhysicsContactPhase::Persist;
                    }),
                contactEvents_.end());
        }
        return result;
    }

    PhysicsBackendStatistics
        PhysicsWorldService::GetStatistics() const noexcept {
        return backend_ && world_ != nullptr
            ? backend_->GetStatistics()
            : PhysicsBackendStatistics{};
    }

    std::vector<PhysicsContactEvent>
        PhysicsWorldService::ConsumeContactEvents() {
        std::vector<PhysicsContactEvent> events;
        events.swap(contactEvents_);
        return events;
    }

    bool PhysicsWorldService::Raycast(
        const PhysicsRaycastQuery& query,
        PhysicsHit& outHit) const {
        return backend_ && world_ != nullptr &&
            backend_->GetCapabilities().raycast &&
            backend_->Raycast(query, outHit);
    }

    bool PhysicsWorldService::ShapeCast(
        const PhysicsShapeCastQuery& query,
        PhysicsHit& outHit) const {
        return backend_ && world_ != nullptr &&
            backend_->GetCapabilities().shapeCast &&
            backend_->ShapeCast(query, outHit);
    }

    std::vector<PhysicsHit> PhysicsWorldService::Overlap(
        const PhysicsOverlapQuery& query) const {
        std::vector<PhysicsHit> hits;
        if (backend_ && world_ != nullptr &&
            backend_->GetCapabilities().overlap) {
            backend_->Overlap(query, hits);
        }
        return hits;
    }

} // namespace HIKARI::PHYSICS
