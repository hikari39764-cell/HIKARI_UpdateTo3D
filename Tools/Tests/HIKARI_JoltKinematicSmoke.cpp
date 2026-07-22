#include <cmath>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "Assets/Collision/HIKARI_HcollisionFormat.h"
#include "Physics/Backends/Jolt/HIKARI_JoltPhysicsBackend.h"
#include "Physics/Backends/Jolt/HIKARI_JoltShapeFactory.h"
#include "Physics/HIKARI_IPhysicsWorldBackend.h"

namespace {
    using namespace HIKARI;
    using namespace HIKARI::PHYSICS;

    constexpr float kFixedDeltaSeconds = 1.0f / 60.0f;

    RuntimeObjectHandle Object(uint32_t slot) noexcept {
        return { slot, 1u };
    }

    PhysicsShapeDesc MakeBoxFloor() {
        PhysicsShapeDesc shape{};
        shape.key.componentOrdinal = 1u;
        shape.type = PhysicsShapeType::Box;
        shape.halfExtents = { 8.0f, 0.5f, 8.0f };
        shape.filter.layer = 1u;
        shape.filter.mask = 0xFFFFFFFFu;
        return shape;
    }

    PhysicsShapeDesc MakeTriangleFloor() {
        auto geometry = std::make_shared<PhysicsGeometryBuffer>();
        geometry->vertices = {
            { -8.0f, 0.0f, -8.0f },
            { -8.0f, 0.0f, 8.0f },
            { 8.0f, 0.0f, 8.0f },
            { 8.0f, 0.0f, -8.0f },
        };
        geometry->indices = { 0u, 1u, 2u, 0u, 2u, 3u };

        PhysicsShapeDesc shape{};
        shape.key.sourceShapeId = 1u;
        shape.type = PhysicsShapeType::TriangleMesh;
        shape.geometry = std::move(geometry);
        shape.vertexCount = 4u;
        shape.indexCount = 6u;
        shape.filter.layer = 1u;
        shape.filter.mask = 0xFFFFFFFFu;
        return shape;
    }

    PhysicsShapeDesc MakeCharacterCapsule() {
        PhysicsShapeDesc shape{};
        shape.key.componentOrdinal = 1u;
        shape.type = PhysicsShapeType::Capsule;
        shape.localCenter = { 0.0f, 0.9f, 0.0f };
        shape.radius = 0.35f;
        shape.height = 1.8f;
        shape.filter.layer = 8u;
        shape.filter.mask = 0xFFFFFFFFu;
        return shape;
    }

    struct MovingJumpResult {
        PhysicsCharacterState state{};
        float groundedY = 0.0f;
        float groundedX = 0.0f;
        float groundedZ = 0.0f;
        float highestY = 0.0f;
        float lowestFallingSpeed = 0.0f;
        bool leftGroundImmediately = false;
        bool stayedAirborneWhileAscending = true;
        bool reachedApex = false;
        bool landedAfterJump = false;
    };

