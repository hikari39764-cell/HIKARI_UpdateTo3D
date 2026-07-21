#include "Assets/Collision/HIKARI_ModelCollisionGenerator.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"

namespace HIKARI::ASSETS::COLLISION {
    namespace {
        MATH::Vec3 BoundsCenter(const Bounds& bounds) noexcept {
            return (bounds.min + bounds.max) * 0.5f;
        }

        MATH::Vec3 BoundsSize(const Bounds& bounds) noexcept {
            return bounds.max - bounds.min;
        }

        std::string ShapeName(
            std::string_view prefix,
            uint64_t id) {

            return std::string(prefix) + " " + std::to_string(id);
        }

        void FitCapsuleToBounds(
            const Bounds& bounds,
            ModelCollisionShape& shape) noexcept {

            const MATH::Vec3 size = BoundsSize(bounds);
            const float largest = (std::max)({ size.x, size.y, size.z });
            if (largest == size.x) {
                shape.rotationEulerDegrees = { 0.0f, 0.0f, -90.0f };
                shape.radius = (std::max)(size.y, size.z) * 0.5f;
            } else if (largest == size.z) {
                shape.rotationEulerDegrees = { 90.0f, 0.0f, 0.0f };
                shape.radius = (std::max)(size.x, size.y) * 0.5f;
            } else {
                shape.rotationEulerDegrees = {};
                shape.radius = (std::max)(size.x, size.z) * 0.5f;
            }
            shape.radius = (std::max)(shape.radius, 0.001f);
            shape.height = (std::max)(largest, shape.radius * 2.0f);
        }

        const char* ShapeNamePrefix(
            CollisionGeometryShapeType type) noexcept {

            switch (type) {
            case CollisionGeometryShapeType::Sphere:
                return "Sphere";
            case CollisionGeometryShapeType::Capsule:
                return "Capsule";
            case CollisionGeometryShapeType::Box:
            default:
                return "Box";
            }
        }
    }

    ModelCollisionShape CreateFittedCollisionShape(
        ModelCollisionSetup& setup,
        CollisionGeometryShapeType type,
        const Bounds& bounds,
        std::string name,
        bool generated,
        int32_t sourceNodeIndex) {

        ModelCollisionShape shape{};
        shape.id = setup.AllocateShapeId();
        shape.type = type;
        shape.name = name.empty()
            ? ShapeName("Collision", shape.id)
            : std::move(name);
        shape.center = BoundsCenter(bounds);
        shape.size = BoundsSize(bounds);
        shape.size.x = (std::max)(shape.size.x, 0.001f);
        shape.size.y = (std::max)(shape.size.y, 0.001f);
        shape.size.z = (std::max)(shape.size.z, 0.001f);
        shape.radius = (std::max)(
            0.001f,
            MATH::Length(shape.size) * 0.5f);
        shape.height = (std::max)(shape.size.y, shape.radius * 2.0f);
        shape.generated = generated;
        shape.sourceNodeIndex = sourceNodeIndex;
        if (type == CollisionGeometryShapeType::Capsule) {
            FitCapsuleToBounds(bounds, shape);
        }
        return shape;
    }

    ModelCollisionGenerationResult GenerateModelCollisionShapes(
        const ModelAsset& model,
        const ModelCollisionGenerationRequest& request,
        ModelCollisionSetup& setup) {

        ModelCollisionGenerationResult result{};
        if (request.maximumGeneratedShapes == 0u) {
            result.message = "generation shape budget is zero";
            return result;
        }

        std::vector<ModelCollisionShape> generated{};
        if (request.target ==
                ModelCollisionGenerationTarget::WholeModel) {
            const Bounds modelBounds = BOUNDS::IsUsable(model.bounds)
                ? model.bounds
                : BOUNDS::ComputeModelBounds(model);
            if (!BOUNDS::IsUsable(modelBounds)) {
                result.message = "model has no usable bounds";
                return result;
            }
            generated.push_back(CreateFittedCollisionShape(
                setup,
                request.shapeType,
                modelBounds,
                std::string("Auto Model ") +
                    ShapeNamePrefix(request.shapeType),
                true));
            result.candidateCount = 1u;
        } else {
            if (request.sourceNodeIndices.empty()) {
                result.message =
                    "select one or more source model parts first";
                return result;
            }
            const std::vector<MATH::Mat4> globals =
                BOUNDS::BuildModelNodeGlobals(model);
            std::unordered_set<int32_t> selected(
                request.sourceNodeIndices.begin(),
                request.sourceNodeIndices.end());
            Bounds combinedBounds = BOUNDS::EmptyBounds();
            bool hasCombinedBounds = false;
            for (size_t nodeIndex = 0;
                nodeIndex < model.nodes.size();
                ++nodeIndex) {

                if (!selected.contains(static_cast<int32_t>(nodeIndex))) {
                    continue;
                }
                const Bounds bounds = BOUNDS::ComputeModelNodeBounds(
                    model,
                    nodeIndex,
                    globals);
                if (!BOUNDS::IsUsable(bounds)) {
                    continue;
                }
                ++result.candidateCount;
                if (request.target ==
                        ModelCollisionGenerationTarget::SelectedNodesCombined) {
                    BOUNDS::Encapsulate(combinedBounds, bounds);
                    hasCombinedBounds = true;
                    continue;
                }
                if (generated.size() >= request.maximumGeneratedShapes) {
                    result.truncated = true;
                    continue;
                }
                const ModelNode& node = model.nodes[nodeIndex];
                const std::string nodeName = node.name.empty()
                    ? "Node " + std::to_string(nodeIndex)
                    : node.name;
                generated.push_back(CreateFittedCollisionShape(
                    setup,
                    request.shapeType,
                    bounds,
                    "Auto " + nodeName + " " +
                        ShapeNamePrefix(request.shapeType),
                    true,
                    static_cast<int32_t>(nodeIndex)));
            }
            if (request.target ==
                    ModelCollisionGenerationTarget::SelectedNodesCombined &&
                hasCombinedBounds) {
                generated.push_back(CreateFittedCollisionShape(
                    setup,
                    request.shapeType,
                    combinedBounds,
                    std::string("Auto Selection ") +
                        ShapeNamePrefix(request.shapeType),
                    true));
            }
        }

        if (generated.empty()) {
            result.message = "no collision shapes could be generated";
            return result;
        }
        if (request.replaceGeneratedShapes) {
            const size_t before = setup.shapes.size();
            std::erase_if(
                setup.shapes,
                [](const ModelCollisionShape& shape) {
                    return shape.generated;
                });
            result.removedGeneratedCount = static_cast<uint32_t>(
                before - setup.shapes.size());
        }
        result.generatedCount = static_cast<uint32_t>(generated.size());
        result.generatedShapeIds.reserve(generated.size());
        for (const ModelCollisionShape& shape : generated) {
            result.generatedShapeIds.push_back(shape.id);
        }
        setup.shapes.insert(
            setup.shapes.end(),
            std::make_move_iterator(generated.begin()),
            std::make_move_iterator(generated.end()));
        result.success = true;
        result.message = "generated " +
            std::to_string(result.generatedCount) +
            " collision shape(s)";
        if (result.truncated) {
            result.message += " (limited from " +
                std::to_string(result.candidateCount) + ")";
        }
        return result;
    }

} // namespace HIKARI::ASSETS::COLLISION
