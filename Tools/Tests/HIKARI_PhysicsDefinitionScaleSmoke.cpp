#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>

#include "Physics/HIKARI_PhysicsDefinitionScale.h"
#include "Render3D/HIKARI_Math3D.h"

namespace {
    bool BitwiseEqual(
        const HIKARI::MATH::Vec3& left,
        const HIKARI::MATH::Vec3& right) noexcept {
        return std::bit_cast<uint32_t>(left.x) ==
                std::bit_cast<uint32_t>(right.x) &&
            std::bit_cast<uint32_t>(left.y) ==
                std::bit_cast<uint32_t>(right.y) &&
            std::bit_cast<uint32_t>(left.z) ==
                std::bit_cast<uint32_t>(right.z);
    }

    bool MeasureScale(
        float yawRadians,
        const HIKARI::MATH::Vec3& authoredScale,
        HIKARI::MATH::Vec3& outScale) {
        HIKARI::MATH::Vec3 position{};
        HIKARI::MATH::Quat rotation{};
        const HIKARI::MATH::Mat4 world = HIKARI::MATH::Mat4::TRS(
            { 8.0f, 3.0f, -5.0f },
            HIKARI::MATH::Quat::FromEulerXYZ(
                0.21f,
                yawRadians,
                -0.13f),
            authoredScale);
        return HIKARI::MATH::DecomposeTRS(
            world,
            position,
            rotation,
            outScale);
    }
}

int main() {
    using namespace HIKARI;
    using namespace HIKARI::PHYSICS;

    MATH::Vec3 measured{};
    if (!MeasureScale(0.0f, { 1.0f, 1.0f, 1.0f }, measured)) {
        std::cerr << "initial transform decomposition failed\n";
        return 1;
    }
    MATH::Vec3 retained = ResolvePhysicsDefinitionScale(measured);

    for (uint32_t index = 1u; index <= 7200u; ++index) {
        const float yaw = static_cast<float>(index) * 0.00137f;
        if (!MeasureScale(yaw, { 1.0f, 1.0f, 1.0f }, measured)) {
            std::cerr << "rotated transform decomposition failed\n";
            return 2;
        }
        const MATH::Vec3 resolved = ResolvePhysicsDefinitionScale(
            measured,
            &retained);
        if (!BitwiseEqual(resolved, retained)) {
            std::cerr << "rotation changed the physics definition scale\n";
            return 3;
        }
    }

    if (!MeasureScale(1.7f, { 1.025f, 1.0f, 1.0f }, measured)) {
        std::cerr << "edited transform decomposition failed\n";
        return 4;
    }
    const MATH::Vec3 edited = ResolvePhysicsDefinitionScale(
        measured,
        &retained);
    if (BitwiseEqual(edited, retained) ||
        !ArePhysicsDefinitionScalesEquivalent(
            edited,
            { 1.025f, 1.0f, 1.0f })) {
        std::cerr << "authored scale edit was not accepted\n";
        return 5;
    }

    std::cout << "Physics definition scale smoke test passed.\n";
    return 0;
}
