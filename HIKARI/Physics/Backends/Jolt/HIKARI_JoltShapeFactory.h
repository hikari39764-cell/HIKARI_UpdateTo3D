#pragma once

#include <span>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI::PHYSICS::JOLT_BACKEND {

    JPH::ShapeRefC BuildCompoundShape(
        std::span<const PhysicsShapeDesc> shapes);

    JPH::ShapeRefC BuildQueryShape(
        const PhysicsShapeDesc& shape);

} // namespace HIKARI::PHYSICS::JOLT_BACKEND
