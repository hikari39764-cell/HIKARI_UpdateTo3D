#include "Tools/Geometry/HIKARI_MeshLodGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "../../../ThirdParty/meshoptimizer/src/meshoptimizer.h"

namespace HIKARI::TOOLS::GEOMETRY {

    namespace {
        using RENDER3D::CLUSTER::ClusterVertex;

        constexpr size_t kLodAttributeCount = 15u;

        size_t AlignIndexCountToTriangles(size_t indexCount) {
            return indexCount - (indexCount % 3u);
        }

        float ClampRatio(float value) {
            if (!std::isfinite(value)) {
                return 0.75f;
            }
            return (std::max)(0.05f, (std::min)(value, 0.98f));
        }

        float ClampTargetError(float value) {
            if (!std::isfinite(value)) {
                return 0.006f;
            }
            return (std::max)(0.0001f, (std::min)(value, 0.08f));
        }

        void BuildSimplifierStreams(
            const std::vector<ClusterVertex>& vertices,
            std::vector<float>& outPositions,
            std::vector<float>& outAttributes) {

            outPositions.clear();
            outAttributes.clear();
            outPositions.reserve(vertices.size() * 3u);
            outAttributes.reserve(vertices.size() * kLodAttributeCount);

            for (const ClusterVertex& vertex : vertices) {
                outPositions.push_back(vertex.position.x);
                outPositions.push_back(vertex.position.y);
                outPositions.push_back(vertex.position.z);

                // LOD は見た目の破綻を避けるため、形状だけでなく主要属性も誤差評価に入れる。
                outAttributes.push_back(vertex.normal.x);
                outAttributes.push_back(vertex.normal.y);
                outAttributes.push_back(vertex.normal.z);
                outAttributes.push_back(vertex.tangent.x);
                outAttributes.push_back(vertex.tangent.y);
                outAttributes.push_back(vertex.tangent.z);
                outAttributes.push_back(vertex.tangent.w);
                outAttributes.push_back(vertex.uv0.x);
                outAttributes.push_back(vertex.uv0.y);
                outAttributes.push_back(vertex.uv1.x);
                outAttributes.push_back(vertex.uv1.y);
                outAttributes.push_back(vertex.color.x);
                outAttributes.push_back(vertex.color.y);
                outAttributes.push_back(vertex.color.z);
                outAttributes.push_back(vertex.color.w);
            }
        }

        bool CompactLodVertices(
            const std::vector<ClusterVertex>& sourceVertices,
            const std::vector<unsigned int>& sourceIndices,
            MeshLodGeneratorResult& outResult) {

            outResult.vertices.clear();
            outResult.indices.clear();
            if (sourceVertices.empty() || sourceIndices.size() < 3u) {
                return false;
            }

            std::vector<uint32_t> remap(
                sourceVertices.size(),
                RENDER3D::CLUSTER::kInvalidClusterIndex);
            outResult.vertices.reserve(sourceVertices.size());
            outResult.indices.reserve(sourceIndices.size());

            for (unsigned int rawIndex : sourceIndices) {
                if (rawIndex >= sourceVertices.size()) {
                    return false;
                }

                uint32_t& mappedIndex = remap[rawIndex];
                if (mappedIndex == RENDER3D::CLUSTER::kInvalidClusterIndex) {
                    mappedIndex = static_cast<uint32_t>(outResult.vertices.size());
                    outResult.vertices.push_back(sourceVertices[rawIndex]);
                }
                outResult.indices.push_back(mappedIndex);
            }

            if (outResult.indices.size() < 3u) {
                return false;
            }

            std::vector<uint32_t> filteredIndices{};
            filteredIndices.reserve(outResult.indices.size());
            for (size_t i = 0; i + 2u < outResult.indices.size(); i += 3u) {
                const uint32_t i0 = outResult.indices[i + 0u];
                const uint32_t i1 = outResult.indices[i + 1u];
                const uint32_t i2 = outResult.indices[i + 2u];
                if (i0 == i1 || i1 == i2 || i2 == i0) {
                    continue;
                }
                filteredIndices.push_back(i0);
                filteredIndices.push_back(i1);
                filteredIndices.push_back(i2);
            }

            outResult.indices = std::move(filteredIndices);
            return outResult.indices.size() >= 3u && !outResult.vertices.empty();
        }