    bool RunMovingJump(
        IPhysicsWorldBackend& backend,
        PhysicsCharacterHandle character,
        PhysicsBodyHandle body,
        const PhysicsCharacterState& groundedState,
        std::string_view name,
        MovingJumpResult& outResult) {
        constexpr float gravityScale = 2.7f;
        PhysicsCharacterStepSettings step{};
        const MATH::Vec3 jumpGravity = step.gravity * gravityScale;
        const float jumpSpeed = std::sqrt(
            2.0f * std::abs(jumpGravity.y) * 1.2f);

        outResult = {};
        outResult.state = groundedState;
        outResult.groundedY = groundedState.pose.position.y;
        outResult.groundedX = groundedState.pose.position.x;
        outResult.groundedZ = groundedState.pose.position.z;
        outResult.highestY = outResult.groundedY;
        for (uint32_t jumpFrame = 0u; jumpFrame < 90u; ++jumpFrame) {
            PhysicsBodyState bodyState{};
            if (!backend.TryGetBodyState(body, bodyState) ||
                !backend.SetCharacterPose(character, bodyState.pose) ||
                !backend.RefreshCharacterContacts(character) ||
                !backend.RefreshCharacterGroundVelocity(character) ||
                !backend.TryGetCharacterState(
                    character,
                    outResult.state)) {
                std::cerr << name
                    << ": moving jump state update failed\n";
                return false;
            }

            MATH::Vec3 velocity{
                -0.67f,
                outResult.state.linearVelocity.y,
                3.13f
            };
            if (jumpFrame == 0u) {
                velocity.y = jumpSpeed +
                    outResult.state.groundVelocity.y;
            } else if (outResult.state.IsGrounded() &&
                velocity.y - outResult.state.groundVelocity.y < 0.1f) {
                velocity.y = outResult.state.groundVelocity.y;
            }
            velocity = velocity + jumpGravity * kFixedDeltaSeconds;
            PhysicsCharacterStepSettings jumpStep = step;
            jumpStep.gravity = jumpGravity;
            if (!backend.SetCharacterVelocity(character, velocity) ||
                !backend.StepCharacter(
                    character,
                    kFixedDeltaSeconds,
                    jumpStep) ||
                !backend.TryGetCharacterState(
                    character,
                    outResult.state) ||
                !backend.SetKinematicTarget(
                    body,
                    outResult.state.pose) ||
                !backend.Step(kFixedDeltaSeconds).Succeeded()) {
                std::cerr << name << ": moving jump step failed\n";
                return false;
            }
            outResult.highestY = (std::max)(
                outResult.highestY,
                outResult.state.pose.position.y);
            outResult.lowestFallingSpeed = (std::min)(
                outResult.lowestFallingSpeed,
                outResult.state.linearVelocity.y);
            if (jumpFrame == 0u) {
                outResult.leftGroundImmediately =
                    !outResult.state.IsGrounded();
            }
            if (outResult.state.linearVelocity.y > 0.1f &&
                outResult.state.IsGrounded()) {
                outResult.stayedAirborneWhileAscending = false;
            }
            outResult.reachedApex = outResult.reachedApex ||
                outResult.state.linearVelocity.y < -0.1f;
            if (outResult.reachedApex &&
                outResult.state.IsGrounded()) {
                outResult.landedAfterJump = true;
                break;
            }
        }
        return true;
    }

    bool MovingJumpSucceeded(const MovingJumpResult& result) noexcept {
        const float horizontalDistance = std::sqrt(
            (result.state.pose.position.x - result.groundedX) *
                (result.state.pose.position.x - result.groundedX) +
            (result.state.pose.position.z - result.groundedZ) *
                (result.state.pose.position.z - result.groundedZ));
        return result.leftGroundImmediately &&
            result.stayedAirborneWhileAscending &&
            result.highestY > result.groundedY + 0.25f &&
            horizontalDistance > 0.25f &&
            result.reachedApex &&
            result.lowestFallingSpeed < -1.0f &&
            result.landedAfterJump;
    }

    PhysicsShapeType ToRuntimeShapeType(
        ASSETS::COLLISION::CollisionGeometryShapeType type) noexcept {
        using SourceType =
            ASSETS::COLLISION::CollisionGeometryShapeType;
        switch (type) {
        case SourceType::Sphere: return PhysicsShapeType::Sphere;
        case SourceType::Capsule: return PhysicsShapeType::Capsule;
        case SourceType::ConvexHull: return PhysicsShapeType::ConvexHull;
        case SourceType::TriangleMesh: return PhysicsShapeType::TriangleMesh;
        case SourceType::Box:
        default: return PhysicsShapeType::Box;
        }
    }

