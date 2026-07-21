#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::ASSETS::COLLISION {

    enum class ModelCollisionGenerationTarget : uint8_t {
        WholeModel,
        SelectedNodesCombined,
        SelectedNodesIndividually,
    };

    struct ModelCollisionGenerationRequest {
        ModelCollisionGenerationTarget target =
            ModelCollisionGenerationTarget::WholeModel;
        CollisionGeometryShapeType shapeType =
            CollisionGeometryShapeType::Box;
        std::vector<int32_t> sourceNodeIndices{};
        bool replaceGeneratedShapes = true;
        uint32_t maximumGeneratedShapes = 512u;
    };

    struct ModelCollisionGenerationResult {
        bool success = false;
        bool truncated = false;
        uint32_t candidateCount = 0u;
        uint32_t generatedCount = 0u;
        uint32_t removedGeneratedCount = 0u;
        std::vector<uint64_t> generatedShapeIds{};
        std::string message{};
    };

    ModelCollisionGenerationResult GenerateModelCollisionShapes(
        const ModelAsset& model,
        const ModelCollisionGenerationRequest& request,
        ModelCollisionSetup& setup);

    ModelCollisionShape CreateFittedCollisionShape(
        ModelCollisionSetup& setup,
        CollisionGeometryShapeType type,
        const Bounds& bounds,
        std::string name,
        bool generated,
        int32_t sourceNodeIndex = -1);

} // namespace HIKARI::ASSETS::COLLISION
