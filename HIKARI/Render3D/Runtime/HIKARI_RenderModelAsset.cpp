#include "Render3D/Runtime/HIKARI_RenderModelAsset.h"

#include <string>
#include <utility>

#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::RENDER3D::RUNTIME {
    namespace {
        MATH::Mat4 GetNodeLocalMatrix(const ModelNode& node) {
            return node.hasLocalMatrix ? node.localMatrix : node.localTransform.GetLocalMatrix();
        }

        bool IsPrimitiveSkinned(const MeshPrimitive& primitive) {
            return primitive.layout == VertexLayoutKind::SkinnedPNTTJW || !primitive.skinnedVertices.empty();
        }

        Bounds ResolvePrimitiveBounds(const MeshPrimitive& primitive) {
            return BOUNDS::IsUsable(primitive.bounds)
                ? primitive.bounds
                : BOUNDS::ComputePrimitiveBounds(primitive);
        }

        Bounds ResolveMeshBounds(const MeshAsset& mesh) {
            return BOUNDS::IsUsable(mesh.bounds)
                ? mesh.bounds
                : BOUNDS::ComputeMeshBounds(mesh);
        }

        void AccumulateBounds(Bounds& bounds, bool& hasBounds, const Bounds& value) {
            if (!BOUNDS::IsUsable(value)) {
                return;
            }
            if (!hasBounds) {
                bounds = value;
                hasBounds = true;
                return;
            }
            BOUNDS::Encapsulate(bounds, value);
        }

        RenderSubmeshRecord BuildSubmeshRecord(
            uint32_t nodeIndex,
            uint32_t meshIndex,
            uint32_t primitiveIndex,
            int skinIndex,
            const MeshPrimitive& primitive) {

            RenderSubmeshRecord record{};
            record.nodeIndex = nodeIndex;
            record.meshIndex = meshIndex;
            record.primitiveIndex = primitiveIndex;
            record.materialIndex = primitive.materialIndex;
            record.localBounds = ResolvePrimitiveBounds(primitive);
            record.skinIndex = skinIndex;
            record.skinningMode = IsPrimitiveSkinned(primitive)
                ? RenderSubmeshSkinningMode::Skinned
                : RenderSubmeshSkinningMode::Static;
            return record;
        }

        void AppendSubmesh(
            RenderModelAsset& out,
            uint32_t nodeIndex,
            uint32_t meshIndex,
            uint32_t primitiveIndex,
            int skinIndex,
            const MeshPrimitive& primitive) {

            RenderSubmeshRecord record = BuildSubmeshRecord(
                nodeIndex,
                meshIndex,
                primitiveIndex,
                skinIndex,
                primitive);

            if (record.skinningMode == RenderSubmeshSkinningMode::Skinned) {
                out.hasSkinnedSubmeshes = true;
            } else {
                out.hasStaticSubmeshes = true;
            }
            out.submeshes.push_back(std::move(record));
        }

        void BuildNodeRecords(const ModelAsset& source, RenderModelAsset& out) {
            out.nodes.reserve(source.nodes.size());
            for (size_t nodeIndex = 0; nodeIndex < source.nodes.size(); ++nodeIndex) {
                const ModelNode& sourceNode = source.nodes[nodeIndex];

                RenderModelNodeRecord nodeRecord{};
                nodeRecord.nodeIndex = static_cast<uint32_t>(nodeIndex);
                nodeRecord.parentIndex = sourceNode.parent;
                nodeRecord.localMatrix = GetNodeLocalMatrix(sourceNode);
                nodeRecord.meshIndex = sourceNode.meshIndex;
                nodeRecord.skinIndex = sourceNode.skinIndex;

                const bool hasValidMesh =
                    sourceNode.meshIndex >= 0 &&
                    sourceNode.meshIndex < static_cast<int>(source.meshes.size());
                nodeRecord.hasMesh = hasValidMesh;
                if (hasValidMesh) {
                    const MeshAsset& mesh = source.meshes[static_cast<size_t>(sourceNode.meshIndex)];
                    nodeRecord.localBounds = ResolveMeshBounds(mesh);
                    for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
                        AppendSubmesh(
                            out,
                            static_cast<uint32_t>(nodeIndex),
                            static_cast<uint32_t>(sourceNode.meshIndex),
                            static_cast<uint32_t>(primitiveIndex),
                            sourceNode.skinIndex,
                            mesh.primitives[primitiveIndex]);
                    }
                }

                out.nodes.push_back(std::move(nodeRecord));
            }
        }

        void BuildLegacySubmeshes(const ModelAsset& source, RenderModelAsset& out) {
            for (size_t meshIndex = 0; meshIndex < source.meshes.size(); ++meshIndex) {
                const MeshAsset& mesh = source.meshes[meshIndex];
                for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
                    AppendSubmesh(
                        out,
                        kInvalidRenderModelIndex,
                        static_cast<uint32_t>(meshIndex),
                        static_cast<uint32_t>(primitiveIndex),
                        -1,
                        mesh.primitives[primitiveIndex]);
                }
            }
        }

        Bounds ResolveModelBounds(const ModelAsset& source) {
            if (BOUNDS::IsUsable(source.bounds)) {
                return source.bounds;
            }
            return BOUNDS::ComputeModelBounds(source);
        }
    }

    bool BuildRenderModelAsset(
        const ModelAsset& source,
        RenderModelAsset& out,
        std::string* outMessage) {

        out = {};
        out.source = &source;
        out.sourceName = source.GetName();
        out.sourcePath = source.GetSourcePath();

        if (!source.nodes.empty()) {
            BuildNodeRecords(source, out);
        } else {
            // 旧形式は node 無しで submesh だけを作る。
            BuildLegacySubmeshes(source, out);
        }

        out.localBounds = ResolveModelBounds(source);
        out.valid = !out.submeshes.empty();

        if (outMessage != nullptr) {
            *outMessage = out.valid
                ? std::string{}
                : "RenderModelAsset has no submesh records.";
        }
        return out.valid;
    }

} // namespace HIKARI::RENDER3D::RUNTIME