    std::vector<PhysicsShapeDesc> BuildImportedShapes(
        ASSETS::COLLISION::CollisionGeometryAsset asset) {
        constexpr float kDegreesToRadians =
            0.01745329251994329577f;
        auto geometry = std::make_shared<PhysicsGeometryBuffer>();
        geometry->vertices = std::move(asset.vertices);
        geometry->indices = std::move(asset.indices);

        std::vector<PhysicsShapeDesc> result{};
        result.reserve(asset.shapes.size());
        for (const auto& source : asset.shapes) {
            PhysicsShapeDesc shape{};
            shape.key.sourceShapeId = source.id;
            shape.key.componentOrdinal = 1u;
            shape.type = ToRuntimeShapeType(source.type);
            shape.localCenter = source.center;
            shape.localRotation = MATH::NormalizeQ(
                MATH::Quat::FromEulerXYZ(
                    source.rotationEulerDegrees.x * kDegreesToRadians,
                    source.rotationEulerDegrees.y * kDegreesToRadians,
                    source.rotationEulerDegrees.z * kDegreesToRadians));
            shape.halfExtents = source.size * 0.5f;
            shape.radius = source.radius;
            shape.height = source.height;
            shape.geometry = geometry;
            shape.vertexOffset = source.vertexOffset;
            shape.vertexCount = source.vertexCount;
            shape.indexOffset = source.indexOffset;
            shape.indexCount = source.indexCount;
            shape.filter.layer = 1u;
            shape.filter.mask = 0xFFFFFFFFu;
            result.push_back(std::move(shape));
        }
        return result;
    }

