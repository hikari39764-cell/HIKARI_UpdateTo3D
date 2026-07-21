#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::ASSETS::COLLISION {

    class ModelCollisionGenerationControl {
    public:
        void RequestCancel() noexcept {
            cancellationRequested_.store(
                true,
                std::memory_order_relaxed);
        }

        bool IsCancellationRequested() const noexcept {
            return cancellationRequested_.load(
                std::memory_order_relaxed);
        }

    private:
        std::atomic_bool cancellationRequested_{ false };
    };

    enum class ModelCollisionGenerationTarget : uint8_t {
        WholeModel,
        SelectedNodesCombined,
        SelectedNodesSpatialGroups,
        SelectedNodesIndividually,
    };

    enum class ModelCollisionGenerationMethod : uint8_t {
        Box,
        Sphere,
        Capsule,
        ConvexHull,
        ConvexDecomposition,
        TriangleMesh,
    };

    struct ModelCollisionGenerationRequest {
        ModelCollisionGenerationTarget target =
            ModelCollisionGenerationTarget::WholeModel;
        ModelCollisionGenerationMethod method =
            ModelCollisionGenerationMethod::Box;
        std::vector<int32_t> sourceNodeIndices{};
        bool replaceGeneratedShapes = true;
        bool preserveGaps = true;
        float mergeDistance = 0.1f;
        float accuracy = 0.05f;
        uint32_t maximumGeneratedShapes = 512u;
        uint32_t maximumHullVertices = 128u;
        uint32_t maximumTriangleCount = 1000000u;
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
        ModelCollisionSetup& setup,
        const ModelCollisionGenerationControl* control = nullptr);

    ModelCollisionShape CreateFittedCollisionShape(
        ModelCollisionSetup& setup,
        CollisionGeometryShapeType type,
        const Bounds& bounds,
        std::string name,
        bool generated,
        std::vector<int32_t> sourceNodeIndices = {});

} // namespace HIKARI::ASSETS::COLLISION
