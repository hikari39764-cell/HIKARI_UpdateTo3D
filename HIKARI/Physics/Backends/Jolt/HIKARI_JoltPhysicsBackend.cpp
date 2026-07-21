#include "Physics/Backends/Jolt/HIKARI_JoltPhysicsBackendInternal.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <utility>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Physics/Body/AllowedDOFs.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/RegisterTypes.h>

#include "Physics/Backends/Jolt/HIKARI_JoltConversions.h"
#include "Physics/Backends/Jolt/HIKARI_JoltShapeFactory.h"

namespace HIKARI::PHYSICS::JOLT_BACKEND {
    namespace {

        constexpr JPH::ObjectLayer kStaticLayer = 0;
        constexpr JPH::ObjectLayer kMovingLayer = 1;
        constexpr JPH::BroadPhaseLayer kStaticBroadPhaseLayer{ 0 };
        constexpr JPH::BroadPhaseLayer kMovingBroadPhaseLayer{ 1 };
        constexpr uint32_t kMaximumBodies = 65536;
        constexpr uint32_t kMaximumBodyPairs = 65536;
        constexpr uint32_t kMaximumContactConstraints = 16384;
        constexpr size_t kTemporaryAllocatorBytes = 32u * 1024u * 1024u;

        class BroadPhaseLayers final
            : public JPH::BroadPhaseLayerInterface {
        public:
            JPH::uint GetNumBroadPhaseLayers() const override {
                return 2;
            }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(
                JPH::ObjectLayer layer) const override {
                return layer == kStaticLayer
                    ? kStaticBroadPhaseLayer
                    : kMovingBroadPhaseLayer;
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(
                JPH::BroadPhaseLayer layer) const override {
                return layer == kStaticBroadPhaseLayer
                    ? "Static"
                    : "Moving";
            }
#endif
        };

        class ObjectVsBroadPhase final
            : public JPH::ObjectVsBroadPhaseLayerFilter {
        public:
            bool ShouldCollide(
                JPH::ObjectLayer layer,
                JPH::BroadPhaseLayer broadPhaseLayer) const override {
                return layer != kStaticLayer ||
                    broadPhaseLayer == kMovingBroadPhaseLayer;
            }
        };

        class ObjectLayerPairs final
            : public JPH::ObjectLayerPairFilter {
        public:
            bool ShouldCollide(
                JPH::ObjectLayer layer1,
                JPH::ObjectLayer layer2) const override {
                return layer1 != kStaticLayer || layer2 != kStaticLayer;
            }
        };

        BroadPhaseLayers gBroadPhaseLayers{};
        ObjectVsBroadPhase gObjectVsBroadPhase{};
        ObjectLayerPairs gObjectLayerPairs{};
        std::mutex gJoltLifetimeMutex{};
        uint32_t gJoltWorldCount = 0;

        bool AcquireJoltRuntime() {
            std::scoped_lock lock(gJoltLifetimeMutex);
            if (gJoltWorldCount == 0) {
                JPH::RegisterDefaultAllocator();
                JPH::Factory::sInstance = new JPH::Factory();
                JPH::RegisterTypes();
            }
            ++gJoltWorldCount;
            return true;
        }

        void ReleaseJoltRuntime() noexcept {
            std::scoped_lock lock(gJoltLifetimeMutex);
            if (gJoltWorldCount == 0 || --gJoltWorldCount != 0) {
                return;
            }
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }

        JPH::EMotionType ToJoltMotionType(
            PhysicsMotionType motionType) noexcept {
            switch (motionType) {
            case PhysicsMotionType::Static:
                return JPH::EMotionType::Static;
            case PhysicsMotionType::Kinematic:
                return JPH::EMotionType::Kinematic;
            case PhysicsMotionType::Dynamic:
                return JPH::EMotionType::Dynamic;
            }
            return JPH::EMotionType::Static;
        }

        JPH::EAllowedDOFs BuildAllowedDofs(
            const PhysicsBodyDesc& body) noexcept {
            JPH::EAllowedDOFs allowed = JPH::EAllowedDOFs::All;
            if (body.lockTranslationX) {
                allowed &= ~JPH::EAllowedDOFs::TranslationX;
            }
            if (body.lockTranslationY) {
                allowed &= ~JPH::EAllowedDOFs::TranslationY;
            }
            if (body.lockTranslationZ) {
                allowed &= ~JPH::EAllowedDOFs::TranslationZ;
            }
            if (body.lockRotationX) {
                allowed &= ~JPH::EAllowedDOFs::RotationX;
            }
            if (body.lockRotationY) {
                allowed &= ~JPH::EAllowedDOFs::RotationY;
            }
            if (body.lockRotationZ) {
                allowed &= ~JPH::EAllowedDOFs::RotationZ;
            }
            return allowed;
        }

        PhysicsBodyHandle HandleFromUserData(uint64_t value) noexcept {
            PhysicsBodyHandle result{};
            result.slot = static_cast<uint32_t>(value & 0xFFFFFFFFu);
            result.generation = static_cast<uint32_t>(value >> 32u);
            return result;
        }

    } // namespace