    bool RunFallCase(
        std::string_view name,
        PhysicsShapeDesc floorShape) {
        std::unique_ptr<IPhysicsWorldBackend> backend =
            CreateJoltPhysicsBackend();
        if (!backend || !backend->Initialize({})) {
            std::cerr << name << ": backend initialization failed\n";
            return false;
        }

        PhysicsBodyCreateInfo floor{};
        floor.body.object = Object(0u);
        floor.body.motionType = PhysicsMotionType::Static;
        floor.initialPose.position = floorShape.type == PhysicsShapeType::Box
            ? MATH::Vec3{ 0.0f, -0.5f, 0.0f }
            : MATH::Vec3{};
        floor.shapes.push_back(std::move(floorShape));
        const PhysicsBodyCreateResult floorResult =
            backend->CreateBody(floor);
        if (!floorResult.Succeeded()) {
            std::cerr << name << ": floor creation failed: "
                << floorResult.message << '\n';
            return false;
        }

        PhysicsCharacterCreateInfo character{};
        character.character.object = Object(1u);
        character.character.shapes.push_back(MakeCharacterCapsule());
        character.character.filter =
            character.character.shapes.front().filter;
        character.initialPose.position = { 0.0f, 3.0f, 0.0f };
        const JPH::ShapeRefC debugShape =
            HIKARI::PHYSICS::JOLT_BACKEND::BuildCompoundShape(
                character.character.shapes);
        if (debugShape) {
            const JPH::AABox bounds = debugShape->GetLocalBounds();
            std::cout << name << ": local bounds minY="
                << bounds.mMin.GetY() << " maxY="
                << bounds.mMax.GetY() << '\n';
        }

        PhysicsBodyCreateInfo characterBody{};
        characterBody.body.object = character.character.object;
        characterBody.body.motionType = PhysicsMotionType::Kinematic;
        characterBody.initialPose = character.initialPose;
        characterBody.shapes = character.character.shapes;
        const PhysicsBodyCreateResult characterBodyResult =
            backend->CreateBody(characterBody);
        if (!characterBodyResult.Succeeded()) {
            std::cerr << name << ": kinematic body creation failed: "
                << characterBodyResult.message << '\n';
            return false;
        }
        const PhysicsCharacterCreateResult characterResult =
            backend->CreateCharacter(character);
        if (!characterResult.Succeeded()) {
            std::cerr << name << ": character creation failed: "
                << characterResult.message << '\n';
            return false;
        }

        PhysicsCharacterStepSettings step{};
        PhysicsCharacterState state{};
        for (uint32_t frame = 0u; frame < 360u; ++frame) {
            PhysicsBodyState bodyState{};
            if (!backend->TryGetBodyState(
                    characterBodyResult.handle,
                    bodyState) ||
                !backend->SetCharacterPose(
                    characterResult.handle,
                    bodyState.pose) ||
                !backend->RefreshCharacterContacts(
                    characterResult.handle) ||
                !backend->RefreshCharacterGroundVelocity(
                    characterResult.handle) ||
                !backend->TryGetCharacterState(
                    characterResult.handle,
                    state)) {
                std::cerr << name << ": character state update failed\n";
                return false;
            }

            MATH::Vec3 velocity = state.linearVelocity;
            if (state.IsGrounded() &&
                velocity.y - state.groundVelocity.y < 0.1f) {
                velocity = state.groundVelocity;
            }
            velocity = velocity + step.gravity * kFixedDeltaSeconds;
            if (!backend->SetCharacterVelocity(
                    characterResult.handle,
                    velocity) ||
                !backend->StepCharacter(
                    characterResult.handle,
                    kFixedDeltaSeconds,
                    step) ||
                !backend->TryGetCharacterState(
                    characterResult.handle,
                    state) ||
                !backend->SetKinematicTarget(
                    characterBodyResult.handle,
                    state.pose) ||
                !backend->Step(kFixedDeltaSeconds).Succeeded()) {
                std::cerr << name << ": physics step failed\n";
                return false;
            }
        }

        if (!backend->TryGetCharacterState(
                characterResult.handle,
                state)) {
            std::cerr << name << ": final state read failed\n";
            return false;
        }
        PhysicsBodyState finalBodyState{};
        if (!backend->TryGetBodyState(
                characterBodyResult.handle,
                finalBodyState)) {
            std::cerr << name << ": final body state read failed\n";
            return false;
        }

        const bool stoppedOnFloor =
            state.pose.position.y > -0.05f &&
            state.pose.position.y < 0.20f &&
            finalBodyState.pose.position.y > -0.05f &&
            finalBodyState.pose.position.y < 0.20f &&
            std::abs(state.linearVelocity.y) < 0.25f &&
            state.IsGrounded();
        std::cout << name
            << ": y=" << state.pose.position.y
            << " bodyY=" << finalBodyState.pose.position.y
            << " vy=" << state.linearVelocity.y
            << " grounded=" << state.IsGrounded()
            << " groundState=" << static_cast<int>(state.groundState)
            << " normal=(" << state.groundNormal.x << ','
            << state.groundNormal.y << ',' << state.groundNormal.z << ')'
            << " wall=" << state.hitWall
            << " ceiling=" << state.hitCeiling
            << '\n';
        if (!stoppedOnFloor) {
            return false;
        }

        MovingJumpResult movingJump{};
        if (!RunMovingJump(
                *backend,
                characterResult.handle,
                characterBodyResult.handle,
                state,
                name,
                movingJump)) {
            return false;
        }
        std::cout << name
            << " moving jump: y=" << movingJump.state.pose.position.y
            << " highestY=" << movingJump.highestY
            << " x=" << movingJump.state.pose.position.x
            << " vy=" << movingJump.state.linearVelocity.y
            << " minVy=" << movingJump.lowestFallingSpeed
            << " grounded=" << movingJump.state.IsGrounded()
            << '\n';
        return MovingJumpSucceeded(movingJump);
    }