        std::array<float, kLodAttributeCount> BuildAttributeWeights() {
            return {
                0.55f, 0.55f, 0.55f,
                0.25f, 0.25f, 0.25f, 0.10f,
                0.70f, 0.70f,
                0.20f, 0.20f,
                0.12f, 0.12f, 0.12f, 0.12f
            };
        }
    }

    bool GenerateClusterSurfaceLod(
        const std::vector<ClusterVertex>& sourceVertices,
        const std::vector<uint32_t>& sourceIndices,
        const MeshLodGeneratorSettings& settings,
        MeshLodGeneratorResult& outResult) {

        outResult = {};
        const size_t sourceIndexCount = AlignIndexCountToTriangles(sourceIndices.size());
        if (sourceVertices.empty() || sourceIndexCount < 96u) {
            return false;
        }

        const float targetRatio = ClampRatio(settings.targetTriangleRatio);
        size_t targetIndexCount = AlignIndexCountToTriangles(
            static_cast<size_t>(std::floor(static_cast<double>(sourceIndexCount) * targetRatio)));
        targetIndexCount = (std::max)(size_t{ 48u }, targetIndexCount);
        if (targetIndexCount >= sourceIndexCount) {
            return false;
        }

        std::vector<unsigned int> sourceIndexData{};
        sourceIndexData.reserve(sourceIndexCount);
        for (size_t i = 0; i < sourceIndexCount; ++i) {
            sourceIndexData.push_back(static_cast<unsigned int>(sourceIndices[i]));
        }

        std::vector<float> positions{};
        std::vector<float> attributes{};
        BuildSimplifierStreams(sourceVertices, positions, attributes);

        std::vector<unsigned int> simplifiedIndices(sourceIndexCount);
        const std::array<float, kLodAttributeCount> attributeWeights = BuildAttributeWeights();
        const unsigned int options =
            (settings.lockOpenBorders ? meshopt_SimplifyLockBorder : 0u) |
            meshopt_SimplifyRegularizeLight;

        float resultError = 0.0f;
        size_t simplifiedIndexCount = 0u;
        if (settings.preserveAttributes) {
            simplifiedIndexCount = meshopt_simplifyWithAttributes(
                simplifiedIndices.data(),
                sourceIndexData.data(),
                sourceIndexCount,
                positions.data(),
                sourceVertices.size(),
                sizeof(float) * 3u,
                attributes.data(),
                sizeof(float) * kLodAttributeCount,
                attributeWeights.data(),
                kLodAttributeCount,
                nullptr,
                targetIndexCount,
                ClampTargetError(settings.targetError),
                options,
                &resultError);
        } else {
            simplifiedIndexCount = meshopt_simplify(
                simplifiedIndices.data(),
                sourceIndexData.data(),
                sourceIndexCount,
                positions.data(),
                sourceVertices.size(),
                sizeof(float) * 3u,
                targetIndexCount,
                ClampTargetError(settings.targetError),
                options,
                &resultError);
        }

        simplifiedIndexCount = AlignIndexCountToTriangles(simplifiedIndexCount);
        if (simplifiedIndexCount < 3u || simplifiedIndexCount >= sourceIndexCount) {
            return false;
        }

        simplifiedIndices.resize(simplifiedIndexCount);
        if (settings.optimizeVertexCache) {
            meshopt_optimizeVertexCache(
                simplifiedIndices.data(),
                simplifiedIndices.data(),
                simplifiedIndices.size(),
                sourceVertices.size());
        }

        if (!CompactLodVertices(sourceVertices, simplifiedIndices, outResult)) {
            return false;
        }

        const float scale = meshopt_simplifyScale(
            positions.data(),
            sourceVertices.size(),
            sizeof(float) * 3u);
        outResult.geometricError = resultError * scale;
        outResult.simplified = outResult.indices.size() < sourceIndexCount;
        return outResult.simplified;
    }

} // namespace HIKARI::TOOLS::GEOMETRY