    JoltPhysicsBackend::JoltPhysicsBackend() = default;

    JoltPhysicsBackend::~JoltPhysicsBackend() {
        Shutdown();
    }

    std::string_view JoltPhysicsBackend::GetBackendName() const noexcept {
        return "Jolt Physics";
    }

    PhysicsBackendCapabilities
        JoltPhysicsBackend::GetCapabilities() const noexcept {
        return {
            .raycast = true,
            .shapeCast = true,
            .overlap = true,
            .continuousCollision = true,
            .triggerEvents = true,
        };
    }

    bool JoltPhysicsBackend::Initialize(
        const PhysicsWorldSettings& settings) {
        if (initialized_) {
            return true;
        }
        if (!AcquireJoltRuntime()) {
            return false;
        }

        settings_ = settings;
        tempAllocator_ = std::make_unique<JPH::TempAllocatorImpl>(
            kTemporaryAllocatorBytes);
        const unsigned int hardwareThreads =
            (std::max)(1u, std::thread::hardware_concurrency());
        const int workerThreads = static_cast<int>(
            hardwareThreads > 1u ? hardwareThreads - 1u : 1u);
        jobSystem_ = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs,
            JPH::cMaxPhysicsBarriers,
            workerThreads);
        physicsSystem_ = std::make_unique<JPH::PhysicsSystem>();
        physicsSystem_->Init(
            kMaximumBodies,
            0,
            kMaximumBodyPairs,
            kMaximumContactConstraints,
            gBroadPhaseLayers,
            gObjectVsBroadPhase,
            gObjectLayerPairs);
        physicsSystem_->SetGravity(ToJolt(settings.gravity));
        contactListener_ = std::make_unique<ContactListener>(*this);
        physicsSystem_->SetContactListener(contactListener_.get());
        initialized_ = true;
        return true;
    }

    void JoltPhysicsBackend::Shutdown() noexcept {
        if (!initialized_) {
            return;
        }

        if (physicsSystem_) {
            physicsSystem_->SetContactListener(nullptr);
            JPH::BodyInterface& bodyInterface =
                physicsSystem_->GetBodyInterface();
            std::scoped_lock lock(recordsMutex_);
            for (BodyRecord& record : bodyRecords_) {
                if (!record.occupied) {
                    continue;
                }
                bodyInterface.RemoveBody(record.bodyId);
                bodyInterface.DestroyBody(record.bodyId);
                record.occupied = false;
                record.shapes.clear();
            }
            bodyRecords_.clear();
            freeSlots_.clear();
        }
        {
            std::scoped_lock lock(contactMutex_);
            pendingContactEvents_.clear();
            activeContacts_.clear();
        }

        contactListener_.reset();
        physicsSystem_.reset();
        jobSystem_.reset();
        tempAllocator_.reset();
        initialized_ = false;
        ReleaseJoltRuntime();
    }

    PhysicsBodyHandle JoltPhysicsBackend::AllocateHandle() {
        PhysicsBodyHandle handle{};
        if (!freeSlots_.empty()) {
            handle.slot = freeSlots_.back();
            freeSlots_.pop_back();
            BodyRecord& record = bodyRecords_[handle.slot];
            ++record.generation;
            if (record.generation == 0) {
                record.generation = 1;
            }
            handle.generation = record.generation;
            return handle;
        }

        handle.slot = static_cast<uint32_t>(bodyRecords_.size());
        handle.generation = 1;
        bodyRecords_.push_back(BodyRecord{});
        bodyRecords_.back().generation = handle.generation;
        return handle;
    }

    JoltPhysicsBackend::BodyRecord* JoltPhysicsBackend::FindRecord(
        PhysicsBodyHandle body) noexcept {
        if (!body.IsValid() || body.slot >= bodyRecords_.size()) {
            return nullptr;
        }
        BodyRecord& record = bodyRecords_[body.slot];
        return record.occupied && record.generation == body.generation
            ? &record
            : nullptr;
    }

    const JoltPhysicsBackend::BodyRecord* JoltPhysicsBackend::FindRecord(
        PhysicsBodyHandle body) const noexcept {
        if (!body.IsValid() || body.slot >= bodyRecords_.size()) {
            return nullptr;
        }
        const BodyRecord& record = bodyRecords_[body.slot];
        return record.occupied && record.generation == body.generation
            ? &record
            : nullptr;
    }

    const JoltPhysicsBackend::BodyRecord*
        JoltPhysicsBackend::FindRecordFromUserData(uint64_t value) const noexcept {
        return FindRecord(HandleFromUserData(value));
    }