    bool RunImportedSceneCase(const char* artifactPath) {
        ASSETS::COLLISION::CollisionGeometryAsset asset{};
        std::string readMessage{};
        if (!ASSETS::COLLISION::ReadHcollisionFile(
                artifactPath,
                asset,
                readMessage)) {
            std::cerr << "imported scene: " << readMessage << '\n';
            return false;
        }

        std::unique_ptr<IPhysicsWorldBackend> backend =
            CreateJoltPhysicsBackend();
        if (!backend || !backend->Initialize({})) {
            std::cerr << "imported scene: backend initialization failed\n";
            return false;
        }

        PhysicsBodyCreateInfo scene{};
        scene.body.object = Object(0u);
        scene.body.motionType = PhysicsMotionType::Static;
        scene.initialPose.position = {
            0.15074238f, 2.31470466f, -1.81452036f
        };
        scene.shapes = BuildImportedShapes(std::move(asset));
        const PhysicsBodyCreateResult sceneResult =
            backend->CreateBody(scene);
        if (!sceneResult.Succeeded()) {
            std::cerr << "imported scene: body creation failed: "
                << sceneResult.message << '\n';
            return false;
        }

        PhysicsBodyCreateInfo characterBody{};
        characterBody.body.object = Object(1u);
        characterBody.body.motionType = PhysicsMotionType::Kinematic;
        characterBody.initialPose.position = {
            -11.0596380f, 3.88450956f, -1.23674965f
        };
        characterBody.shapes.push_back(MakeCharacterCapsule());
        const PhysicsBodyCreateResult bodyResult =
            backend->CreateBody(characterBody);
        if (!bodyResult.Succeeded()) {
            std::cerr << "imported scene: character body creation failed: "
                << bodyResult.message << '\n';
            return false;
        }

        PhysicsCharacterCreateInfo character{};
        character.character.object = characterBody.body.object;
        character.character.shapes = characterBody.shapes;
        character.character.filter =
            character.character.shapes.front().filter;
        character.initialPose = characterBody.initialPose;
        const PhysicsCharacterCreateResult characterResult =
            backend->CreateCharacter(character);
        if (!characterResult.Succeeded()) {
            std::cerr << "imported scene: character creation failed: "
                << characterResult.message << '\n';
            return false;
        }

        PhysicsCharacterStepSettings step{};
        PhysicsCharacterState state{};
        bool reachedGround = false;
        for (uint32_t frame = 0u; frame < 900u; ++frame) {
            PhysicsBodyState bodyState{};
            if (!backend->TryGetBodyState(bodyResult.handle, bodyState) ||
                !backend->SetCharacterPose(
                    characterResult.handle,
                    bodyState.pose) ||
                !backend->RefreshCharacterContacts(
                    characterResult.handle) ||
                !backend->RefreshCharacterGroundVelocity(
                    characterResult.handle) ||
                !backend->TryGetCharacterState(
                    characterResult.handle,
                    state)) {
                std::cerr << "imported scene: state update failed\n";
                return false;
            }

            MATH::Vec3 velocity = state.linearVelocity;
            if (state.IsGrounded() &&
                velocity.y - state.groundVelocity.y < 0.1f) {
                velocity = state.groundVelocity;
            }
            velocity = velocity + step.gravity * kFixedDeltaSeconds;
            if (!backend->SetCharacterVelocity(
                    characterResult.handle,
                    velocity) ||
                !backend->StepCharacter(
                    characterResult.handle,
                    kFixedDeltaSeconds,
                    step) ||
                !backend->TryGetCharacterState(
                    characterResult.handle,
                    state) ||
                !backend->SetKinematicTarget(
                    bodyResult.handle,
                    state.pose) ||
                !backend->Step(kFixedDeltaSeconds).Succeeded()) {
                std::cerr << "imported scene: physics step failed\n";
                return false;
            }
            reachedGround = reachedGround || state.IsGrounded();
        }

        MovingJumpResult movingJump{};
        if (!RunMovingJump(
                *backend,
                characterResult.handle,
                bodyResult.handle,
                state,
                "imported scene",
                movingJump)) {
            return false;
        }
        const bool movingJumped = MovingJumpSucceeded(movingJump);

        std::cout << "imported scene: y="
            << movingJump.state.pose.position.y
            << " highestY=" << movingJump.highestY
            << " x=" << movingJump.state.pose.position.x
            << " vy=" << movingJump.state.linearVelocity.y
            << " minVy=" << movingJump.lowestFallingSpeed
            << " grounded=" << movingJump.state.IsGrounded()
            << " reachedGround=" << reachedGround
            << " movingJumped=" << movingJumped << '\n';
        return reachedGround && movingJumped &&
            movingJump.state.pose.position.y > -10.0f;
    }
}

int main(int argc, char** argv) {
    const bool box = RunFallCase("box floor", MakeBoxFloor());
    const bool mesh = RunFallCase("triangle floor", MakeTriangleFloor());
    const bool imported = argc >= 2
        ? RunImportedSceneCase(argv[1])
        : true;
    return box && mesh && imported ? 0 : 1;
}
