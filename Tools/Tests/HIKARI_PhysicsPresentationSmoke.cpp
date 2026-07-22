#include <cmath>
#include <iostream>
#include <limits>

#include "Physics/HIKARI_PhysicsPresentation.h"

namespace {
    constexpr float kTolerance = 1.0e-5f;

    bool NearlyEqual(float left, float right) noexcept {
        return std::abs(left - right) <= kTolerance;
    }

    bool NearlyEqual(
        const HIKARI::MATH::Vec3& left,
        const HIKARI::MATH::Vec3& right) noexcept {
        return NearlyEqual(left.x, right.x) &&
            NearlyEqual(left.y, right.y) &&
            NearlyEqual(left.z, right.z);
    }

    float QuaternionLength(const HIKARI::MATH::Quat& value) noexcept {
        return std::sqrt(
            value.x * value.x + value.y * value.y +
            value.z * value.z + value.w * value.w);
    }
}

int main() {
    using namespace HIKARI;
    using namespace HIKARI::PHYSICS;

    PhysicsPose previous{};
    previous.position = { 2.0f, -4.0f, 8.0f };
    previous.rotation = MATH::Quat::Identity();

    PhysicsPose current{};
    current.position = { 10.0f, 4.0f, -8.0f };
    current.rotation = { 0.0f, 0.0f, 0.0f, -1.0f };

    const PhysicsPose midpoint = InterpolatePhysicsPose(
        previous,
        current,
        0.5f);
    if (!NearlyEqual(midpoint.position, { 6.0f, 0.0f, 0.0f })) {
        std::cerr << "position interpolation is incorrect\n";
        return 1;
    }
    if (!NearlyEqual(QuaternionLength(midpoint.rotation), 1.0f) ||
        !NearlyEqual(std::abs(midpoint.rotation.w), 1.0f)) {
        std::cerr << "shortest-path quaternion interpolation is incorrect\n";
        return 2;
    }

    const PhysicsPose belowRange = InterpolatePhysicsPose(
        previous,
        current,
        -2.0f);
    if (!NearlyEqual(belowRange.position, previous.position)) {
        std::cerr << "negative interpolation alpha was not clamped\n";
        return 3;
    }

    const PhysicsPose aboveRange = InterpolatePhysicsPose(
        previous,
        current,
        3.0f);
    if (!NearlyEqual(aboveRange.position, current.position)) {
        std::cerr << "large interpolation alpha was not clamped\n";
        return 4;
    }

    const PhysicsPose nonFinite = InterpolatePhysicsPose(
        previous,
        current,
        (std::numeric_limits<float>::quiet_NaN)());
    if (!NearlyEqual(nonFinite.position, previous.position)) {
        std::cerr << "non-finite interpolation alpha was not sanitized\n";
        return 5;
    }

    std::cout << "Physics presentation smoke test passed.\n";
    return 0;
}