    PhysicsBodyHandle JoltPhysicsBackend::CreateBody(
        const PhysicsBodyCreateInfo& createInfo) {
        if (!initialized_ || !physicsSystem_ || createInfo.shapes.empty()) {
            return {};
        }
        if (createInfo.body.motionType != PhysicsMotionType::Static &&
            std::any_of(
                createInfo.shapes.begin(),
                createInfo.shapes.end(),
                [](const PhysicsShapeDesc& candidate) {
                    return candidate.type == PhysicsShapeType::TriangleMesh;
                })) {
            return {};
        }

        JPH::ShapeRefC shape = BuildCompoundShape(createInfo.shapes);
        if (!shape) {
            return {};
        }

        PhysicsBodyHandle handle{};
        {
            std::scoped_lock lock(recordsMutex_);
            handle = AllocateHandle();
        }

        JPH::EMotionType motionType = ToJoltMotionType(
            createInfo.body.motionType);
        const JPH::EAllowedDOFs allowedDofs = BuildAllowedDofs(
            createInfo.body);
        if (motionType == JPH::EMotionType::Dynamic &&
            allowedDofs == JPH::EAllowedDOFs::None) {
            motionType = JPH::EMotionType::Kinematic;
        }
        const JPH::ObjectLayer layer =
            motionType == JPH::EMotionType::Static
            ? kStaticLayer
            : kMovingLayer;
        JPH::BodyCreationSettings settings(
            shape.GetPtr(),
            ToJoltPosition(createInfo.initialPose.position),
            ToJolt(createInfo.initialPose.rotation),
            motionType,
            layer);
        settings.mUserData = handle.ToValue();
        settings.mAllowedDOFs = allowedDofs == JPH::EAllowedDOFs::None
            ? JPH::EAllowedDOFs::All
            : allowedDofs;
        settings.mAllowSleeping = createInfo.body.allowSleeping &&
            settings_.allowSleeping;
        settings.mLinearDamping = (std::max)(
            0.0f,
            createInfo.body.linearDamping);
        settings.mAngularDamping = (std::max)(
            0.0f,
            createInfo.body.angularDamping);
        settings.mGravityFactor = createInfo.body.gravityScale;
        settings.mMotionQuality = createInfo.body.continuousCollision
            ? JPH::EMotionQuality::LinearCast
            : JPH::EMotionQuality::Discrete;
        settings.mCollideKinematicVsNonDynamic =
            motionType == JPH::EMotionType::Kinematic;
        settings.mFriction = std::clamp(
            createInfo.shapes.front().material.friction,
            0.0f,
            1.0f);
        settings.mRestitution = std::clamp(
            createInfo.shapes.front().material.restitution,
            0.0f,
            1.0f);
        if (motionType == JPH::EMotionType::Dynamic) {
            settings.mOverrideMassProperties =
                JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = (std::max)(
                0.001f,
                createInfo.body.mass);
            settings.mLinearVelocity = ToJolt(
                createInfo.initialLinearVelocity);
            settings.mAngularVelocity = ToJolt(
                createInfo.initialAngularVelocity);
        }

        const JPH::BodyID bodyId =
            physicsSystem_->GetBodyInterface().CreateAndAddBody(
                settings,
                motionType == JPH::EMotionType::Dynamic
                    ? JPH::EActivation::Activate
                    : JPH::EActivation::DontActivate);
        if (bodyId.IsInvalid()) {
            std::scoped_lock lock(recordsMutex_);
            freeSlots_.push_back(handle.slot);
            return {};
        }

        std::scoped_lock lock(recordsMutex_);
        BodyRecord& record = bodyRecords_[handle.slot];
        record.occupied = true;
        record.bodyId = bodyId;
        record.object = createInfo.body.object;
        record.motionType = createInfo.body.motionType;
        record.shapes = createInfo.shapes;
        record.hasPendingKinematicTarget = false;
        return handle;
    }

    bool JoltPhysicsBackend::DestroyBody(PhysicsBodyHandle body) {
        if (!initialized_ || !physicsSystem_) {
            return false;
        }
        JPH::BodyID bodyId{};
        {
            std::scoped_lock lock(recordsMutex_);
            BodyRecord* record = FindRecord(body);
            if (!record) {
                return false;
            }
            bodyId = record->bodyId;
            record->occupied = false;
            record->object = {};
            record->shapes.clear();
            record->hasPendingKinematicTarget = false;
            freeSlots_.push_back(body.slot);
        }
        JPH::BodyInterface& bodyInterface =
            physicsSystem_->GetBodyInterface();
        bodyInterface.RemoveBody(bodyId);
        bodyInterface.DestroyBody(bodyId);
        return true;
    }

    bool JoltPhysicsBackend::SetBodyPose(
        PhysicsBodyHandle body,
        const PhysicsPose& pose,
        bool activate) {
        if (!initialized_ || !physicsSystem_) {
            return false;
        }
        JPH::BodyID bodyId{};
        {
            std::scoped_lock lock(recordsMutex_);
            const BodyRecord* record = FindRecord(body);
            if (!record) {
                return false;
            }
            bodyId = record->bodyId;
        }
        physicsSystem_->GetBodyInterface().SetPositionAndRotation(
            bodyId,
            ToJoltPosition(pose.position),
            ToJolt(pose.rotation),
            activate ? JPH::EActivation::Activate
                : JPH::EActivation::DontActivate);
        return true;
    }

    bool JoltPhysicsBackend::SetKinematicTarget(
        PhysicsBodyHandle body,
        const PhysicsPose& pose) {
        if (!initialized_ || !physicsSystem_) {
            return false;
        }
        std::scoped_lock lock(recordsMutex_);
        BodyRecord* record = FindRecord(body);
        if (!record || record->motionType != PhysicsMotionType::Kinematic) {
            return false;
        }
        record->pendingKinematicTarget = pose;
        record->hasPendingKinematicTarget = true;
        return true;
    }

    bool JoltPhysicsBackend::SetBodyVelocity(
        PhysicsBodyHandle body,
        const MATH::Vec3& linearVelocity,
        const MATH::Vec3& angularVelocity) {
        if (!initialized_ || !physicsSystem_) {
            return false;
        }
        JPH::BodyID bodyId{};
        {
            std::scoped_lock lock(recordsMutex_);
            const BodyRecord* record = FindRecord(body);
            if (!record ||
                record->motionType == PhysicsMotionType::Static) {
                return false;
            }
            bodyId = record->bodyId;
        }
        physicsSystem_->GetBodyInterface().SetLinearAndAngularVelocity(
            bodyId,
            ToJolt(linearVelocity),
            ToJolt(angularVelocity));
        return true;
    }

    bool JoltPhysicsBackend::TryGetBodyState(
        PhysicsBodyHandle body,
        PhysicsBodyState& outState) const {
        if (!initialized_ || !physicsSystem_) {
            return false;
        }
        JPH::BodyID bodyId{};
        {
            std::scoped_lock lock(recordsMutex_);
            const BodyRecord* record = FindRecord(body);
            if (!record) {
                return false;
            }
            bodyId = record->bodyId;
        }
        const JPH::BodyInterface& bodyInterface =
            physicsSystem_->GetBodyInterface();
        JPH::RVec3 position;
        JPH::Quat rotation;
        JPH::Vec3 linearVelocity;
        JPH::Vec3 angularVelocity;
        bodyInterface.GetPositionAndRotation(
            bodyId,
            position,
            rotation);
        bodyInterface.GetLinearAndAngularVelocity(
            bodyId,
            linearVelocity,
            angularVelocity);
        outState.pose.position = FromJoltPosition(position);
        outState.pose.rotation = FromJolt(rotation);
        outState.linearVelocity = FromJolt(linearVelocity);
        outState.angularVelocity = FromJolt(angularVelocity);
        outState.awake = bodyInterface.IsActive(bodyId);
        return true;
    }

    void JoltPhysicsBackend::Step(float fixedDeltaSeconds) {
        if (!initialized_ || !physicsSystem_ ||
            fixedDeltaSeconds <= 0.0f) {
            return;
        }
        std::vector<std::pair<JPH::BodyID, PhysicsPose>> targets{};
        {
            std::scoped_lock lock(recordsMutex_);
            for (BodyRecord& record : bodyRecords_) {
                if (!record.occupied ||
                    !record.hasPendingKinematicTarget) {
                    continue;
                }
                targets.emplace_back(
                    record.bodyId,
                    record.pendingKinematicTarget);
                record.hasPendingKinematicTarget = false;
            }
        }
        JPH::BodyInterface& bodyInterface =
            physicsSystem_->GetBodyInterface();
        for (const auto& [bodyId, pose] : targets) {
            bodyInterface.MoveKinematic(
                bodyId,
                ToJoltPosition(pose.position),
                ToJolt(pose.rotation),
                fixedDeltaSeconds);
        }
        physicsSystem_->Update(
            fixedDeltaSeconds,
            1,
            tempAllocator_.get(),
            jobSystem_.get());
    }

    std::unique_ptr<IPhysicsWorldBackend> CreateJoltPhysicsBackend() {
        return std::make_unique<JoltPhysicsBackend>();
    }

} // namespace HIKARI::PHYSICS::JOLT_BACKEND

namespace HIKARI::PHYSICS {

    std::unique_ptr<IPhysicsWorldBackend> CreateJoltPhysicsBackend() {
        return JOLT_BACKEND::CreateJoltPhysicsBackend();
    }

} // namespace HIKARI::PHYSICS
