#include "Assets/Geometry/HIKARI_ClusteredGeometryCooker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Math/HIKARI_NormalMatrix.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Cluster/HIKARI_ClusterGeometryPacked.h"
#include "Tools/Geometry/HIKARI_MeshLodGenerator.h"
#include "../../../ThirdParty/meshoptimizer/src/meshoptimizer.h"

namespace HIKARI::ASSETS::GEOMETRY {

    namespace {
        using RENDER3D::CLUSTER::ClusterPage;
        using RENDER3D::CLUSTER::ClusterSurface;
        using RENDER3D::CLUSTER::ClusterSurfaceFlags;
        using RENDER3D::CLUSTER::ClusterSurfaceLodRange;
        using RENDER3D::CLUSTER::ClusterSurfaceSection;
        using RENDER3D::CLUSTER::ClusterSkinVertex;
        using RENDER3D::CLUSTER::ClusterVertex;
        using RENDER3D::CLUSTER::ClusteredGeometryAsset;
        using RENDER3D::CLUSTER::ClusteredGeometryBuildReport;
        using RENDER3D::CLUSTER::MeshCluster;
        using RENDER3D::CLUSTER::MeshletPrimitive;

        struct SourceTriangle {
            uint32_t i0 = 0;
            uint32_t i1 = 0;
            uint32_t i2 = 0;
            Bounds bounds{};
            MATH::Vec3 normal{ 0.0f, 1.0f, 0.0f };
            float area = 0.0f;
        };

        struct SurfaceWork {
            uint32_t nodeIndex = RENDER3D::CLUSTER::kInvalidClusterIndex;
            uint32_t meshIndex = 0;
            uint32_t primitiveIndex = 0;
            uint32_t materialIndex = 0;
            uint32_t flags = 0;
            std::vector<ClusterVertex> vertices{};
            std::vector<uint32_t> indices{};
        };

        struct SourceGeometryCounts {
            uint32_t staticTriangleCount = 0;
            uint32_t staticVertexCount = 0;
        };

        struct SurfaceLodBuildResult {
            SurfaceWork work{};
            float geometricError = 0.0f;
        };

        struct SurfaceSectionBuildSource {
            std::vector<uint32_t> triangleIndices{};
            std::vector<std::vector<uint32_t>> groups{};
        };

        struct SurfaceShapeAnalysis {
            float maxExtent = 0.0f;
            float midExtent = 0.0f;
            float minExtent = 0.0f;
            float normalCoherence = 0.0f;
            float dominantNormalRatio = 0.0f;
            bool coherentPlanar = false;
            bool largePlanar = false;
        };

        struct TrianglePartitionConfig {
            uint32_t minChunkTriangles = 1;
            uint32_t maxDepth = 1;
            float maxExtent = 1.0f;
            bool planarCoarsened = false;
        };

        std::vector<std::vector<uint32_t>> BuildClusterTriangleGroups(
            const std::vector<ClusterVertex>& vertices,
            const std::vector<SourceTriangle>& triangles,
            const ClusterCookSettings& settings,
            ClusteredGeometryBuildReport* report = nullptr);

        bool ShouldUsePermissiveOpaqueLods(uint32_t flags);
        uint32_t TriangleNormalBucket(const SourceTriangle& tri);
        SurfaceShapeAnalysis AnalyzeSurfaceShape(
            const std::vector<SourceTriangle>& triangles,
            const Bounds& bounds);
        TrianglePartitionConfig ResolveTrianglePartitionConfig(
            const ClusterCookSettings& settings,
            const SurfaceShapeAnalysis& analysis);

        bool ApplyMeshoptMeshletBounds(
            const ClusteredGeometryAsset& asset,
            const MeshCluster& cluster,
            MeshCluster& outCluster) {

            if (cluster.vertexCount == 0u ||
                cluster.primitiveCount == 0u ||
                cluster.firstVertex + cluster.vertexCount > asset.packedVertices.size() ||
                cluster.firstPrimitive + cluster.primitiveCount > asset.meshletPrimitives.size()) {
                return false;
            }

            std::vector<unsigned int> meshletVertices(cluster.vertexCount);
            std::iota(meshletVertices.begin(), meshletVertices.end(), 0u);

            std::vector<unsigned char> meshletTriangles{};
            meshletTriangles.reserve(static_cast<size_t>(cluster.primitiveCount) * 3u);
            for (uint32_t primitiveOffset = 0; primitiveOffset < cluster.primitiveCount; ++primitiveOffset) {
                const MeshletPrimitive& primitive =
                    asset.meshletPrimitives[cluster.firstPrimitive + primitiveOffset];
                if (primitive.i0 >= cluster.vertexCount ||
                    primitive.i1 >= cluster.vertexCount ||
                    primitive.i2 >= cluster.vertexCount) {
                    return false;
                }
                meshletTriangles.push_back(static_cast<unsigned char>(primitive.i0));
                meshletTriangles.push_back(static_cast<unsigned char>(primitive.i1));
                meshletTriangles.push_back(static_cast<unsigned char>(primitive.i2));
            }

            const ClusterVertex* firstVertex = asset.packedVertices.data() + cluster.firstVertex;
            const meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                meshletVertices.data(),
                meshletTriangles.data(),
                cluster.primitiveCount,
                &firstVertex->position.x,
                cluster.vertexCount,
                sizeof(ClusterVertex));

            if (!std::isfinite(bounds.radius) || bounds.radius <= 0.0f) {
                return false;
            }

            outCluster.sphereCenter = {
                bounds.center[0],
                bounds.center[1],
                bounds.center[2]
            };
            outCluster.sphereRadius = bounds.radius;
            outCluster.coneApex = {
                bounds.cone_apex[0],
                bounds.cone_apex[1],
                bounds.cone_apex[2]
            };
            outCluster.coneAxis = {
                bounds.cone_axis[0],
                bounds.cone_axis[1],
                bounds.cone_axis[2]
            };
            outCluster.coneCutoff = bounds.cone_cutoff;
            if (!MATH::IsFinite(outCluster.coneApex) ||
                !MATH::IsFinite(outCluster.coneAxis) ||
                !std::isfinite(outCluster.coneCutoff) ||
                MATH::Length(outCluster.coneAxis) <= 1.0e-5f) {
                outCluster.coneApex = outCluster.sphereCenter;
                outCluster.coneAxis = { 0.0f, 1.0f, 0.0f };
                outCluster.coneCutoff = 1.0f;
            }
            return true;
        }

        uint32_t ExpandMorton10(uint32_t value) {
            value &= 0x000003ffu;
            value = (value | (value << 16u)) & 0x030000ffu;
            value = (value | (value << 8u)) & 0x0300f00fu;
            value = (value | (value << 4u)) & 0x030c30c3u;
            value = (value | (value << 2u)) & 0x09249249u;
            return value;
        }

        uint32_t EncodeMorton3D(uint32_t x, uint32_t y, uint32_t z) {
            return
                (ExpandMorton10(x) << 2u) |
                (ExpandMorton10(y) << 1u) |
                ExpandMorton10(z);
        }

        uint32_t QuantizeMortonAxis(float value, float minValue, float extent) {
            if (!std::isfinite(value) ||
                !std::isfinite(minValue) ||
                !std::isfinite(extent) ||
                extent <= 1.0e-5f) {
                return 0u;
            }

            const float normalized = (std::max)(
                0.0f,
                (std::min)((value - minValue) / extent, 1.0f));
            return static_cast<uint32_t>(normalized * 1023.0f + 0.5f);
        }

        uint32_t ClusterMortonCode(
            const MeshCluster& cluster,
            const Bounds& rangeBounds) {

            const MATH::Vec3 center =
                (cluster.localBounds.min + cluster.localBounds.max) * 0.5f;
            const MATH::Vec3 extent = rangeBounds.max - rangeBounds.min;
            return EncodeMorton3D(
                QuantizeMortonAxis(center.x, rangeBounds.min.x, extent.x),
                QuantizeMortonAxis(center.y, rangeBounds.min.y, extent.y),
                QuantizeMortonAxis(center.z, rangeBounds.min.z, extent.z));
        }

        void SpatialSortClusterRangeForPages(
            uint32_t firstCluster,
            uint32_t clusterCount,
            ClusteredGeometryAsset& asset) {

            if (clusterCount <= 1u ||
                firstCluster >= asset.clusters.size() ||
                firstCluster + clusterCount > asset.clusters.size()) {
                return;
            }

            Bounds rangeBounds{};
            bool hasBounds = false;
            for (uint32_t i = 0; i < clusterCount; ++i) {
                const MeshCluster& cluster = asset.clusters[firstCluster + i];
                if (!BOUNDS::IsUsable(cluster.localBounds)) {
                    continue;
                }
                if (!hasBounds) {
                    rangeBounds = cluster.localBounds;
                } else {
                    rangeBounds.min.x = (std::min)(rangeBounds.min.x, cluster.localBounds.min.x);
                    rangeBounds.min.y = (std::min)(rangeBounds.min.y, cluster.localBounds.min.y);
                    rangeBounds.min.z = (std::min)(rangeBounds.min.z, cluster.localBounds.min.z);
                    rangeBounds.max.x = (std::max)(rangeBounds.max.x, cluster.localBounds.max.x);
                    rangeBounds.max.y = (std::max)(rangeBounds.max.y, cluster.localBounds.max.y);
                    rangeBounds.max.z = (std::max)(rangeBounds.max.z, cluster.localBounds.max.z);
                }
                hasBounds = true;
            }
            if (!hasBounds || !BOUNDS::IsUsable(rangeBounds)) {
                return;
            }

            auto begin = asset.clusters.begin() + firstCluster;
            auto end = begin + clusterCount;
            std::stable_sort(
                begin,
                end,
                [&](const MeshCluster& a, const MeshCluster& b) {
                    const uint32_t mortonA = ClusterMortonCode(a, rangeBounds);
                    const uint32_t mortonB = ClusterMortonCode(b, rangeBounds);
                    if (mortonA != mortonB) {
                        return mortonA < mortonB;
                    }
                    if (a.firstIndex != b.firstIndex) {
                        return a.firstIndex < b.firstIndex;
                    }
                    return a.firstPrimitive < b.firstPrimitive;
                });
        }

        MATH::Vec3 TransformVector(const MATH::Mat4& matrix, const MATH::Vec3& value) {
            const MATH::Vec4 transformed = matrix.TransformPoint({ value.x, value.y, value.z, 0.0f });
            return { transformed.x, transformed.y, transformed.z };
        }

        ClusterVertex ToClusterVertex(
            const Vertex3D& source,
            const MATH::Mat4& matrix,
            const MATH::Mat4& normalMatrix) {

            ClusterVertex out{};
            const MATH::Vec4 p = matrix.TransformPoint({ source.position.x, source.position.y, source.position.z, 1.0f });
            out.position = { p.x, p.y, p.z };
            out.normal = MATH::Normalize(TransformVector(normalMatrix, source.normal));
            if (MATH::Length(out.normal) <= 1e-5f) {
                out.normal = { 0.0f, 1.0f, 0.0f };
            }
            const MATH::Vec3 tangent = MATH::Normalize(TransformVector(
                normalMatrix,
                { source.tangent.x, source.tangent.y, source.tangent.z }));
            out.tangent = {
                tangent.x,
                tangent.y,
                tangent.z,
                source.tangent.w == 0.0f ? 1.0f : source.tangent.w
            };
            out.uv0 = source.uv0;
            out.uv1 = source.uv1;
            out.color = source.color0;
            return out;
        }

        ClusterVertex ToClusterVertex(const SkinnedVertex3D& source) {
            ClusterVertex out{};
            out.position = source.position;
            out.normal = MATH::Normalize(source.normal);
            if (MATH::Length(out.normal) <= 1e-5f) {
                out.normal = { 0.0f, 1.0f, 0.0f };
            }
            MATH::Vec3 tangent = MATH::Normalize({
                source.tangent.x,
                source.tangent.y,
                source.tangent.z
            });
            if (MATH::Length(tangent) <= 1e-5f) {
                tangent = { 1.0f, 0.0f, 0.0f };
            }
            out.tangent = {
                tangent.x,
                tangent.y,
                tangent.z,
                source.tangent.w == 0.0f ? 1.0f : source.tangent.w
            };
            out.uv0 = source.uv0;
            out.uv1 = source.uv1;
            out.color = source.color0;

            float weightSum = 0.0f;
            for (size_t i = 0; i < 4u; ++i) {
                out.joints[i] = source.joints[i];
                out.weights[i] = (std::max)(0.0f, source.weights[i]);
                weightSum += out.weights[i];
            }
            if (weightSum <= 1e-8f) {
                out.joints[0] = 0u;
                out.weights[0] = 1.0f;
            } else {
                const float inverseWeightSum = 1.0f / weightSum;
                for (float& weight : out.weights) {
                    weight *= inverseWeightSum;
                }
            }
            return out;
        }

        void EncapsulatePoint(Bounds& bounds, bool& hasBounds, const MATH::Vec3& point) {
            if (!MATH::IsFinite(point)) {
                return;
            }
            if (!hasBounds) {
                bounds.min = point;
                bounds.max = point;
                hasBounds = true;
                return;
            }
            BOUNDS::Encapsulate(bounds, point);
        }

        Bounds ComputeVertexBounds(const std::vector<ClusterVertex>& vertices) {
            Bounds bounds{};
            bool hasBounds = false;
            for (const ClusterVertex& vertex : vertices) {
                EncapsulatePoint(bounds, hasBounds, vertex.position);
            }
            return hasBounds ? bounds : Bounds{};
        }

        Bounds MergeBounds(const Bounds& a, const Bounds& b) {
            if (!BOUNDS::IsUsable(a)) {
                return b;
            }
            if (!BOUNDS::IsUsable(b)) {
                return a;
            }
            Bounds out = a;
            BOUNDS::Encapsulate(out, b);
            return out;
        }

        float BoundsVolume(const Bounds& bounds) {
            if (!BOUNDS::IsUsable(bounds)) {
                return 0.0f;
            }
            const MATH::Vec3 extent = bounds.max - bounds.min;
            return (std::max)(0.0f, extent.x) *
                (std::max)(0.0f, extent.y) *
                (std::max)(0.0f, extent.z);
        }

        SourceTriangle BuildTriangle(
            uint32_t i0,
            uint32_t i1,
            uint32_t i2,
            const std::vector<ClusterVertex>& vertices) {

            SourceTriangle tri{};
            tri.i0 = i0;
            tri.i1 = i1;
            tri.i2 = i2;

            Bounds bounds{};
            bool hasBounds = false;
            EncapsulatePoint(bounds, hasBounds, vertices[i0].position);
            EncapsulatePoint(bounds, hasBounds, vertices[i1].position);
            EncapsulatePoint(bounds, hasBounds, vertices[i2].position);
            tri.bounds = hasBounds ? bounds : Bounds{};

            const MATH::Vec3 e1 = vertices[i1].position - vertices[i0].position;
            const MATH::Vec3 e2 = vertices[i2].position - vertices[i0].position;
            const MATH::Vec3 cross = MATH::Cross(e1, e2);
            tri.area = MATH::Length(cross) * 0.5f;
            tri.normal = MATH::Normalize(cross);
            if (MATH::Length(tri.normal) <= 1e-5f) {
                tri.normal = { 0.0f, 1.0f, 0.0f };
            }
            return tri;
        }

        bool HasMissingNormals(const std::vector<ClusterVertex>& vertices) {
            for (const ClusterVertex& vertex : vertices) {
                if (MATH::Length(vertex.normal) <= 1e-5f) {
                    return true;
                }
            }
            return false;
        }

        bool HasMissingTangents(const std::vector<ClusterVertex>& vertices) {
            for (const ClusterVertex& vertex : vertices) {
                const MATH::Vec3 t{ vertex.tangent.x, vertex.tangent.y, vertex.tangent.z };
                if (MATH::Length(t) <= 1e-5f) {
                    return true;
                }
            }
            return false;
        }

        void GenerateNormals(std::vector<ClusterVertex>& vertices, const std::vector<uint32_t>& indices) {
            std::vector<MATH::Vec3> sums(vertices.size(), {});
            for (size_t i = 0; i + 2u < indices.size(); i += 3u) {
                const uint32_t i0 = indices[i + 0u];
                const uint32_t i1 = indices[i + 1u];
                const uint32_t i2 = indices[i + 2u];
                if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                    continue;
                }
                const MATH::Vec3 e1 = vertices[i1].position - vertices[i0].position;
                const MATH::Vec3 e2 = vertices[i2].position - vertices[i0].position;
                MATH::Vec3 n = MATH::Cross(e1, e2);
                if (MATH::Length(n) <= 1e-5f) {
                    continue;
                }
                sums[i0] = sums[i0] + n;
                sums[i1] = sums[i1] + n;
                sums[i2] = sums[i2] + n;
            }
            for (size_t i = 0; i < vertices.size(); ++i) {
                MATH::Vec3 n = MATH::Normalize(sums[i]);
                if (MATH::Length(n) <= 1e-5f) {
                    n = { 0.0f, 1.0f, 0.0f };
                }
                vertices[i].normal = n;
            }
        }

        void GenerateTangents(std::vector<ClusterVertex>& vertices, const std::vector<uint32_t>& indices) {
            std::vector<MATH::Vec3> sums(vertices.size(), {});
            for (size_t i = 0; i + 2u < indices.size(); i += 3u) {
                const uint32_t i0 = indices[i + 0u];
                const uint32_t i1 = indices[i + 1u];
                const uint32_t i2 = indices[i + 2u];
                if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                    continue;
                }

                const MATH::Vec3 p0 = vertices[i0].position;
                const MATH::Vec3 p1 = vertices[i1].position;
                const MATH::Vec3 p2 = vertices[i2].position;
                const MATH::Vec2 uv0 = vertices[i0].uv0;
                const MATH::Vec2 uv1 = vertices[i1].uv0;
                const MATH::Vec2 uv2 = vertices[i2].uv0;
                const MATH::Vec3 e1 = p1 - p0;
                const MATH::Vec3 e2 = p2 - p0;
                const MATH::Vec2 duv1 = uv1 - uv0;
                const MATH::Vec2 duv2 = uv2 - uv0;
                const float denom = duv1.x * duv2.y - duv2.x * duv1.y;
                if (std::abs(denom) <= 1e-8f) {
                    continue;
                }
                const float r = 1.0f / denom;
                const MATH::Vec3 tangent = (e1 * duv2.y - e2 * duv1.y) * r;
                sums[i0] = sums[i0] + tangent;
                sums[i1] = sums[i1] + tangent;
                sums[i2] = sums[i2] + tangent;
            }

            for (size_t i = 0; i < vertices.size(); ++i) {
                const MATH::Vec3 n = MATH::Normalize(vertices[i].normal);
                MATH::Vec3 t = sums[i];
                t = t - n * MATH::Dot(n, t);
                t = MATH::Normalize(t);
                if (MATH::Length(t) <= 1e-5f) {
                    t = { 1.0f, 0.0f, 0.0f };
                }
                vertices[i].tangent = { t.x, t.y, t.z, 1.0f };
            }
        }

        uint32_t BuildSurfaceFlags(const ModelAsset& model, const MeshPrimitive& primitive, int nodeSkinIndex) {
            uint32_t flags = 0;
            const MaterialAsset* material = primitive.materialIndex < model.materials.size()
                ? &model.materials[primitive.materialIndex]
                : nullptr;

            if (material == nullptr || material->alphaMode == AlphaMode::Opaque) {
                RENDER3D::CLUSTER::AddFlag(flags, ClusterSurfaceFlags::Opaque);
            } else if (material->alphaMode == AlphaMode::Mask) {
                RENDER3D::CLUSTER::AddFlag(flags, ClusterSurfaceFlags::AlphaMask);
            } else {
                RENDER3D::CLUSTER::AddFlag(flags, ClusterSurfaceFlags::Transparent);
            }
            // material の doubleSided は描画結果の契約なので、cluster cook でも保持する。
            if (material != nullptr && SURFACE_POLICY::ShouldRenderDoubleSided(*material, primitive)) {
                RENDER3D::CLUSTER::AddFlag(flags, ClusterSurfaceFlags::DoubleSided);
            }
            if (primitive.layout == VertexLayoutKind::SkinnedPNTTJW ||
                !primitive.skinnedVertices.empty() ||
                nodeSkinIndex >= 0) {
                RENDER3D::CLUSTER::AddFlag(flags, ClusterSurfaceFlags::Skinned);
            }
            const bool skinned =
                primitive.layout == VertexLayoutKind::SkinnedPNTTJW ||
                !primitive.skinnedVertices.empty() ||
                nodeSkinIndex >= 0;
            if (primitive.indices.size() < 3u ||
                (skinned
                    ? primitive.skinnedVertices.empty()
                    : primitive.staticVertices.empty())) {
                RENDER3D::CLUSTER::AddFlag(flags, ClusterSurfaceFlags::Unsupported);
            }
            return flags;
        }

        bool IsClusterablePrimitive(
            const ModelAsset& model,
            const MeshPrimitive& primitive,
            int nodeSkinIndex) {

            const uint32_t flags = BuildSurfaceFlags(model, primitive, nodeSkinIndex);
            return !primitive.hasMorphTargets &&
                !RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::Unsupported);
        }

        void AccumulateSourcePrimitiveCounts(
            const ModelAsset& model,
            const MeshPrimitive& primitive,
            int nodeSkinIndex,
            SourceGeometryCounts& counts) {

            if (!IsClusterablePrimitive(model, primitive, nodeSkinIndex)) {
                return;
            }

            counts.staticTriangleCount += static_cast<uint32_t>(primitive.indices.size() / 3u);
            const bool skinned =
                primitive.layout == VertexLayoutKind::SkinnedPNTTJW ||
                !primitive.skinnedVertices.empty() ||
                nodeSkinIndex >= 0;
            counts.staticVertexCount += static_cast<uint32_t>(
                skinned
                    ? primitive.skinnedVertices.size()
                    : primitive.staticVertices.size());
        }

        SourceGeometryCounts CountSourceGeometry(const ModelAsset& model) {
            SourceGeometryCounts counts{};
            if (!model.nodes.empty()) {
                for (const ModelNode& node : model.nodes) {
                    if (node.meshIndex < 0 || node.meshIndex >= static_cast<int>(model.meshes.size())) {
                        continue;
                    }
                    const MeshAsset& mesh = model.meshes[static_cast<size_t>(node.meshIndex)];
                    for (const MeshPrimitive& primitive : mesh.primitives) {
                        AccumulateSourcePrimitiveCounts(model, primitive, node.skinIndex, counts);
                    }
                }
                return counts;
            }

            for (const MeshAsset& mesh : model.meshes) {
                for (const MeshPrimitive& primitive : mesh.primitives) {
                    AccumulateSourcePrimitiveCounts(model, primitive, -1, counts);
                }
            }
            return counts;
        }

        std::vector<SourceTriangle> BuildTriangles(
            const std::vector<ClusterVertex>& vertices,
            const std::vector<uint32_t>& indices) {

            std::vector<SourceTriangle> triangles;
            triangles.reserve(indices.size() / 3u);
            for (size_t i = 0; i + 2u < indices.size(); i += 3u) {
                const uint32_t i0 = indices[i + 0u];
                const uint32_t i1 = indices[i + 1u];
                const uint32_t i2 = indices[i + 2u];
                if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                    continue;
                }
                if (i0 == i1 || i1 == i2 || i0 == i2) {
                    continue;
                }
                triangles.push_back(BuildTriangle(i0, i1, i2, vertices));
            }
            return triangles;
        }

        uint32_t CountWorkTriangles(const SurfaceWork& work) {
            return static_cast<uint32_t>(work.indices.size() / 3u);
        }

        float Distance(const MATH::Vec3& a, const MATH::Vec3& b) {
            const MATH::Vec3 d = a - b;
            return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        }

        float TriangleMaxEdgeLength(
            const std::vector<ClusterVertex>& vertices,
            uint32_t i0,
            uint32_t i1,
            uint32_t i2) {

            if (i0 >= vertices.size() ||
                i1 >= vertices.size() ||
                i2 >= vertices.size()) {
                return 0.0f;
            }

            return (std::max)(
                Distance(vertices[i0].position, vertices[i1].position),
                (std::max)(
                    Distance(vertices[i1].position, vertices[i2].position),
                    Distance(vertices[i2].position, vertices[i0].position)));
        }

        MATH::Vec2 LerpVec2(const MATH::Vec2& a, const MATH::Vec2& b, float t) {
            return {
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t
            };
        }

        MATH::Vec3 LerpVec3(const MATH::Vec3& a, const MATH::Vec3& b, float t) {
            return {
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t
            };
        }

        MATH::Vec4 LerpVec4(const MATH::Vec4& a, const MATH::Vec4& b, float t) {
            return {
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t
            };
        }

        ClusterVertex LerpClusterVertex(
            const ClusterVertex& a,
            const ClusterVertex& b,
            float t) {

            ClusterVertex out{};
            out.position = LerpVec3(a.position, b.position, t);
            out.normal = MATH::Normalize(LerpVec3(a.normal, b.normal, t));
            if (MATH::Length(out.normal) <= 1.0e-5f) {
                out.normal = MATH::Length(a.normal) > 1.0e-5f
                    ? a.normal
                    : MATH::Vec3{ 0.0f, 1.0f, 0.0f };
            }

            const MATH::Vec3 tangentA{ a.tangent.x, a.tangent.y, a.tangent.z };
            const MATH::Vec3 tangentB{ b.tangent.x, b.tangent.y, b.tangent.z };
            MATH::Vec3 tangent = MATH::Normalize(LerpVec3(tangentA, tangentB, t));
            if (MATH::Length(tangent) <= 1.0e-5f) {
                tangent = MATH::Length(tangentA) > 1.0e-5f
                    ? tangentA
                    : MATH::Vec3{ 1.0f, 0.0f, 0.0f };
            }
            const float handedness =
                std::abs(a.tangent.w) >= std::abs(b.tangent.w)
                    ? a.tangent.w
                    : b.tangent.w;
            out.tangent = {
                tangent.x,
                tangent.y,
                tangent.z,
                std::abs(handedness) <= 1.0e-5f ? 1.0f : handedness
            };
            out.uv0 = LerpVec2(a.uv0, b.uv0, t);
            out.uv1 = LerpVec2(a.uv1, b.uv1, t);
            out.color = LerpVec4(a.color, b.color, t);

            struct Influence {
                uint16_t joint = 0;
                float weight = 0.0f;
            };
            std::array<Influence, 8> influences{};
            size_t influenceCount = 0;
            const auto appendInfluence = [&](uint16_t joint, float weight) {
                if (weight <= 1e-8f) {
                    return;
                }
                for (size_t i = 0; i < influenceCount; ++i) {
                    if (influences[i].joint == joint) {
                        influences[i].weight += weight;
                        return;
                    }
                }
                if (influenceCount < influences.size()) {
                    influences[influenceCount++] = { joint, weight };
                }
            };
            for (size_t i = 0; i < 4u; ++i) {
                appendInfluence(a.joints[i], a.weights[i] * (1.0f - t));
                appendInfluence(b.joints[i], b.weights[i] * t);
            }
            std::sort(
                influences.begin(),
                influences.begin() + influenceCount,
                [](const Influence& lhs, const Influence& rhs) {
                    return lhs.weight > rhs.weight;
                });
            const size_t retainedInfluenceCount =
                (std::min)(influenceCount, size_t{ 4u });
            float retainedWeightSum = 0.0f;
            for (size_t i = 0; i < retainedInfluenceCount; ++i) {
                out.joints[i] = influences[i].joint;
                out.weights[i] = influences[i].weight;
                retainedWeightSum += influences[i].weight;
            }
            if (retainedWeightSum > 1e-8f) {
                const float inverseWeightSum = 1.0f / retainedWeightSum;
                for (size_t i = 0; i < retainedInfluenceCount; ++i) {
                    out.weights[i] *= inverseWeightSum;
                }
            }
            return out;
        }

        struct TriangleSubdivisionStats {
            uint32_t sourceTriangleCount = 0;
            uint32_t outputTriangleCount = 0;
            bool budgetReached = false;
        };

        uint32_t AppendMidpointVertex(
            std::vector<ClusterVertex>& vertices,
            uint32_t a,
            uint32_t b) {

            if (a >= vertices.size() || b >= vertices.size()) {
                return a;
            }
            if (vertices.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
                return a;
            }

            const uint32_t index = static_cast<uint32_t>(vertices.size());
            vertices.push_back(LerpClusterVertex(vertices[a], vertices[b], 0.5f));
            return index;
        }

        void AppendTriangleIndices(
            std::vector<uint32_t>& indices,
            uint32_t i0,
            uint32_t i1,
            uint32_t i2,
            TriangleSubdivisionStats& stats) {

            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);
            ++stats.outputTriangleCount;
        }

        void AppendSubdividedTriangleRecursive(
            std::vector<ClusterVertex>& vertices,
            std::vector<uint32_t>& indices,
            uint32_t i0,
            uint32_t i1,
            uint32_t i2,
            float maxEdgeLength,
            uint32_t maxDepth,
            uint32_t depth,
            uint32_t maxGeneratedTriangles,
            TriangleSubdivisionStats& stats) {

            if (stats.outputTriangleCount >= maxGeneratedTriangles) {
                stats.budgetReached = true;
                AppendTriangleIndices(indices, i0, i1, i2, stats);
                return;
            }

            const float e01 = i0 < vertices.size() && i1 < vertices.size()
                ? Distance(vertices[i0].position, vertices[i1].position)
                : 0.0f;
            const float e12 = i1 < vertices.size() && i2 < vertices.size()
                ? Distance(vertices[i1].position, vertices[i2].position)
                : 0.0f;
            const float e20 = i2 < vertices.size() && i0 < vertices.size()
                ? Distance(vertices[i2].position, vertices[i0].position)
                : 0.0f;
            const float longest = (std::max)(e01, (std::max)(e12, e20));
            if (longest <= maxEdgeLength ||
                depth >= maxDepth ||
                stats.outputTriangleCount + 2u > maxGeneratedTriangles) {
                AppendTriangleIndices(indices, i0, i1, i2, stats);
                if (longest > maxEdgeLength &&
                    (depth >= maxDepth || stats.outputTriangleCount >= maxGeneratedTriangles)) {
                    stats.budgetReached = true;
                }
                return;
            }

            if (e01 >= e12 && e01 >= e20) {
                const uint32_t mid = AppendMidpointVertex(vertices, i0, i1);
                AppendSubdividedTriangleRecursive(
                    vertices, indices, i0, mid, i2, maxEdgeLength,
                    maxDepth, depth + 1u, maxGeneratedTriangles, stats);
                AppendSubdividedTriangleRecursive(
                    vertices, indices, mid, i1, i2, maxEdgeLength,
                    maxDepth, depth + 1u, maxGeneratedTriangles, stats);
            } else if (e12 >= e20) {
                const uint32_t mid = AppendMidpointVertex(vertices, i1, i2);
                AppendSubdividedTriangleRecursive(
                    vertices, indices, i1, mid, i0, maxEdgeLength,
                    maxDepth, depth + 1u, maxGeneratedTriangles, stats);
                AppendSubdividedTriangleRecursive(
                    vertices, indices, mid, i2, i0, maxEdgeLength,
                    maxDepth, depth + 1u, maxGeneratedTriangles, stats);
            } else {
                const uint32_t mid = AppendMidpointVertex(vertices, i2, i0);
                AppendSubdividedTriangleRecursive(
                    vertices, indices, i2, mid, i1, maxEdgeLength,
                    maxDepth, depth + 1u, maxGeneratedTriangles, stats);
                AppendSubdividedTriangleRecursive(
                    vertices, indices, mid, i0, i1, maxEdgeLength,
                    maxDepth, depth + 1u, maxGeneratedTriangles, stats);
            }
        }

        bool ShouldSubdivideLargeStaticTriangles(
            const SurfaceWork& work,
            const ClusterCookSettings& settings) {

            return
                settings.subdivideLargeStaticTriangles &&
                settings.surfacePartitionPolicy == SurfacePartitionPolicy::SceneStatic &&
                ShouldUsePermissiveOpaqueLods(work.flags) &&
                work.vertices.size() >= 3u &&
                work.indices.size() >= 3u &&
                settings.largeStaticTriangleMaxEdgeLength > 0.0f &&
                settings.largeStaticTriangleMaxSubdivisions > 0u &&
                settings.largeStaticTriangleMaxGeneratedTriangles > 0u;
        }

        SurfaceWork SubdivideLargeStaticTriangles(
            const SurfaceWork& work,
            const ClusterCookSettings& settings,
            ClusteredGeometryBuildReport& report) {

            if (!ShouldSubdivideLargeStaticTriangles(work, settings)) {
                return work;
            }

            const std::vector<SourceTriangle> sourceTriangles =
                BuildTriangles(work.vertices, work.indices);
            const SurfaceShapeAnalysis shapeAnalysis =
                AnalyzeSurfaceShape(sourceTriangles, ComputeVertexBounds(work.vertices));
            const bool balancePlanarSubdivision =
                settings.balancePlanarStaticSurfaces &&
                settings.surfacePartitionPolicy == SurfacePartitionPolicy::SceneStatic &&
                shapeAnalysis.largePlanar;
            if (balancePlanarSubdivision &&
                sourceTriangles.size() >= (std::max)(
                    settings.largeSurfacePartitionMinTriangles / 2u,
                    256u)) {
                ++report.planarSubdivisionSkippedSurfaceCount;
                return work;
            }

            SurfaceWork refined = work;
            refined.indices.clear();
            refined.indices.reserve(work.indices.size());
            float maxEdgeLength =
                (std::max)(0.1f, settings.largeStaticTriangleMaxEdgeLength);
            uint32_t maxDepth =
                (std::max)(1u, settings.largeStaticTriangleMaxSubdivisions);
            uint32_t maxGeneratedTriangles =
                (std::max)(1u, settings.largeStaticTriangleMaxGeneratedTriangles);
            if (balancePlanarSubdivision) {
                maxEdgeLength = (std::max)(
                    maxEdgeLength,
                    (std::max)(2.0f, settings.planarStaticSurfaceMinPartitionExtent * 0.50f));
                maxDepth = (std::min)(maxDepth, 4u);
                maxGeneratedTriangles = (std::min)(maxGeneratedTriangles, 16384u);
                ++report.planarSubdivisionCoarsenedSurfaceCount;
            }

            TriangleSubdivisionStats stats{};
            for (size_t i = 0; i + 2u < work.indices.size(); i += 3u) {
                const uint32_t i0 = work.indices[i + 0u];
                const uint32_t i1 = work.indices[i + 1u];
                const uint32_t i2 = work.indices[i + 2u];
                if (i0 >= refined.vertices.size() ||
                    i1 >= refined.vertices.size() ||
                    i2 >= refined.vertices.size()) {
                    continue;
                }

                const float maxEdge = TriangleMaxEdgeLength(refined.vertices, i0, i1, i2);
                if (maxEdge > maxEdgeLength) {
                    ++stats.sourceTriangleCount;
                }
                if (stats.budgetReached) {
                    AppendTriangleIndices(refined.indices, i0, i1, i2, stats);
                    continue;
                }
                AppendSubdividedTriangleRecursive(
                    refined.vertices,
                    refined.indices,
                    i0,
                    i1,
                    i2,
                    maxEdgeLength,
                    maxDepth,
                    0u,
                    maxGeneratedTriangles,
                    stats);
            }

            if (stats.sourceTriangleCount == 0u ||
                refined.indices.size() <= work.indices.size()) {
                return work;
            }

            ++report.subdividedSurfaceCount;
            report.subdividedSourceTriangleCount += stats.sourceTriangleCount;
            report.subdividedOutputTriangleCount += stats.outputTriangleCount;
            if (stats.budgetReached) {
                report.messages.push_back(
                    "large static triangle subdivision reached per-surface budget");
            }
            return refined;
        }

        bool ShouldBuildReducedLods(uint32_t flags) {
            if (RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::Transparent)) {
                return false;
            }
            return true;
        }

        bool ShouldUsePermissiveOpaqueLods(uint32_t flags) {
            return RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::Opaque) &&
                !RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::AlphaMask) &&
                !RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::Transparent) &&
                !RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::DoubleSided) &&
                !RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::Skinned) &&
                !RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::Unsupported);
        }

        float BoundsExtentOnAxis(const Bounds& bounds, uint32_t axis) {
            switch (axis) {
            case 0u:
                return (std::max)(0.0f, bounds.max.x - bounds.min.x);
            case 1u:
                return (std::max)(0.0f, bounds.max.y - bounds.min.y);
            default:
                return (std::max)(0.0f, bounds.max.z - bounds.min.z);
            }
        }

        uint32_t LongestBoundsAxis(const Bounds& bounds) {
            const float extentX = BoundsExtentOnAxis(bounds, 0u);
            const float extentY = BoundsExtentOnAxis(bounds, 1u);
            const float extentZ = BoundsExtentOnAxis(bounds, 2u);
            if (extentX >= extentY && extentX >= extentZ) {
                return 0u;
            }
            return extentY >= extentZ ? 1u : 2u;
        }

        float MaxBoundsExtent(const Bounds& bounds) {
            return (std::max)(
                BoundsExtentOnAxis(bounds, 0u),
                (std::max)(BoundsExtentOnAxis(bounds, 1u), BoundsExtentOnAxis(bounds, 2u)));
        }

        MATH::Vec3 BoundsCenter(const Bounds& bounds) {
            return {
                (bounds.min.x + bounds.max.x) * 0.5f,
                (bounds.min.y + bounds.max.y) * 0.5f,
                (bounds.min.z + bounds.max.z) * 0.5f,
            };
        }

        float BoundsRadius(const Bounds& bounds) {
            const MATH::Vec3 center = BoundsCenter(bounds);
            const MATH::Vec3 extent{
                (std::max)(bounds.max.x - center.x, 0.0f),
                (std::max)(bounds.max.y - center.y, 0.0f),
                (std::max)(bounds.max.z - center.z, 0.0f),
            };
            return std::sqrt(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z);
        }

        Bounds BuildCenterRadiusBounds(const MATH::Vec3& center, float radius) {
            const float safeRadius = (std::max)(radius, 0.0f);
            Bounds bounds{};
            bounds.min = {
                center.x - safeRadius,
                center.y - safeRadius,
                center.z - safeRadius,
            };
            bounds.max = {
                center.x + safeRadius,
                center.y + safeRadius,
                center.z + safeRadius,
            };
            return bounds;
        }

        float ResolveSectionLodErrorBudgetNdc(const ClusterCookSettings& settings) {
            float budget = settings.lod1TargetError;
            if (settings.surfacePartitionPolicy == SurfacePartitionPolicy::SceneStatic) {
                budget = (std::max)(budget, settings.lod2TargetError * 0.80f);
            } else if (settings.surfacePartitionPolicy == SurfacePartitionPolicy::CharacterStatic) {
                budget = (std::max)(budget, settings.lod2TargetError * 0.75f);
            }

            // 後段 LOD は section 単位で選ばれるため、LOD1 だけの誤差予算だと
            // LOD3/4 が遠距離まで選ばれにくい。近景は screen radius で守る。
            return (std::max)(budget, 0.0005f);
        }

        Bounds ResolveSectionLodMetricBounds(
            const ClusterCookSettings& settings,
            const Bounds& sectionBounds,
            const Bounds& surfaceBounds,
            const Bounds& assetBounds,
            size_t assetSurfaceCount,
            bool partitioned) {

            if (!BOUNDS::IsUsable(sectionBounds)) {
                return sectionBounds;
            }

            const float sectionRadius = BoundsRadius(sectionBounds);
            if (sectionRadius <= 0.000001f) {
                return sectionBounds;
            }

            if (partitioned) {
                if (settings.surfacePartitionPolicy != SurfacePartitionPolicy::SceneStatic) {
                    return sectionBounds;
                }

                // Scene の平面 section は見た目の対角線で LOD 判定が引っ張られやすい。
                // 可視判定用 bounds はそのまま使い、LOD metric だけを cook 時の目標粒度へ寄せる。
                const float targetRadius =
                    (std::max)(0.35f, settings.largeSurfacePartitionMaxExtent * 0.50f);
                const float metricRadius = (std::min)(sectionRadius, targetRadius);
                if (metricRadius >= sectionRadius * 0.98f) {
                    return sectionBounds;
                }
                return BuildCenterRadiusBounds(BoundsCenter(sectionBounds), metricRadius);
            }

            float referenceRadius = BOUNDS::IsUsable(surfaceBounds)
                ? BoundsRadius(surfaceBounds)
                : sectionRadius;

            // Character は部位ごとの LOD 判定を優先するため、asset 全体半径で section を膨らませない。
            const float compactAssetScale =
                settings.surfacePartitionPolicy == SurfacePartitionPolicy::CharacterStatic
                    ? 0.0f
                    : (settings.surfacePartitionPolicy == SurfacePartitionPolicy::SceneStatic ? 0.15f : 0.30f);
            if (compactAssetScale > 0.0f && assetSurfaceCount <= 16u && BOUNDS::IsUsable(assetBounds)) {
                const float assetRadius = BoundsRadius(assetBounds);
                referenceRadius = (std::max)(referenceRadius, assetRadius * compactAssetScale);
            }

            const float sectionMetricScale =
                settings.surfacePartitionPolicy == SurfacePartitionPolicy::CharacterStatic ? 0.20f : 0.35f;
            const float minMetricRadius = referenceRadius * sectionMetricScale;
            const float metricRadius = (std::max)(sectionRadius, minMetricRadius);
            if (metricRadius <= sectionRadius * 1.01f) {
                return sectionBounds;
            }

            return BuildCenterRadiusBounds(BoundsCenter(sectionBounds), metricRadius);
        }

        void ConfigureSectionLodMetric(
            ClusterSurfaceSection& section,
            const ClusterCookSettings& settings,
            const Bounds& metricBounds) {

            section.lodMetricBounds = BOUNDS::IsUsable(metricBounds)
                ? metricBounds
                : section.localBounds;
            section.lodErrorBudgetNdc = ResolveSectionLodErrorBudgetNdc(settings);
        }

        void FinalizeAssetLodMetrics(
            ClusteredGeometryAsset& asset,
            const ClusterCookSettings& settings) {

            for (ClusterSurfaceSection& section : asset.surfaceSections) {
                const ClusterSurface* surface = nullptr;
                if (section.surfaceIndex < asset.surfaces.size()) {
                    surface = &asset.surfaces[section.surfaceIndex];
                }

                const Bounds& surfaceBounds = surface != nullptr
                    ? surface->localBounds
                    : section.localBounds;
                const bool partitioned = surface != nullptr && surface->sectionCount > 1u;

                ConfigureSectionLodMetric(
                    section,
                    settings,
                    ResolveSectionLodMetricBounds(
                        settings,
                        section.localBounds,
                        surfaceBounds,
                        asset.localBounds,
                        asset.surfaces.size(),
                        partitioned));
            }
        }

        float TriangleCenterOnAxis(const SourceTriangle& tri, uint32_t axis) {
            switch (axis) {
            case 0u:
                return (tri.bounds.min.x + tri.bounds.max.x) * 0.5f;
            case 1u:
                return (tri.bounds.min.y + tri.bounds.max.y) * 0.5f;
            default:
                return (tri.bounds.min.z + tri.bounds.max.z) * 0.5f;
            }
        }

        uint32_t TriangleNormalBucket(const SourceTriangle& tri) {
            const float ax = std::abs(tri.normal.x);
            const float ay = std::abs(tri.normal.y);
            const float az = std::abs(tri.normal.z);
            if (ax >= ay && ax >= az) {
                return tri.normal.x >= 0.0f ? 0u : 1u;
            }
            if (ay >= az) {
                return tri.normal.y >= 0.0f ? 2u : 3u;
            }
            return tri.normal.z >= 0.0f ? 4u : 5u;
        }

        SurfaceShapeAnalysis AnalyzeSurfaceShape(
            const std::vector<SourceTriangle>& triangles,
            const Bounds& bounds) {

            SurfaceShapeAnalysis analysis{};
            if (triangles.empty() || !BOUNDS::IsUsable(bounds)) {
                return analysis;
            }

            std::array<float, 3u> extents{
                BoundsExtentOnAxis(bounds, 0u),
                BoundsExtentOnAxis(bounds, 1u),
                BoundsExtentOnAxis(bounds, 2u),
            };
            std::sort(extents.begin(), extents.end());
            analysis.minExtent = extents[0];
            analysis.midExtent = extents[1];
            analysis.maxExtent = extents[2];

            double totalArea = 0.0;
            double bucketAreaMax = 0.0;
            std::array<double, 6u> bucketAreas{};
            MATH::Vec3 normalSum{};
            for (const SourceTriangle& tri : triangles) {
                const float weight = std::isfinite(tri.area) && tri.area > 0.0f
                    ? tri.area
                    : 1.0f;
                totalArea += static_cast<double>(weight);
                normalSum = normalSum + tri.normal * weight;
                bucketAreas[TriangleNormalBucket(tri)] += static_cast<double>(weight);
            }

            for (double bucketArea : bucketAreas) {
                bucketAreaMax = (std::max)(bucketAreaMax, bucketArea);
            }

            if (totalArea > 0.0) {
                analysis.normalCoherence =
                    static_cast<float>(MATH::Length(normalSum) / totalArea);
                analysis.dominantNormalRatio =
                    static_cast<float>(bucketAreaMax / totalArea);
            }

            const bool thinBounds =
                analysis.maxExtent > 0.0001f &&
                analysis.midExtent > 0.0001f &&
                analysis.minExtent <= (std::max)(0.08f, analysis.maxExtent * 0.04f);
            analysis.coherentPlanar =
                analysis.normalCoherence >= 0.88f &&
                analysis.dominantNormalRatio >= 0.82f;
            analysis.largePlanar =
                analysis.coherentPlanar &&
                thinBounds &&
                analysis.maxExtent >= 1.5f;
            return analysis;
        }

        TrianglePartitionConfig ResolveTrianglePartitionConfig(
            const ClusterCookSettings& settings,
            const SurfaceShapeAnalysis& analysis) {

            TrianglePartitionConfig config{};
            config.minChunkTriangles =
                (std::max)(1u, settings.largeSurfacePartitionMinTrianglesPerChunk);
            const uint32_t minChunkByClusterEstimate =
                (std::max)(1u, settings.maxTrianglesPerCluster) *
                (std::max)(1u, settings.minPartitionClusterEstimate);
            config.minChunkTriangles =
                (std::max)(config.minChunkTriangles, minChunkByClusterEstimate);
            config.maxDepth = (std::max)(1u, settings.largeSurfacePartitionMaxDepth);
            config.maxExtent =
                (std::max)(0.25f, settings.largeSurfacePartitionMaxExtent);

            if (!settings.balancePlanarStaticSurfaces ||
                settings.surfacePartitionPolicy != SurfacePartitionPolicy::SceneStatic) {
                return config;
            }

            if (analysis.largePlanar) {
                config.planarCoarsened = true;
                config.maxExtent = (std::max)(
                    config.maxExtent,
                    (std::max)(settings.planarStaticSurfaceMinPartitionExtent, 3.0f));
                config.minChunkTriangles = (std::max)(
                    config.minChunkTriangles,
                    (std::max)(
                        settings.planarStaticSurfaceMinTrianglesPerChunk,
                        settings.maxTrianglesPerCluster * 12u));
                config.maxDepth = (std::min)(
                    config.maxDepth,
                    (std::max)(1u, settings.planarStaticSurfaceMaxDepth));
            } else if (analysis.coherentPlanar) {
                config.maxExtent = (std::max)(config.maxExtent, 1.5f);
                config.minChunkTriangles = (std::max)(
                    config.minChunkTriangles,
                    settings.maxTrianglesPerCluster * 8u);
                config.maxDepth = (std::min)(config.maxDepth, 5u);
            } else {
                config.maxExtent = (std::max)(config.maxExtent, 0.75f);
                config.minChunkTriangles = (std::max)(
                    config.minChunkTriangles,
                    settings.maxTrianglesPerCluster * 4u);
            }

            return config;
        }

        Bounds ComputeTriangleSubsetBounds(
            const std::vector<SourceTriangle>& triangles,
            const std::vector<uint32_t>& triangleIndices) {

            Bounds bounds{};
            bool hasBounds = false;
            for (uint32_t triangleIndex : triangleIndices) {
                if (triangleIndex >= triangles.size()) {
                    continue;
                }
                const Bounds& triBounds = triangles[triangleIndex].bounds;
                if (!BOUNDS::IsUsable(triBounds)) {
                    continue;
                }
                bounds = hasBounds ? MergeBounds(bounds, triBounds) : triBounds;
                hasBounds = true;
            }
            return hasBounds ? bounds : Bounds{};
        }

        bool ShouldPartitionLargeStaticSurface(
            const SurfaceWork& work,
            const Bounds& bounds,
            const ClusterCookSettings& settings) {

            if (!settings.partitionLargeStaticSurfaces ||
                settings.surfacePartitionPolicy == SurfacePartitionPolicy::Disabled ||
                !ShouldUsePermissiveOpaqueLods(work.flags) ||
                CountWorkTriangles(work) < settings.largeSurfacePartitionMinTriangles ||
                !BOUNDS::IsUsable(bounds)) {
                return false;
            }

            const float maxExtent =
                (std::max)(0.25f, settings.largeSurfacePartitionMaxExtent);
            return MaxBoundsExtent(bounds) > maxExtent;
        }

        void PartitionTriangleIndicesRecursive(
            const std::vector<SourceTriangle>& triangles,
            std::vector<uint32_t> triangleIndices,
            const TrianglePartitionConfig& config,
            uint32_t depth,
            std::vector<std::vector<uint32_t>>& outPartitions);

        void AppendSpatialTrianglePartitions(
            const std::vector<SourceTriangle>& triangles,
            std::vector<uint32_t> triangleIndices,
            const TrianglePartitionConfig& config,
            std::vector<std::vector<uint32_t>>& outPartitions) {

            if (triangleIndices.empty()) {
                return;
            }
            PartitionTriangleIndicesRecursive(
                triangles,
                std::move(triangleIndices),
                config,
                0u,
                outPartitions);
        }

        bool BuildNormalAwareTrianglePartitions(
            const std::vector<SourceTriangle>& triangles,
            const ClusterCookSettings& settings,
            const TrianglePartitionConfig& config,
            std::vector<std::vector<uint32_t>>& outPartitions) {

            outPartitions.clear();
            if (!settings.buildNormalCone ||
                settings.surfacePartitionPolicy != SurfacePartitionPolicy::SceneStatic ||
                triangles.empty()) {
                return false;
            }

            const uint32_t minChunkTriangles = config.minChunkTriangles;
            std::array<std::vector<uint32_t>, 6u> buckets{};
            for (uint32_t triangleIndex = 0; triangleIndex < triangles.size(); ++triangleIndex) {
                buckets[TriangleNormalBucket(triangles[triangleIndex])].push_back(triangleIndex);
            }

            size_t usableBucketCount = 0;
            for (const std::vector<uint32_t>& bucket : buckets) {
                if (bucket.size() >= minChunkTriangles) {
                    ++usableBucketCount;
                }
            }
            if (usableBucketCount <= 1u) {
                return false;
            }

            std::vector<uint32_t> smallBuckets{};
            for (std::vector<uint32_t>& bucket : buckets) {
                if (bucket.empty()) {
                    continue;
                }
                if (bucket.size() < minChunkTriangles) {
                    smallBuckets.insert(smallBuckets.end(), bucket.begin(), bucket.end());
                    continue;
                }
                AppendSpatialTrianglePartitions(
                    triangles,
                    std::move(bucket),
                    config,
                    outPartitions);
            }

            if (smallBuckets.size() >= minChunkTriangles) {
                AppendSpatialTrianglePartitions(
                    triangles,
                    std::move(smallBuckets),
                    config,
                    outPartitions);
            } else if (!smallBuckets.empty() && !outPartitions.empty()) {
                outPartitions.back().insert(
                    outPartitions.back().end(),
                    smallBuckets.begin(),
                    smallBuckets.end());
            }

            if (outPartitions.size() <= 1u) {
                outPartitions.clear();
                return false;
            }
            return true;
        }

        bool ShouldAcceptTrianglePartitions(
            const std::vector<std::vector<uint32_t>>& partitions,
            const TrianglePartitionConfig& config,
            const ClusterCookSettings& settings) {

            if (partitions.size() <= 1u) {
                return false;
            }

            const uint32_t minChunkTriangles = (std::max)(1u, config.minChunkTriangles);
            const uint32_t softMinChunkTriangles = (std::max)(
                minChunkTriangles / 2u,
                (std::max)(1u, settings.maxTrianglesPerCluster) *
                (std::max)(1u, settings.minPartitionClusterEstimate / 2u));

            uint32_t smallPartitionCount = 0;
            uint64_t totalTriangleCount = 0;
            for (const std::vector<uint32_t>& partition : partitions) {
                if (partition.empty()) {
                    ++smallPartitionCount;
                    continue;
                }
                totalTriangleCount += static_cast<uint64_t>(partition.size());
                if (partition.size() < softMinChunkTriangles) {
                    ++smallPartitionCount;
                }
            }

            const double averagePartitionTriangles =
                static_cast<double>(totalTriangleCount) /
                static_cast<double>(partitions.size());
            return averagePartitionTriangles >= static_cast<double>(softMinChunkTriangles) &&
                smallPartitionCount * 4u <= static_cast<uint32_t>(partitions.size());
        }

        void PartitionTriangleIndicesRecursive(
            const std::vector<SourceTriangle>& triangles,
            std::vector<uint32_t> triangleIndices,
            const TrianglePartitionConfig& config,
            uint32_t depth,
            std::vector<std::vector<uint32_t>>& outPartitions) {

            const Bounds bounds = ComputeTriangleSubsetBounds(triangles, triangleIndices);
            const uint32_t minChunkTriangles = config.minChunkTriangles;
            const float maxExtent = config.maxExtent;
            if (triangleIndices.size() < static_cast<size_t>(minChunkTriangles) * 2u ||
                depth >= config.maxDepth ||
                !BOUNDS::IsUsable(bounds) ||
                MaxBoundsExtent(bounds) <= maxExtent) {
                outPartitions.push_back(std::move(triangleIndices));
                return;
            }

            const uint32_t axis = LongestBoundsAxis(bounds);
            auto middle = triangleIndices.begin() +
                static_cast<std::ptrdiff_t>(triangleIndices.size() / 2u);
            std::nth_element(
                triangleIndices.begin(),
                middle,
                triangleIndices.end(),
                [&](uint32_t lhs, uint32_t rhs) {
                    const float lhsCenter =
                        TriangleCenterOnAxis(triangles[lhs], axis);
                    const float rhsCenter =
                        TriangleCenterOnAxis(triangles[rhs], axis);
                    if (lhsCenter == rhsCenter) {
                        return lhs < rhs;
                    }
                    return lhsCenter < rhsCenter;
                });

            std::vector<uint32_t> left(triangleIndices.begin(), middle);
            std::vector<uint32_t> right(middle, triangleIndices.end());
            if (left.size() < minChunkTriangles || right.size() < minChunkTriangles) {
                outPartitions.push_back(std::move(triangleIndices));
                return;
            }

            PartitionTriangleIndicesRecursive(
                triangles,
                std::move(left),
                config,
                depth + 1u,
                outPartitions);
            PartitionTriangleIndicesRecursive(
                triangles,
                std::move(right),
                config,
                depth + 1u,
                outPartitions);
        }

        bool BuildLargeStaticSurfaceSectionSources(
            const SurfaceWork& work,
            const std::vector<SourceTriangle>& triangles,
            const ClusterCookSettings& settings,
            ClusteredGeometryBuildReport& report,
            std::vector<SurfaceSectionBuildSource>& outSections) {

            outSections.clear();
            const Bounds sourceBounds = ComputeVertexBounds(work.vertices);
            if (!ShouldPartitionLargeStaticSurface(work, sourceBounds, settings)) {
                return false;
            }

            if (triangles.size() < settings.largeSurfacePartitionMinTriangles) {
                return false;
            }

            std::vector<uint32_t> triangleIndices(triangles.size());
            std::iota(triangleIndices.begin(), triangleIndices.end(), 0u);

            const SurfaceShapeAnalysis shapeAnalysis =
                AnalyzeSurfaceShape(triangles, sourceBounds);
            const TrianglePartitionConfig partitionConfig =
                ResolveTrianglePartitionConfig(settings, shapeAnalysis);

            std::vector<std::vector<uint32_t>> partitions{};
            const bool normalPartitioned =
                BuildNormalAwareTrianglePartitions(
                    triangles,
                    settings,
                    partitionConfig,
                    partitions);
            if (!normalPartitioned) {
                PartitionTriangleIndicesRecursive(
                    triangles,
                    std::move(triangleIndices),
                    partitionConfig,
                    0u,
                    partitions);
            }
            if (partitions.size() <= 1u) {
                return false;
            }
            if (!ShouldAcceptTrianglePartitions(partitions, partitionConfig, settings)) {
                ++report.rejectedPartitionedSurfaceCount;
                return false;
            }

            // SurfaceGpuScene は primitive 単位で 1 つの surface を参照するため、
            // cook では runtime surface を分割せず、cluster/page の粒度だけを分割境界に寄せる。
            for (const std::vector<uint32_t>& partition : partitions) {
                if (partition.empty()) {
                    continue;
                }

                std::vector<SourceTriangle> partitionTriangles{};
                partitionTriangles.reserve(partition.size());
                for (uint32_t triangleIndex : partition) {
                    if (triangleIndex < triangles.size()) {
                        partitionTriangles.push_back(triangles[triangleIndex]);
                    }
                }
                if (partitionTriangles.empty()) {
                    continue;
                }

                const std::vector<std::vector<uint32_t>> partitionGroups =
                    BuildClusterTriangleGroups(work.vertices, partitionTriangles, settings, &report);
                SurfaceSectionBuildSource section{};
                section.triangleIndices = partition;
                for (const std::vector<uint32_t>& localGroup : partitionGroups) {
                    std::vector<uint32_t> group{};
                    group.reserve(localGroup.size());
                    for (uint32_t localTriangleIndex : localGroup) {
                        if (localTriangleIndex < partition.size()) {
                            group.push_back(partition[localTriangleIndex]);
                        }
                    }
                    if (!group.empty()) {
                        section.groups.push_back(std::move(group));
                    }
                }
                if (!section.groups.empty()) {
                    outSections.push_back(std::move(section));
                }
            }
            if (outSections.empty()) {
                return false;
            }

            ++report.partitionedSurfaceCount;
            report.partitionedSurfaceChunkCount += static_cast<uint32_t>(outSections.size());
            if (normalPartitioned) {
                ++report.normalPartitionedSurfaceCount;
                report.normalPartitionedChunkCount += static_cast<uint32_t>(outSections.size());
            }
            if (shapeAnalysis.coherentPlanar) {
                ++report.planarPartitionedSurfaceCount;
                report.planarPartitionedChunkCount += static_cast<uint32_t>(outSections.size());
            }
            if (partitionConfig.planarCoarsened) {
                ++report.planarPartitionCoarsenedSurfaceCount;
            }
            return true;
        }

        SurfaceWork BuildSectionSurfaceWork(
            const SurfaceWork& source,
            const std::vector<SourceTriangle>& triangles,
            const std::vector<uint32_t>& triangleIndices) {

            SurfaceWork sectionWork{};
            sectionWork.nodeIndex = source.nodeIndex;
            sectionWork.meshIndex = source.meshIndex;
            sectionWork.primitiveIndex = source.primitiveIndex;
            sectionWork.materialIndex = source.materialIndex;
            sectionWork.flags = source.flags;

            std::unordered_map<uint32_t, uint32_t> vertexRemap{};
            vertexRemap.reserve(triangleIndices.size() * 3u);
            sectionWork.indices.reserve(triangleIndices.size() * 3u);

            for (uint32_t triangleIndex : triangleIndices) {
                if (triangleIndex >= triangles.size()) {
                    continue;
                }

                const SourceTriangle& tri = triangles[triangleIndex];
                const uint32_t sourceIndices[3] = { tri.i0, tri.i1, tri.i2 };
                for (uint32_t sourceIndex : sourceIndices) {
                    if (sourceIndex >= source.vertices.size()) {
                        continue;
                    }

                    auto it = vertexRemap.find(sourceIndex);
                    if (it == vertexRemap.end()) {
                        const uint32_t remappedIndex =
                            static_cast<uint32_t>(sectionWork.vertices.size());
                        vertexRemap[sourceIndex] = remappedIndex;
                        sectionWork.vertices.push_back(source.vertices[sourceIndex]);
                        sectionWork.indices.push_back(remappedIndex);
                    } else {
                        sectionWork.indices.push_back(it->second);
                    }
                }
            }

            return sectionWork;
        }

        float ResolveLodTargetRatio(uint32_t flags, uint32_t lodIndex, const ClusterCookSettings& settings) {
            float ratio = settings.lod4TriangleRatio;
            switch (lodIndex) {
            case 1u:
                ratio = settings.lod1TriangleRatio;
                break;
            case 2u:
                ratio = settings.lod2TriangleRatio;
                break;
            case 3u:
                ratio = settings.lod3TriangleRatio;
                break;
            default:
                break;
            }
            ratio = (std::max)(0.05f, (std::min)(ratio, 0.95f));
            if (RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::AlphaMask)) {
                // 両面/マスク材質は輪郭破綻が目立つので、通常 opaque より控えめに落とす。
                ratio = std::sqrt(ratio);
            } else if (RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::DoubleSided)) {
                ratio = ratio * 0.85f + std::sqrt(ratio) * 0.15f;
            }
            return ratio;
        }

        float ResolveLodTargetError(uint32_t flags, uint32_t lodIndex, const ClusterCookSettings& settings) {
            float error = settings.lod4TargetError;
            switch (lodIndex) {
            case 1u:
                error = settings.lod1TargetError;
                break;
            case 2u:
                error = settings.lod2TargetError;
                break;
            case 3u:
                error = settings.lod3TargetError;
                break;
            default:
                break;
            }
            error = (std::max)(0.0001f, (std::min)(error, 0.08f));
            if (RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::AlphaMask)) {
                error *= 0.65f;
            } else if (RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::DoubleSided)) {
                error *= 0.85f;
            }
            return error;
        }

        float ResolveLodMinScreenRadius(uint32_t lodIndex, const ClusterCookSettings& settings) {
            if (lodIndex == 0u) {
                return (std::max)(settings.lod0MinScreenRadius, settings.lod1MinScreenRadius);
            }
            if (lodIndex == 1u) {
                return (std::max)(settings.lod1MinScreenRadius, settings.lod2MinScreenRadius);
            }
            if (lodIndex == 2u) {
                return (std::max)(settings.lod2MinScreenRadius, settings.lod3MinScreenRadius);
            }
            if (lodIndex == 3u) {
                return (std::max)(settings.lod3MinScreenRadius, settings.lod4MinScreenRadius);
            }
            return (std::max)(0.0f, settings.lod4MinScreenRadius);
        }

        bool BuildReducedSurfaceLodWork(
            const SurfaceWork& source,
            uint32_t lodIndex,
            uint32_t previousTriangleCount,
            const ClusterCookSettings& settings,
            bool lockSectionBorders,
            SurfaceLodBuildResult& outResult) {

            outResult = {};
            if (!settings.buildSurfaceLods ||
                settings.maxSurfaceLodCount <= lodIndex ||
                !ShouldBuildReducedLods(source.flags)) {
                return false;
            }

            const uint32_t sourceTriangleCount = CountWorkTriangles(source);
            if (sourceTriangleCount < 96u || previousTriangleCount < 48u) {
                return false;
            }

            const float targetRatio = ResolveLodTargetRatio(source.flags, lodIndex, settings);
            const uint32_t minimumUsefulReduction =
                (std::max)(1u, static_cast<uint32_t>(
                    std::floor(static_cast<float>(previousTriangleCount) * 0.92f)));

            TOOLS::GEOMETRY::MeshLodGeneratorSettings lodSettings{};
            lodSettings.lodIndex = lodIndex;
            lodSettings.targetTriangleRatio = targetRatio;
            lodSettings.targetError = ResolveLodTargetError(source.flags, lodIndex, settings);
            const bool permissiveOpaqueLod = ShouldUsePermissiveOpaqueLods(source.flags);
            // 通常 opaque は seam 越しの簡略化を許可し、section 境界だけは裂け防止で固定する。
            lodSettings.preserveAttributes = true;
            lodSettings.lockOpenBorders = lockSectionBorders || !permissiveOpaqueLod;
            lodSettings.optimizeVertexCache = true;
            lodSettings.allowAttributeSeamCollapse = permissiveOpaqueLod && !lockSectionBorders;
            lodSettings.protectGeometricBorders = permissiveOpaqueLod || lockSectionBorders;
            lodSettings.pruneIsolatedComponents = false;

            TOOLS::GEOMETRY::MeshLodGeneratorResult lodResult{};
            if (!TOOLS::GEOMETRY::GenerateClusterSurfaceLod(
                    source.vertices,
                    source.indices,
                    lodSettings,
                    lodResult)) {
                return false;
            }

            SurfaceWork lodWork = source;
            lodWork.vertices = std::move(lodResult.vertices);
            lodWork.indices = std::move(lodResult.indices);

            const uint32_t lodTriangleCount = CountWorkTriangles(lodWork);
            if (lodTriangleCount == 0u || lodTriangleCount >= minimumUsefulReduction) {
                return false;
            }

            outResult.work = std::move(lodWork);
            outResult.geometricError = lodResult.geometricError;
            return outResult.geometricError >= 0.0f;
        }

        struct TriangleKey {
            uint32_t i0 = 0;
            uint32_t i1 = 0;
            uint32_t i2 = 0;

            bool operator==(const TriangleKey& rhs) const {
                return i0 == rhs.i0 && i1 == rhs.i1 && i2 == rhs.i2;
            }
        };

        struct TriangleKeyHash {
            size_t operator()(const TriangleKey& key) const {
                uint64_t h = 1469598103934665603ull;
                const uint32_t values[3] = { key.i0, key.i1, key.i2 };
                for (uint32_t value : values) {
                    h ^= static_cast<uint64_t>(value);
                    h *= 1099511628211ull;
                }
                return static_cast<size_t>(h);
            }
        };

        TriangleKey MakeTriangleKey(const SourceTriangle& tri) {
            return { tri.i0, tri.i1, tri.i2 };
        }

        struct TriangleGroupStats {
            uint32_t groupCount = 0;
            uint32_t totalTriangleCount = 0;
            uint32_t lowTriangleGroupCount = 0;
            uint32_t normalCoherentGroupCount = 0;
            float averageTrianglesPerGroup = 0.0f;
            float normalCoherentGroupRatio = 0.0f;
        };

        uint32_t ResolvePreferredClusterTriangleCount(const ClusterCookSettings& settings) {
            const uint32_t maxTriangles = (std::max)(1u, settings.maxTrianglesPerCluster);
            const float occupancyRatio =
                (std::max)(0.25f, (std::min)(settings.minClusterOccupancyRatio, 1.0f));
            const uint32_t occupancyTarget =
                static_cast<uint32_t>(std::ceil(static_cast<float>(maxTriangles) * occupancyRatio));
            return (std::min)(
                maxTriangles,
                (std::max)((std::max)(1u, settings.minTrianglesPerCluster), occupancyTarget));
        }

        float ResolveNormalDotThreshold(float value) {
            return (std::max)(-1.0f, (std::min)(value, 0.99f));
        }

        float ResolvePositiveTriangleArea(const SourceTriangle& tri) {
            return std::isfinite(tri.area) && tri.area > 1e-7f
                ? tri.area
                : 1.0f;
        }

        struct TriangleGroupNormalBasis {
            bool hasNormals = false;
            bool coherent = false;
            MATH::Vec3 axis{ 0.0f, 1.0f, 0.0f };
            float weight = 0.0f;
        };

        TriangleGroupNormalBasis ResolveTriangleGroupNormalBasis(
            const std::vector<SourceTriangle>& triangles,
            const std::vector<uint32_t>& group) {

            TriangleGroupNormalBasis basis{};
            MATH::Vec3 normalSum{};
            for (uint32_t triangleIndex : group) {
                if (triangleIndex >= triangles.size()) {
                    continue;
                }

                const SourceTriangle& tri = triangles[triangleIndex];
                if (MATH::Length(tri.normal) <= 1e-5f) {
                    continue;
                }

                const float weight = ResolvePositiveTriangleArea(tri);
                normalSum = normalSum + tri.normal * weight;
                basis.weight += weight;
                basis.hasNormals = true;
            }

            if (!basis.hasNormals) {
                return basis;
            }

            if (MATH::Length(normalSum) > 1e-5f) {
                basis.axis = MATH::Normalize(normalSum);
                basis.coherent = true;
            }
            return basis;
        }

        float TriangleGroupMinNormalDotAgainstAxis(
            const std::vector<SourceTriangle>& triangles,
            const std::vector<uint32_t>& group,
            const MATH::Vec3& axis) {

            float minDot = 1.0f;
            bool hasNormal = false;
            for (uint32_t triangleIndex : group) {
                if (triangleIndex >= triangles.size()) {
                    continue;
                }

                const SourceTriangle& tri = triangles[triangleIndex];
                if (MATH::Length(tri.normal) <= 1e-5f) {
                    continue;
                }

                minDot = (std::min)(minDot, MATH::Dot(axis, tri.normal));
                hasNormal = true;
            }
            return hasNormal ? minDot : 1.0f;
        }

        float TriangleGroupMinNormalDot(
            const std::vector<SourceTriangle>& triangles,
            const std::vector<uint32_t>& group) {

            const TriangleGroupNormalBasis basis =
                ResolveTriangleGroupNormalBasis(triangles, group);
            if (!basis.hasNormals) {
                return 1.0f;
            }
            if (!basis.coherent) {
                return -1.0f;
            }
            return TriangleGroupMinNormalDotAgainstAxis(triangles, group, basis.axis);
        }

        bool AreTriangleGroupsNormalCompatible(
            const std::vector<SourceTriangle>& triangles,
            const std::vector<uint32_t>& lhs,
            const std::vector<uint32_t>& rhs,
            const ClusterCookSettings& settings) {

            const float minDot =
                ResolveNormalDotThreshold(settings.clusterMergeNormalMinDot);
            if (minDot <= -0.99f) {
                return true;
            }

            const TriangleGroupNormalBasis lhsBasis =
                ResolveTriangleGroupNormalBasis(triangles, lhs);
            const TriangleGroupNormalBasis rhsBasis =
                ResolveTriangleGroupNormalBasis(triangles, rhs);
            if (!lhsBasis.hasNormals || !rhsBasis.hasNormals) {
                return true;
            }
            if (!lhsBasis.coherent || !rhsBasis.coherent) {
                return false;
            }
            if (MATH::Dot(lhsBasis.axis, rhsBasis.axis) < minDot) {
                return false;
            }

            const MATH::Vec3 mergedNormal =
                lhsBasis.axis * lhsBasis.weight + rhsBasis.axis * rhsBasis.weight;
            if (MATH::Length(mergedNormal) <= 1e-5f) {
                return false;
            }

            const MATH::Vec3 mergedAxis = MATH::Normalize(mergedNormal);
            return TriangleGroupMinNormalDotAgainstAxis(triangles, lhs, mergedAxis) >= minDot &&
                TriangleGroupMinNormalDotAgainstAxis(triangles, rhs, mergedAxis) >= minDot;
        }

        void AppendUniqueTriangleVertices(
            const SourceTriangle& tri,
            std::vector<uint32_t>& vertices) {

            const uint32_t ids[3] = { tri.i0, tri.i1, tri.i2 };
            for (uint32_t id : ids) {
                if (std::find(vertices.begin(), vertices.end(), id) == vertices.end()) {
                    vertices.push_back(id);
                }
            }
        }

        bool CanMergeTriangleGroups(
            const std::vector<SourceTriangle>& triangles,
            const std::vector<uint32_t>& lhs,
            const std::vector<uint32_t>& rhs,
            const ClusterCookSettings& settings) {

            if (lhs.size() + rhs.size() >
                static_cast<size_t>((std::max)(1u, settings.maxTrianglesPerCluster))) {
                return false;
            }
            if (!AreTriangleGroupsNormalCompatible(triangles, lhs, rhs, settings)) {
                return false;
            }

            std::vector<uint32_t> vertices{};
            vertices.reserve((lhs.size() + rhs.size()) * 3u);
            for (uint32_t triangleIndex : lhs) {
                if (triangleIndex < triangles.size()) {
                    AppendUniqueTriangleVertices(triangles[triangleIndex], vertices);
                }
            }
            for (uint32_t triangleIndex : rhs) {
                if (triangleIndex < triangles.size()) {
                    AppendUniqueTriangleVertices(triangles[triangleIndex], vertices);
                }
            }
            return vertices.size() <= static_cast<size_t>((std::max)(3u, settings.maxVerticesPerCluster));
        }

        MATH::Vec3 TriangleGroupCenter(
            const std::vector<SourceTriangle>& triangles,
            const std::vector<uint32_t>& group) {

            const Bounds bounds = ComputeTriangleSubsetBounds(triangles, group);
            if (!BOUNDS::IsUsable(bounds)) {
                return {};
            }
            return (bounds.min + bounds.max) * 0.5f;
        }

        float DistanceSquared(const MATH::Vec3& lhs, const MATH::Vec3& rhs) {
            const MATH::Vec3 delta = lhs - rhs;
            return MATH::Dot(delta, delta);
        }

        TriangleGroupStats EvaluateTriangleGroups(
            const std::vector<SourceTriangle>& triangles,
            const std::vector<std::vector<uint32_t>>& groups,
            uint32_t preferredTrianglesPerGroup,
            const ClusterCookSettings& settings) {

            TriangleGroupStats stats{};
            stats.groupCount = static_cast<uint32_t>(groups.size());
            const float coherentNormalMinDot =
                ResolveNormalDotThreshold(settings.normalBucketCoherentGroupMinDot);
            for (const std::vector<uint32_t>& group : groups) {
                stats.totalTriangleCount += static_cast<uint32_t>(group.size());
                if (group.size() < preferredTrianglesPerGroup) {
                    ++stats.lowTriangleGroupCount;
                }
                if (TriangleGroupMinNormalDot(triangles, group) >= coherentNormalMinDot) {
                    ++stats.normalCoherentGroupCount;
                }
            }
            if (stats.groupCount > 0u) {
                stats.averageTrianglesPerGroup =
                    static_cast<float>(
                        static_cast<double>(stats.totalTriangleCount) /
                        static_cast<double>(stats.groupCount));
                stats.normalCoherentGroupRatio =
                    static_cast<float>(
                        static_cast<double>(stats.normalCoherentGroupCount) /
                        static_cast<double>(stats.groupCount));
            }
            return stats;
        }

        std::vector<std::vector<uint32_t>> CompactUnderfilledClusterGroups(
            const std::vector<SourceTriangle>& triangles,
            std::vector<std::vector<uint32_t>> groups,
            const ClusterCookSettings& settings,
            ClusteredGeometryBuildReport* report) {

            if (!settings.compactUnderfilledClusterGroups || groups.size() <= 1u) {
                return groups;
            }

            const uint32_t preferredTriangles = ResolvePreferredClusterTriangleCount(settings);
            std::vector<bool> consumed(groups.size(), false);
            std::vector<std::vector<uint32_t>> compacted{};
            compacted.reserve(groups.size());

            for (size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
                if (consumed[groupIndex]) {
                    continue;
                }

                std::vector<uint32_t> current = std::move(groups[groupIndex]);
                consumed[groupIndex] = true;
                while (current.size() < preferredTriangles) {
                    const MATH::Vec3 currentCenter = TriangleGroupCenter(triangles, current);
                    size_t bestIndex = groups.size();
                    float bestScore = (std::numeric_limits<float>::max)();
                    for (size_t candidateIndex = groupIndex + 1u;
                         candidateIndex < groups.size();
                         ++candidateIndex) {
                        if (consumed[candidateIndex] || groups[candidateIndex].empty()) {
                            continue;
                        }
                        if (!CanMergeTriangleGroups(
                                triangles,
                                current,
                                groups[candidateIndex],
                                settings)) {
                            continue;
                        }

                        const float score = DistanceSquared(
                            currentCenter,
                            TriangleGroupCenter(triangles, groups[candidateIndex]));
                        if (score < bestScore ||
                            (score == bestScore &&
                             groups[candidateIndex].size() >
                                (bestIndex < groups.size() ? groups[bestIndex].size() : 0u))) {
                            bestScore = score;
                            bestIndex = candidateIndex;
                        }
                    }

                    if (bestIndex >= groups.size()) {
                        break;
                    }

                    current.insert(
                        current.end(),
                        groups[bestIndex].begin(),
                        groups[bestIndex].end());
                    consumed[bestIndex] = true;
                    if (report != nullptr) {
                        ++report->mergedClusterGroupCount;
                    }
                }

                if (!current.empty() &&
                    current.size() < preferredTriangles &&
                    !compacted.empty() &&
                    CanMergeTriangleGroups(triangles, compacted.back(), current, settings)) {
                    compacted.back().insert(
                        compacted.back().end(),
                        current.begin(),
                        current.end());
                    if (report != nullptr) {
                        ++report->mergedClusterGroupCount;
                    }
                    continue;
                }

                if (!current.empty()) {
                    compacted.push_back(std::move(current));
                }
            }

            if (report != nullptr && compacted.size() < groups.size()) {
                report->compactedClusterGroupCount +=
                    static_cast<uint32_t>(groups.size() - compacted.size());
            }
            return compacted;
        }

        std::vector<std::vector<uint32_t>> BuildSequentialTriangleGroups(
            const std::vector<SourceTriangle>& triangles,
            const ClusterCookSettings& settings,
            ClusteredGeometryBuildReport* report = nullptr) {

            std::vector<std::vector<uint32_t>> groups{};
            const uint32_t maxTriangles =
                (std::max)(1u, settings.maxTrianglesPerCluster);
            const uint32_t maxVertices =
                (std::max)(3u, settings.maxVerticesPerCluster);
            auto countNewVertices = [](const SourceTriangle& tri, const std::vector<uint32_t>& vertices) {
                uint32_t added = 0;
                const uint32_t ids[3] = { tri.i0, tri.i1, tri.i2 };
                for (uint32_t id : ids) {
                    if (std::find(vertices.begin(), vertices.end(), id) == vertices.end()) {
                        ++added;
                    }
                }
                return added;
            };
            auto appendTriangleVertices = [](const SourceTriangle& tri, std::vector<uint32_t>& vertices) {
                const uint32_t ids[3] = { tri.i0, tri.i1, tri.i2 };
                for (uint32_t id : ids) {
                    if (std::find(vertices.begin(), vertices.end(), id) == vertices.end()) {
                        vertices.push_back(id);
                    }
                }
            };
            for (uint32_t i = 0; i < triangles.size();) {
                std::vector<uint32_t> group{};
                group.reserve(maxTriangles);
                std::vector<uint32_t> groupVertices{};
                groupVertices.reserve(maxVertices);
                for (; i < triangles.size() && group.size() < maxTriangles;) {
                    const uint32_t newVertices = countNewVertices(triangles[i], groupVertices);
                    if (!group.empty() && groupVertices.size() + newVertices > maxVertices) {
                        break;
                    }
                    group.push_back(i);
                    appendTriangleVertices(triangles[i], groupVertices);
                    ++i;
                }
                if (!group.empty()) {
                    groups.push_back(std::move(group));
                } else {
                    ++i;
                }
            }
            return CompactUnderfilledClusterGroups(
                triangles,
                std::move(groups),
                settings,
                report);
        }

        std::vector<std::vector<uint32_t>> BuildMeshoptTriangleGroups(
            const std::vector<ClusterVertex>& vertices,
            const std::vector<SourceTriangle>& triangles,
            const ClusterCookSettings& settings,
            ClusteredGeometryBuildReport* report = nullptr) {

            if (vertices.empty() || triangles.empty()) {
                return {};
            }

            const size_t maxVertices =
                (std::min<size_t>)((std::max)(1u, settings.maxVerticesPerCluster), 256u);
            const size_t maxTriangles =
                (std::min<size_t>)((std::max)(1u, settings.maxTrianglesPerCluster), 512u);
            const size_t minTriangles =
                (std::min<size_t>)(
                    (std::max<size_t>)(1u, settings.minTrianglesPerCluster),
                    maxTriangles);
            if (maxVertices < 3u || maxTriangles == 0u) {
                return BuildSequentialTriangleGroups(triangles, settings, report);
            }

            std::vector<unsigned int> indices{};
            indices.reserve(triangles.size() * 3u);
            std::unordered_map<TriangleKey, std::vector<uint32_t>, TriangleKeyHash> triangleLookup{};
            triangleLookup.reserve(triangles.size());
            for (uint32_t triangleIndex = 0; triangleIndex < triangles.size(); ++triangleIndex) {
                const SourceTriangle& tri = triangles[triangleIndex];
                if (tri.i0 >= vertices.size() || tri.i1 >= vertices.size() || tri.i2 >= vertices.size()) {
                    continue;
                }

                indices.push_back(tri.i0);
                indices.push_back(tri.i1);
                indices.push_back(tri.i2);
                triangleLookup[MakeTriangleKey(tri)].push_back(triangleIndex);
            }
            if (indices.empty()) {
                return {};
            }

            if (indices.size() >= 6u) {
                std::vector<unsigned int> sortedIndices(indices.size());
                meshopt_spatialSortTriangles(
                    sortedIndices.data(),
                    indices.data(),
                    indices.size(),
                    &vertices[0].position.x,
                    vertices.size(),
                    sizeof(ClusterVertex));
                indices = std::move(sortedIndices);
            }

            const size_t meshletBound =
                meshopt_buildMeshletsBound(indices.size(), maxVertices, minTriangles);
            std::vector<meshopt_Meshlet> meshlets(meshletBound);
            std::vector<unsigned int> meshletVertices(indices.size());
            std::vector<unsigned char> meshletTriangles(indices.size());

            // 正式な meshlet builder で cluster を作り、後段の独自 HCMESH レイアウトへ変換する。
            const size_t meshletCount = meshopt_buildMeshletsFlex(
                meshlets.data(),
                meshletVertices.data(),
                meshletTriangles.data(),
                indices.data(),
                indices.size(),
                &vertices[0].position.x,
                vertices.size(),
                sizeof(ClusterVertex),
                maxVertices,
                minTriangles,
                maxTriangles,
                (std::max)(0.0f, settings.meshletConeWeight),
                (std::max)(0.0f, settings.meshletSplitFactor));
            if (meshletCount == 0u) {
                return BuildSequentialTriangleGroups(triangles, settings, report);
            }

            std::vector<std::vector<uint32_t>> groups{};
            groups.reserve(meshletCount);
            for (size_t meshletIndex = 0; meshletIndex < meshletCount; ++meshletIndex) {
                const meshopt_Meshlet& meshlet = meshlets[meshletIndex];
                if (meshlet.triangle_count == 0u || meshlet.vertex_count == 0u) {
                    continue;
                }

                meshopt_optimizeMeshlet(
                    meshletVertices.data() + meshlet.vertex_offset,
                    meshletTriangles.data() + meshlet.triangle_offset,
                    meshlet.triangle_count,
                    meshlet.vertex_count);

                std::vector<uint32_t> group{};
                group.reserve(meshlet.triangle_count);
                for (uint32_t triOffset = 0; triOffset < meshlet.triangle_count; ++triOffset) {
                    const uint32_t base = meshlet.triangle_offset + triOffset * 3u;
                    const unsigned char local0 = meshletTriangles[base + 0u];
                    const unsigned char local1 = meshletTriangles[base + 1u];
                    const unsigned char local2 = meshletTriangles[base + 2u];
                    if (local0 >= meshlet.vertex_count ||
                        local1 >= meshlet.vertex_count ||
                        local2 >= meshlet.vertex_count) {
                        continue;
                    }

                    TriangleKey key{};
                    key.i0 = meshletVertices[meshlet.vertex_offset + local0];
                    key.i1 = meshletVertices[meshlet.vertex_offset + local1];
                    key.i2 = meshletVertices[meshlet.vertex_offset + local2];

                    auto it = triangleLookup.find(key);
                    if (it == triangleLookup.end() || it->second.empty()) {
                        return BuildSequentialTriangleGroups(triangles, settings, report);
                    }

                    group.push_back(it->second.back());
                    it->second.pop_back();
                }

                if (!group.empty()) {
                    groups.push_back(std::move(group));
                }
            }

            return groups.empty()
                ? BuildSequentialTriangleGroups(triangles, settings, report)
                : CompactUnderfilledClusterGroups(
                    triangles,
                    std::move(groups),
                    settings,
                    report);
        }

        std::vector<std::vector<uint32_t>> BuildClusterTriangleGroups(
            const std::vector<ClusterVertex>& vertices,
            const std::vector<SourceTriangle>& triangles,
            const ClusterCookSettings& settings,
            ClusteredGeometryBuildReport* report) {

            const size_t maxTriangles =
                (std::min<size_t>)((std::max)(1u, settings.maxTrianglesPerCluster), 512u);
            const bool coneFriendlyScene =
                settings.buildNormalCone &&
                settings.surfacePartitionPolicy == SurfacePartitionPolicy::SceneStatic &&
                triangles.size() >= maxTriangles * 2u;
            if (!coneFriendlyScene) {
                return BuildMeshoptTriangleGroups(vertices, triangles, settings, report);
            }

            std::array<std::vector<uint32_t>, 6u> buckets{};
            for (uint32_t triangleIndex = 0; triangleIndex < triangles.size(); ++triangleIndex) {
                buckets[TriangleNormalBucket(triangles[triangleIndex])].push_back(triangleIndex);
            }

            size_t nonEmptyBucketCount = 0;
            for (const std::vector<uint32_t>& bucket : buckets) {
                if (!bucket.empty()) {
                    ++nonEmptyBucketCount;
                }
            }
            if (nonEmptyBucketCount <= 1u) {
                return BuildMeshoptTriangleGroups(vertices, triangles, settings, report);
            }

            std::vector<std::vector<uint32_t>> rawGroups =
                BuildMeshoptTriangleGroups(vertices, triangles, settings, nullptr);
            std::vector<std::vector<uint32_t>> groups{};
            for (const std::vector<uint32_t>& bucket : buckets) {
                if (bucket.empty()) {
                    continue;
                }

                std::vector<SourceTriangle> bucketTriangles{};
                bucketTriangles.reserve(bucket.size());
                for (uint32_t triangleIndex : bucket) {
                    bucketTriangles.push_back(triangles[triangleIndex]);
                }

                std::vector<std::vector<uint32_t>> bucketGroups =
                    BuildMeshoptTriangleGroups(vertices, bucketTriangles, settings, nullptr);
                for (std::vector<uint32_t>& bucketGroup : bucketGroups) {
                    bool groupValid = true;
                    for (uint32_t& localTriangleIndex : bucketGroup) {
                        if (localTriangleIndex >= bucket.size()) {
                            groupValid = false;
                            break;
                        }
                        localTriangleIndex = bucket[localTriangleIndex];
                    }
                    if (groupValid && !bucketGroup.empty()) {
                        groups.push_back(std::move(bucketGroup));
                    }
                }
            }

            if (groups.empty()) {
                return rawGroups;
            }

            const uint32_t preferredTriangles = ResolvePreferredClusterTriangleCount(settings);
            const TriangleGroupStats rawStats =
                EvaluateTriangleGroups(triangles, rawGroups, preferredTriangles, settings);
            const TriangleGroupStats bucketStats =
                EvaluateTriangleGroups(triangles, groups, preferredTriangles, settings);
            const float maxOverhead =
                (std::max)(1.0f, (std::min)(settings.maxNormalBucketClusterOverhead, 2.0f));
            const uint32_t allowedBucketGroups =
                static_cast<uint32_t>(
                    std::ceil(static_cast<float>((std::max)(1u, rawStats.groupCount)) * maxOverhead));
            const float minimumBucketAverage =
                rawStats.averageTrianglesPerGroup / maxOverhead;
            const float qualityBonus =
                (std::max)(0.0f, (std::min)(settings.normalBucketQualityBonusRatio, 1.0f));
            const bool bucketNormalQualityBetter =
                bucketStats.normalCoherentGroupRatio >=
                (std::min)(1.0f, rawStats.normalCoherentGroupRatio + qualityBonus);
            const bool rawNormalQualityWeak = rawStats.normalCoherentGroupRatio < 0.90f;
            const bool bucketAverageStillUseful =
                bucketStats.averageTrianglesPerGroup >=
                rawStats.averageTrianglesPerGroup * 0.75f;
            const bool bucketCullingQualityWorthOverhead =
                rawNormalQualityWeak &&
                bucketNormalQualityBetter &&
                bucketStats.normalCoherentGroupRatio >= 0.80f &&
                bucketAverageStillUseful;
            const bool acceptBucketGroups =
                rawStats.groupCount == 0u ||
                (bucketStats.groupCount <= allowedBucketGroups &&
                 (bucketStats.averageTrianglesPerGroup >= minimumBucketAverage ||
                  bucketCullingQualityWorthOverhead));
            if (report != nullptr) {
                if (acceptBucketGroups) {
                    ++report->acceptedNormalBucketGroupCount;
                } else {
                    ++report->rejectedNormalBucketGroupCount;
                }
            }
            return acceptBucketGroups ? groups : rawGroups;
        }

        uint32_t AppendClusterGeometry(
            const SurfaceWork& work,
            const std::vector<SourceTriangle>& triangles,
            const std::vector<uint32_t>& group,
            uint32_t surfaceIndex,
            const ClusterCookSettings& settings,
            ClusteredGeometryAsset& asset) {

            std::unordered_map<uint32_t, uint32_t> vertexRemap{};
            vertexRemap.reserve(group.size() * 3u);

            MeshCluster cluster{};
            cluster.surfaceIndex = surfaceIndex;
            cluster.firstIndex = static_cast<uint32_t>(asset.packedIndices.size());
            cluster.firstVertex = static_cast<uint32_t>(asset.packedVertices.size());
            cluster.triangleCount = static_cast<uint32_t>(group.size());
            cluster.firstPrimitive = static_cast<uint32_t>(asset.meshletPrimitives.size());
            cluster.primitiveCount = cluster.triangleCount;

            Bounds bounds{};
            bool hasBounds = false;
            for (uint32_t triangleIndex : group) {
                const SourceTriangle& tri = triangles[triangleIndex];
                const uint32_t sourceIndices[3] = { tri.i0, tri.i1, tri.i2 };
                uint32_t localIndices[3]{};
                for (uint32_t corner = 0; corner < 3u; ++corner) {
                    const uint32_t sourceIndex = sourceIndices[corner];
                    auto it = vertexRemap.find(sourceIndex);
                    if (it == vertexRemap.end()) {
                        const uint32_t localIndex =
                            static_cast<uint32_t>(asset.packedVertices.size()) - cluster.firstVertex;
                        vertexRemap[sourceIndex] = localIndex;
                        asset.packedVertices.push_back(work.vertices[sourceIndex]);
                        EncapsulatePoint(bounds, hasBounds, work.vertices[sourceIndex].position);
                        // cluster 内 index は meshlet と同じく局所 index として保存する。
                        localIndices[corner] = localIndex;
                    } else {
                        localIndices[corner] = it->second;
                    }
                }
                asset.packedIndices.push_back(localIndices[0]);
                asset.packedIndices.push_back(localIndices[1]);
                asset.packedIndices.push_back(localIndices[2]);

                MeshletPrimitive primitive{};
                primitive.i0 = localIndices[0];
                primitive.i1 = localIndices[1];
                primitive.i2 = localIndices[2];
                asset.meshletPrimitives.push_back(primitive);
            }

            cluster.indexCount = static_cast<uint32_t>(asset.packedIndices.size()) - cluster.firstIndex;
            cluster.vertexCount = static_cast<uint32_t>(asset.packedVertices.size()) - cluster.firstVertex;
            cluster.localBounds = hasBounds ? bounds : Bounds{};
            cluster.sphereCenter = (cluster.localBounds.min + cluster.localBounds.max) * 0.5f;
            cluster.sphereRadius = 0.0f;
            for (uint32_t i = 0; i < cluster.vertexCount; ++i) {
                const ClusterVertex& vertex = asset.packedVertices[cluster.firstVertex + i];
                cluster.sphereRadius = (std::max)(
                    cluster.sphereRadius,
                    MATH::Length(vertex.position - cluster.sphereCenter));
            }

            cluster.coneApex = cluster.sphereCenter;
            cluster.coneAxis = { 0.0f, 1.0f, 0.0f };
            cluster.coneCutoff = 1.0f;
            if (settings.buildNormalCone) {
                MeshCluster meshoptCluster = cluster;
                if (ApplyMeshoptMeshletBounds(asset, cluster, meshoptCluster)) {
                    cluster = meshoptCluster;
                }
            }

            cluster.flags = work.flags;
            asset.clusters.push_back(cluster);
            return static_cast<uint32_t>(asset.clusters.size() - 1u);
        }

        void AppendPagesForClusterRange(
            uint32_t firstCluster,
            uint32_t clusterCount,
            const ClusterCookSettings& settings,
            ClusteredGeometryAsset& asset,
            uint32_t& outFirstPage,
            uint32_t& outPageCount) {

            outFirstPage = static_cast<uint32_t>(asset.pages.size());
            outPageCount = 0u;
            if (!settings.buildClusterPages || clusterCount == 0u) {
                return;
            }

            SpatialSortClusterRangeForPages(firstCluster, clusterCount, asset);

            const uint32_t maxPerPage = (std::max)(1u, settings.maxClustersPerPage);
            uint32_t remaining = clusterCount;
            uint32_t clusterCursor = firstCluster;
            while (remaining > 0u) {
                const uint32_t pageClusterCount = (std::min)(remaining, maxPerPage);
                ClusterPage page{};
                page.firstCluster = clusterCursor;
                page.clusterCount = pageClusterCount;

                Bounds bounds{};
                bool hasBounds = false;
                const uint32_t invalidOffset = (std::numeric_limits<uint32_t>::max)();
                uint32_t minIndex = invalidOffset;
                uint32_t minVertex = invalidOffset;
                uint32_t minPrimitive = invalidOffset;
                uint32_t endIndex = 0u;
                uint32_t endVertex = 0u;
                uint32_t endPrimitive = 0u;
                for (uint32_t i = 0; i < pageClusterCount; ++i) {
                    const MeshCluster& cluster = asset.clusters[clusterCursor + i];
                    bounds = hasBounds ? MergeBounds(bounds, cluster.localBounds) : cluster.localBounds;
                    hasBounds = hasBounds || BOUNDS::IsUsable(cluster.localBounds);
                    minIndex = (std::min)(minIndex, cluster.firstIndex);
                    minVertex = (std::min)(minVertex, cluster.firstVertex);
                    minPrimitive = (std::min)(minPrimitive, cluster.firstPrimitive);
                    endIndex = (std::max)(endIndex, cluster.firstIndex + cluster.indexCount);
                    endVertex = (std::max)(endVertex, cluster.firstVertex + cluster.vertexCount);
                    endPrimitive = (std::max)(endPrimitive, cluster.firstPrimitive + cluster.primitiveCount);
                }
                page.firstIndex = minIndex != invalidOffset ? minIndex : 0u;
                page.firstVertex = minVertex != invalidOffset ? minVertex : 0u;
                page.firstPrimitive = minPrimitive != invalidOffset ? minPrimitive : 0u;
                page.indexCount = endIndex >= page.firstIndex ? endIndex - page.firstIndex : 0u;
                page.vertexCount = endVertex >= page.firstVertex ? endVertex - page.firstVertex : 0u;
                page.primitiveCount = endPrimitive >= page.firstPrimitive ? endPrimitive - page.firstPrimitive : 0u;
                page.localBounds = hasBounds ? bounds : Bounds{};
                asset.pages.push_back(page);

                clusterCursor += pageClusterCount;
                remaining -= pageClusterCount;
            }
            outPageCount = static_cast<uint32_t>(asset.pages.size()) - outFirstPage;
        }

        void AppendLod0RangeForSection(
            ClusterSurfaceSection& section,
            uint32_t surfaceIndex,
            const ClusterCookSettings& settings,
            ClusteredGeometryAsset& asset) {

            ClusterSurfaceLodRange lodRange{};
            lodRange.surfaceIndex = surfaceIndex;
            lodRange.lodIndex = 0u;
            lodRange.firstCluster = section.firstCluster;
            lodRange.clusterCount = section.clusterCount;
            lodRange.firstIndex = section.firstIndex;
            lodRange.indexCount = section.indexCount;
            lodRange.firstVertex = section.firstVertex;
            lodRange.vertexCount = section.vertexCount;
            lodRange.firstPage = section.firstPage;
            lodRange.pageCount = section.pageCount;
            lodRange.firstPrimitive = section.firstPrimitive;
            lodRange.primitiveCount = section.primitiveCount;
            lodRange.minScreenRadius = ResolveLodMinScreenRadius(0u, settings);
            lodRange.flags = section.flags;
            lodRange.sectionIndex = section.sectionIndex;

            section.firstLodRange = static_cast<uint32_t>(asset.surfaceLodRanges.size());
            section.lodRangeCount = 1u;
            asset.surfaceLodRanges.push_back(lodRange);
        }

        bool AppendReducedLodRangeForSection(
            ClusterSurfaceSection& section,
            uint32_t surfaceIndex,
            uint32_t lodIndex,
            const SurfaceWork& lodWork,
            float geometricError,
            const ClusterCookSettings& settings,
            ClusteredGeometryAsset& asset) {

            const std::vector<SourceTriangle> lodTriangles =
                BuildTriangles(lodWork.vertices, lodWork.indices);
            if (lodTriangles.empty()) {
                return false;
            }

            ClusterSurfaceLodRange lodRange{};
            lodRange.surfaceIndex = surfaceIndex;
            lodRange.lodIndex = lodIndex;
            lodRange.firstCluster = static_cast<uint32_t>(asset.clusters.size());
            lodRange.firstIndex = static_cast<uint32_t>(asset.packedIndices.size());
            lodRange.firstVertex = static_cast<uint32_t>(asset.packedVertices.size());
            lodRange.firstPrimitive = static_cast<uint32_t>(asset.meshletPrimitives.size());
            lodRange.geometricError = geometricError;
            lodRange.minScreenRadius = ResolveLodMinScreenRadius(lodIndex, settings);
            lodRange.flags = section.flags;
            lodRange.sectionIndex = section.sectionIndex;

            const std::vector<std::vector<uint32_t>> lodGroups =
                BuildClusterTriangleGroups(lodWork.vertices, lodTriangles, settings);
            for (const std::vector<uint32_t>& group : lodGroups) {
                if (group.empty()) {
                    continue;
                }
                AppendClusterGeometry(lodWork, lodTriangles, group, surfaceIndex, settings, asset);
            }

            lodRange.clusterCount =
                static_cast<uint32_t>(asset.clusters.size()) - lodRange.firstCluster;
            lodRange.indexCount =
                static_cast<uint32_t>(asset.packedIndices.size()) - lodRange.firstIndex;
            lodRange.vertexCount =
                static_cast<uint32_t>(asset.packedVertices.size()) - lodRange.firstVertex;
            lodRange.primitiveCount =
                static_cast<uint32_t>(asset.meshletPrimitives.size()) - lodRange.firstPrimitive;
            if (lodRange.clusterCount == 0u ||
                lodRange.indexCount == 0u ||
                lodRange.vertexCount == 0u ||
                lodRange.primitiveCount == 0u) {
                return false;
            }

            AppendPagesForClusterRange(
                lodRange.firstCluster,
                lodRange.clusterCount,
                settings,
                asset,
                lodRange.firstPage,
                lodRange.pageCount);
            if (lodRange.pageCount == 0u && settings.buildClusterPages) {
                return false;
            }

            asset.surfaceLodRanges.push_back(lodRange);
            ++section.lodRangeCount;
            return true;
        }

        void AppendReducedLodRangesForSection(
            ClusterSurfaceSection& section,
            uint32_t surfaceIndex,
            const SurfaceWork& sourceWork,
            const ClusterCookSettings& settings,
            bool lockSectionBorders,
            ClusteredGeometryAsset& asset) {

            SurfaceWork currentSource = sourceWork;
            uint32_t previousTriangleCount = section.primitiveCount;
            const uint32_t maxLodCount = (std::min)(settings.maxSurfaceLodCount, 5u);
            for (uint32_t lodIndex = 1u; lodIndex < maxLodCount; ++lodIndex) {
                SurfaceLodBuildResult lodResult{};
                if (!BuildReducedSurfaceLodWork(
                        currentSource,
                        lodIndex,
                        previousTriangleCount,
                        settings,
                        lockSectionBorders,
                        lodResult)) {
                    break;
                }

                const uint32_t lodTriangleCount = CountWorkTriangles(lodResult.work);
                if (lodTriangleCount == 0u ||
                    lodTriangleCount >= previousTriangleCount) {
                    break;
                }

                if (AppendReducedLodRangeForSection(
                        section,
                        surfaceIndex,
                        lodIndex,
                        lodResult.work,
                        lodResult.geometricError,
                        settings,
                        asset)) {
                    previousTriangleCount = lodTriangleCount;
                    currentSource = std::move(lodResult.work);
                } else {
                    break;
                }
            }
        }

        bool CookSurfaceWork(
            const SurfaceWork& work,
            const ClusterCookSettings& settings,
            ClusteredGeometryAsset& asset,
            ClusteredGeometryBuildReport& report) {

            if (!settings.buildPackedGeometry || work.vertices.empty() || work.indices.size() < 3u) {
                ++report.skippedInvalidPrimitiveCount;
                return false;
            }

            const SurfaceWork buildWork =
                SubdivideLargeStaticTriangles(work, settings, report);
            const std::vector<SourceTriangle> triangles =
                BuildTriangles(buildWork.vertices, buildWork.indices);
            if (triangles.empty()) {
                ++report.skippedInvalidPrimitiveCount;
                return false;
            }

            ClusterSurface surface{};
            surface.nodeIndex = buildWork.nodeIndex;
            surface.meshIndex = buildWork.meshIndex;
            surface.primitiveIndex = buildWork.primitiveIndex;
            surface.materialIndex = buildWork.materialIndex;
            const uint32_t surfaceIndex = static_cast<uint32_t>(asset.surfaces.size());
            surface.firstCluster = static_cast<uint32_t>(asset.clusters.size());
            surface.firstIndex = static_cast<uint32_t>(asset.packedIndices.size());
            surface.firstVertex = static_cast<uint32_t>(asset.packedVertices.size());
            surface.firstPrimitive = static_cast<uint32_t>(asset.meshletPrimitives.size());
            surface.firstSection = static_cast<uint32_t>(asset.surfaceSections.size());
            surface.flags = buildWork.flags;

            std::vector<SurfaceSectionBuildSource> sectionSources{};
            const bool partitioned = BuildLargeStaticSurfaceSectionSources(
                buildWork,
                triangles,
                settings,
                report,
                sectionSources);
            if (!partitioned) {
                SurfaceSectionBuildSource wholeSurface{};
                wholeSurface.triangleIndices.resize(triangles.size());
                std::iota(wholeSurface.triangleIndices.begin(), wholeSurface.triangleIndices.end(), 0u);
                wholeSurface.groups = BuildClusterTriangleGroups(buildWork.vertices, triangles, settings, &report);
                sectionSources.push_back(std::move(wholeSurface));
            }

            std::vector<ClusterSurfaceSection> sections{};
            std::vector<SurfaceWork> sectionWorks{};
            sections.reserve(sectionSources.size());
            sectionWorks.reserve(sectionSources.size());

            for (const SurfaceSectionBuildSource& sectionSource : sectionSources) {
                ClusterSurfaceSection section{};
                section.surfaceIndex = surfaceIndex;
                section.sectionIndex = static_cast<uint32_t>(sections.size());
                section.firstCluster = static_cast<uint32_t>(asset.clusters.size());
                section.firstIndex = static_cast<uint32_t>(asset.packedIndices.size());
                section.firstVertex = static_cast<uint32_t>(asset.packedVertices.size());
                section.firstPrimitive = static_cast<uint32_t>(asset.meshletPrimitives.size());
                section.flags = buildWork.flags;

                SurfaceWork sectionWork = partitioned
                    ? BuildSectionSurfaceWork(buildWork, triangles, sectionSource.triangleIndices)
                    : buildWork;
                if (sectionWork.vertices.empty() || sectionWork.indices.size() < 3u) {
                    continue;
                }

                for (const std::vector<uint32_t>& group : sectionSource.groups) {
                    if (group.empty()) {
                        continue;
                    }
                    AppendClusterGeometry(buildWork, triangles, group, surfaceIndex, settings, asset);
                }

                section.clusterCount = static_cast<uint32_t>(asset.clusters.size()) - section.firstCluster;
                section.indexCount = static_cast<uint32_t>(asset.packedIndices.size()) - section.firstIndex;
                section.vertexCount = static_cast<uint32_t>(asset.packedVertices.size()) - section.firstVertex;
                section.primitiveCount =
                    static_cast<uint32_t>(asset.meshletPrimitives.size()) - section.firstPrimitive;
                section.localBounds = ComputeTriangleSubsetBounds(triangles, sectionSource.triangleIndices);
                if (!BOUNDS::IsUsable(section.localBounds)) {
                    section.localBounds = ComputeVertexBounds(buildWork.vertices);
                }
                ConfigureSectionLodMetric(
                    section,
                    settings,
                    section.localBounds);
                if (section.clusterCount == 0u) {
                    continue;
                }

                sections.push_back(section);
                sectionWorks.push_back(std::move(sectionWork));
            }

            surface.clusterCount = static_cast<uint32_t>(asset.clusters.size()) - surface.firstCluster;
            surface.indexCount = static_cast<uint32_t>(asset.packedIndices.size()) - surface.firstIndex;
            surface.vertexCount = static_cast<uint32_t>(asset.packedVertices.size()) - surface.firstVertex;
            surface.primitiveCount = static_cast<uint32_t>(asset.meshletPrimitives.size()) - surface.firstPrimitive;
            surface.localBounds = ComputeVertexBounds(buildWork.vertices);
            surface.firstLodRange = static_cast<uint32_t>(asset.surfaceLodRanges.size());
            if (surface.clusterCount == 0u || sections.empty()) {
                ++report.skippedInvalidPrimitiveCount;
                return false;
            }

            for (size_t sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex) {
                ClusterSurfaceSection& section = sections[sectionIndex];
                AppendPagesForClusterRange(
                    section.firstCluster,
                    section.clusterCount,
                    settings,
                    asset,
                    section.firstPage,
                    section.pageCount);
                if (section.pageCount == 0u && settings.buildClusterPages) {
                    continue;
                }

                AppendLod0RangeForSection(section, surfaceIndex, settings, asset);
                AppendReducedLodRangesForSection(
                    section,
                    surfaceIndex,
                    sectionWorks[sectionIndex],
                    settings,
                    partitioned && settings.lockPartitionBorders,
                    asset);
                asset.surfaceSections.push_back(section);
            }

            surface.sectionCount = static_cast<uint32_t>(asset.surfaceSections.size()) - surface.firstSection;
            surface.lodRangeCount =
                static_cast<uint32_t>(asset.surfaceLodRanges.size()) - surface.firstLodRange;
            if (surface.sectionCount == 0u || surface.lodRangeCount == 0u) {
                ++report.skippedInvalidPrimitiveCount;
                return false;
            }

            asset.surfaces.push_back(surface);
            return true;
        }

        bool HasNormalMap(const ModelAsset& model, uint32_t materialIndex) {
            if (materialIndex >= model.materials.size()) {
                return false;
            }
            return model.materials[materialIndex].normalTexture.textureIndex >= 0;
        }

        bool BuildSurfaceWork(
            const ModelAsset& model,
            const MeshPrimitive& primitive,
            uint32_t nodeIndex,
            int nodeSkinIndex,
            uint32_t meshIndex,
            uint32_t primitiveIndex,
            const MATH::Mat4& matrix,
            const ClusterCookSettings& settings,
            SurfaceWork& outWork) {

            outWork = {};
            outWork.nodeIndex = nodeIndex;
            outWork.meshIndex = meshIndex;
            outWork.primitiveIndex = primitiveIndex;
            outWork.materialIndex = primitive.materialIndex;
            outWork.flags = BuildSurfaceFlags(model, primitive, nodeSkinIndex);

            if (primitive.hasMorphTargets ||
                RENDER3D::CLUSTER::HasFlag(outWork.flags, ClusterSurfaceFlags::Unsupported)) {
                return false;
            }

            const bool skinned =
                RENDER3D::CLUSTER::HasFlag(outWork.flags, ClusterSurfaceFlags::Skinned);
            outWork.vertices.reserve(
                skinned
                    ? primitive.skinnedVertices.size()
                    : primitive.staticVertices.size());
            // 位置は node global を焼き込むが、法線と接線は逆転置で焼き込む。
            if (skinned) {
                for (const SkinnedVertex3D& vertex : primitive.skinnedVertices) {
                    outWork.vertices.push_back(ToClusterVertex(vertex));
                }
            } else {
                const MATH::Mat4 normalMatrix = MATH::BuildNormalMatrixFromWorld(matrix);
                for (const Vertex3D& vertex : primitive.staticVertices) {
                    outWork.vertices.push_back(ToClusterVertex(vertex, matrix, normalMatrix));
                }
            }
            outWork.indices = primitive.indices;

            if (settings.generateMissingNormals && HasMissingNormals(outWork.vertices)) {
                GenerateNormals(outWork.vertices, outWork.indices);
            }
            if (settings.generateMissingTangents &&
                HasNormalMap(model, primitive.materialIndex) &&
                HasMissingTangents(outWork.vertices)) {
                GenerateTangents(outWork.vertices, outWork.indices);
            }
            return !outWork.vertices.empty() && outWork.indices.size() >= 3u;
        }

        void AppendMaterialSlots(const ModelAsset& model, ClusteredGeometryAsset& asset) {
            asset.materialSlotMapping.clear();
            asset.materialSlotMapping.reserve(model.materials.size());
            for (uint32_t i = 0; i < model.materials.size(); ++i) {
                asset.materialSlotMapping.push_back(i);
            }
            if (asset.materialSlotMapping.empty()) {
                asset.materialSlotMapping.push_back(0u);
            }
        }

        void CookPrimitive(
            const ModelAsset& model,
            const MeshPrimitive& primitive,
            uint32_t nodeIndex,
            int nodeSkinIndex,
            uint32_t meshIndex,
            uint32_t primitiveIndex,
            const MATH::Mat4& matrix,
            const ClusterCookSettings& settings,
            ClusteredGeometryAsset& asset,
            ClusteredGeometryBuildReport& report) {

            SurfaceWork work{};
            if (!BuildSurfaceWork(
                    model,
                    primitive,
                    nodeIndex,
                    nodeSkinIndex,
                    meshIndex,
                    primitiveIndex,
                    matrix,
                    settings,
                    work)) {
                if (primitive.hasMorphTargets) {
                    ++report.skippedMorphPrimitiveCount;
                } else if (primitive.layout == VertexLayoutKind::SkinnedPNTTJW ||
                    !primitive.skinnedVertices.empty() ||
                    nodeSkinIndex >= 0) {
                    ++report.skippedSkinnedPrimitiveCount;
                } else {
                    ++report.skippedInvalidPrimitiveCount;
                }
                return;
            }

            (void)CookSurfaceWork(work, settings, asset, report);
        }

        void FillReportFromAsset(const ClusteredGeometryAsset& asset, ClusteredGeometryBuildReport& report) {
            report.surfaceCount = static_cast<uint32_t>(asset.surfaces.size());
            report.surfaceLodRangeCount = static_cast<uint32_t>(asset.surfaceLodRanges.size());
            report.surfaceSectionCount = static_cast<uint32_t>(asset.surfaceSections.size());
            report.clusterCount = static_cast<uint32_t>(asset.clusters.size());
            report.pageCount = static_cast<uint32_t>(asset.pages.size());
            report.meshletPrimitiveCount = static_cast<uint32_t>(asset.meshletPrimitives.size());
            report.triangleCount = asset.totalTriangleCount;
            report.vertexCount = asset.totalVertexCount;
            report.maxVerticesPerCluster = RENDER3D::CLUSTER::CountMaxClusterVertices(asset);
            report.unsupportedFeatureCount = asset.unsupportedFeatureCount;
            uint64_t clusterTriangleTotal = 0;
            float cutoffMin = (std::numeric_limits<float>::max)();
            float cutoffMax = 0.0f;
            double cutoffSum = 0.0;
            uint32_t cutoffCount = 0;
            for (const MeshCluster& cluster : asset.clusters) {
                clusterTriangleTotal += cluster.triangleCount;
                report.maxTrianglesPerClusterObserved =
                    (std::max)(report.maxTrianglesPerClusterObserved, cluster.triangleCount);
                if (cluster.triangleCount <= 1u) {
                    ++report.singleTriangleClusterCount;
                }
                if (cluster.triangleCount < RENDER3D::CLUSTER::kHcmeshMaxTrianglesPerCluster / 4u) {
                    ++report.lowTriangleClusterCount;
                }

                const float axisLength = MATH::Length(cluster.coneAxis);
                const bool axisValid = axisLength > 1.0e-5f;
                const bool cutoffValid =
                    cluster.coneCutoff > 0.0f &&
                    cluster.coneCutoff < 1.0f;
                if (axisValid && cutoffValid) {
                    ++report.normalConeValidClusterCount;
                    cutoffMin = (std::min)(cutoffMin, cluster.coneCutoff);
                    cutoffMax = (std::max)(cutoffMax, cluster.coneCutoff);
                    cutoffSum += cluster.coneCutoff;
                    ++cutoffCount;
                    continue;
                }

                ++report.normalConeInvalidClusterCount;
                if (!axisValid) {
                    ++report.normalConeAxisInvalidCount;
                }
                if (cluster.coneCutoff <= 0.0f) {
                    ++report.normalConeCutoffLeZeroCount;
                } else if (cluster.coneCutoff >= 1.0f) {
                    ++report.normalConeCutoffGeOneCount;
                }
            }
            if (report.clusterCount > 0u) {
                report.averageTrianglesPerCluster =
                    static_cast<float>(
                        static_cast<double>(clusterTriangleTotal) /
                        static_cast<double>(report.clusterCount));
            }
            if (cutoffCount > 0u) {
                report.normalConeCutoffMin = cutoffMin;
                report.normalConeCutoffAverage =
                    static_cast<float>(cutoffSum / static_cast<double>(cutoffCount));
                report.normalConeCutoffMax = cutoffMax;
            }
        }

        void FillPackedByteReport(const ClusteredGeometryAsset& asset, ClusteredGeometryBuildReport& report) {
            report.fallbackIndexByteSize =
                static_cast<uint64_t>(asset.packedIndices.size()) * sizeof(uint16_t);
            report.meshletPrimitiveByteSize =
                static_cast<uint64_t>(asset.meshletPrimitives.size()) *
                sizeof(RENDER3D::CLUSTER::ClusterGeometryGpuMeshletPrimitive);
            report.packedVertexPositionByteSize =
                static_cast<uint64_t>(asset.packedVertices.size()) *
                sizeof(RENDER3D::CLUSTER::ClusterGeometryGpuVertexPosition);
            report.packedVertexAttributeByteSize =
                static_cast<uint64_t>(asset.packedVertices.size()) *
                sizeof(RENDER3D::CLUSTER::ClusterGeometryGpuVertexAttributes);
            report.packedSkinVertexByteSize =
                static_cast<uint64_t>(asset.packedSkinningVertices.size()) *
                sizeof(RENDER3D::CLUSTER::ClusterGeometryGpuSkinVertex);

            RENDER3D::CLUSTER::ClusterGeometryPackOptions packOptions{};
            packOptions.includeFallbackIndices = true;
            RENDER3D::CLUSTER::ClusterGeometryPackedBytes packed{};
            std::string packMessage{};
            if (!RENDER3D::CLUSTER::PackClusterGeometryForGpu(
                    asset,
                    packOptions,
                    packed,
                    &packMessage)) {
                report.messages.push_back(packMessage.empty()
                    ? "cluster geometry GPU packing failed during report generation"
                    : packMessage);
                return;
            }

            report.packedGeometryByteSize = static_cast<uint64_t>(packed.geometryBytes.size());
            report.packedMetadataByteSize = static_cast<uint64_t>(packed.metadataBytes.size());
            report.packedTotalByteSize =
                report.packedGeometryByteSize +
                report.packedMetadataByteSize;
        }

        void ApplyCookBudgetReport(
            const ClusterCookSettings& settings,
            ClusteredGeometryBuildReport& report) {

            if (report.sourceStaticTriangleCount > 0u) {
                report.triangleInflationRatio =
                    static_cast<float>(
                        static_cast<double>(report.triangleCount) /
                        static_cast<double>(report.sourceStaticTriangleCount));
                report.triangleBudgetExceeded =
                    report.triangleInflationRatio > settings.maxTriangleInflationRatio;
                if (report.triangleBudgetExceeded) {
                    report.messages.push_back(
                        "cluster geometry triangle inflation exceeds budget: ratio=" +
                        std::to_string(report.triangleInflationRatio) +
                        " budget=" +
                        std::to_string(settings.maxTriangleInflationRatio));
                }
            }

            if (report.sourceStaticVertexCount > 0u) {
                report.vertexInflationRatio =
                    static_cast<float>(
                        static_cast<double>(report.vertexCount) /
                        static_cast<double>(report.sourceStaticVertexCount));
                report.vertexBudgetExceeded =
                    report.vertexInflationRatio > settings.maxVertexInflationRatio;
                if (report.vertexBudgetExceeded) {
                    report.messages.push_back(
                        "cluster geometry vertex inflation exceeds budget: ratio=" +
                        std::to_string(report.vertexInflationRatio) +
                        " budget=" +
                        std::to_string(settings.maxVertexInflationRatio));
                }
            }

            report.clusterOccupancyWarning =
                report.clusterCount > 0u &&
                report.averageTrianglesPerCluster < settings.minAverageTrianglesPerClusterWarning;
            if (report.clusterOccupancyWarning) {
                report.messages.push_back(
                    "cluster geometry average triangles per cluster is below budget: average=" +
                    std::to_string(report.averageTrianglesPerCluster) +
                    " budget=" +
                    std::to_string(settings.minAverageTrianglesPerClusterWarning));
            }
        }
    }

    bool CookClusteredGeometryFromModel(
        const ModelAsset& model,
        const AssetGuid& sourceGuid,
        const ClusterCookSettings& settings,
        ClusteredGeometryAsset& outAsset,
        ClusteredGeometryBuildReport& outReport) {

        outAsset = {};
        outReport = {};
        outAsset.sourceModelGuid = sourceGuid;
        outAsset.sourceModelPath = model.sourcePath;
        const SourceGeometryCounts sourceCounts = CountSourceGeometry(model);
        outReport.sourceStaticTriangleCount = sourceCounts.staticTriangleCount;
        outReport.sourceStaticVertexCount = sourceCounts.staticVertexCount;
        // Static surfaces bake the node transform; skinned surfaces retain bind-pose
        // model-space vertices and are identified by their per-surface flag.
        RENDER3D::CLUSTER::AddFlag(
            outAsset.flags,
            RENDER3D::CLUSTER::ClusteredGeometryFlags::NodeTransformBaked);
        RENDER3D::CLUSTER::AddFlag(
            outAsset.flags,
            RENDER3D::CLUSTER::ClusteredGeometryFlags::ClusterLocalIndices);
        RENDER3D::CLUSTER::AddFlag(
            outAsset.flags,
            RENDER3D::CLUSTER::ClusteredGeometryFlags::SourceMapping);
        RENDER3D::CLUSTER::AddFlag(
            outAsset.flags,
            RENDER3D::CLUSTER::ClusteredGeometryFlags::MeshletPrimitiveTable);
        RENDER3D::CLUSTER::AddFlag(
            outAsset.flags,
            RENDER3D::CLUSTER::ClusteredGeometryFlags::MeshletReady);
        RENDER3D::CLUSTER::AddFlag(
            outAsset.flags,
            RENDER3D::CLUSTER::ClusteredGeometryFlags::LodRanges);
        AppendMaterialSlots(model, outAsset);

        if (model.meshes.empty()) {
            outReport.messages.push_back("model has no mesh");
            outAsset.valid = false;
            return false;
        }

        const std::vector<MATH::Mat4> nodeGlobals = BOUNDS::BuildModelNodeGlobals(model);
        if (!model.nodes.empty()) {
            for (uint32_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
                const ModelNode& node = model.nodes[nodeIndex];
                if (node.meshIndex < 0 || node.meshIndex >= static_cast<int>(model.meshes.size())) {
                    continue;
                }
                const MeshAsset& mesh = model.meshes[static_cast<size_t>(node.meshIndex)];
                for (uint32_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
                    CookPrimitive(
                        model,
                        mesh.primitives[primitiveIndex],
                        nodeIndex,
                        node.skinIndex,
                        static_cast<uint32_t>(node.meshIndex),
                        primitiveIndex,
                        nodeGlobals[static_cast<size_t>(nodeIndex)],
                        settings,
                        outAsset,
                        outReport);
                }
            }
        } else {
            for (uint32_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
                const MeshAsset& mesh = model.meshes[meshIndex];
                for (uint32_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
                    CookPrimitive(
                        model,
                        mesh.primitives[primitiveIndex],
                        RENDER3D::CLUSTER::kInvalidClusterIndex,
                        -1,
                        meshIndex,
                        primitiveIndex,
                        MATH::Mat4::Identity(),
                        settings,
                        outAsset,
                        outReport);
                }
            }
        }

        outAsset.skippedSkinnedPrimitiveCount = outReport.skippedSkinnedPrimitiveCount;
        outAsset.skippedMorphPrimitiveCount = outReport.skippedMorphPrimitiveCount;
        outAsset.skippedInvalidPrimitiveCount = outReport.skippedInvalidPrimitiveCount;
        outAsset.skippedPrimitiveCount =
            outAsset.skippedSkinnedPrimitiveCount +
            outAsset.skippedMorphPrimitiveCount +
            outAsset.skippedInvalidPrimitiveCount;
        outAsset.unsupportedPrimitiveModeCount = outReport.unsupportedPrimitiveModeCount;
        outAsset.unsupportedFeatureCount = outReport.unsupportedFeatureCount;
        outAsset.skippedMorphPrimitiveCount += model.importDiagnostics.skippedMorphPrimitiveCount;
        outAsset.unsupportedPrimitiveModeCount += model.importDiagnostics.unsupportedPrimitiveModeCount;
        outAsset.unsupportedFeatureCount += model.importDiagnostics.unsupportedFeatureCount;
        outAsset.skippedPrimitiveCount =
            outAsset.skippedSkinnedPrimitiveCount +
            outAsset.skippedMorphPrimitiveCount +
            outAsset.skippedInvalidPrimitiveCount;
        outAsset.totalTriangleCount = RENDER3D::CLUSTER::CountClusterTriangles(outAsset);
        outAsset.totalVertexCount = static_cast<uint32_t>(outAsset.packedVertices.size());
        outAsset.localBounds = ComputeVertexBounds(outAsset.packedVertices);
        const bool hasSkinningData = std::any_of(
            outAsset.surfaces.begin(),
            outAsset.surfaces.end(),
            [](const ClusterSurface& surface) {
                return RENDER3D::CLUSTER::HasFlag(
                    surface.flags,
                    ClusterSurfaceFlags::Skinned);
            });
        if (hasSkinningData) {
            RENDER3D::CLUSTER::AddFlag(
                outAsset.flags,
                RENDER3D::CLUSTER::ClusteredGeometryFlags::SkinningData);
            outAsset.packedSkinningVertices.resize(outAsset.packedVertices.size());
            for (size_t vertexIndex = 0;
                 vertexIndex < outAsset.packedVertices.size();
                 ++vertexIndex) {
                const ClusterVertex& source = outAsset.packedVertices[vertexIndex];
                ClusterSkinVertex& destination =
                    outAsset.packedSkinningVertices[vertexIndex];
                for (size_t influence = 0; influence < 4u; ++influence) {
                    destination.joints[influence] = source.joints[influence];
                    destination.weights[influence] = source.weights[influence];
                }
            }
        }
        FinalizeAssetLodMetrics(outAsset, settings);
        outAsset.valid =
            !outAsset.surfaces.empty() &&
            !outAsset.surfaceLodRanges.empty() &&
            !outAsset.surfaceSections.empty() &&
            !outAsset.clusters.empty() &&
            !outAsset.packedVertices.empty() &&
            !outAsset.packedIndices.empty() &&
            !outAsset.meshletPrimitives.empty();

        FillReportFromAsset(outAsset, outReport);
        if (outAsset.valid) {
            FillPackedByteReport(outAsset, outReport);
            ApplyCookBudgetReport(settings, outReport);
        }
        outReport.skippedMorphPrimitiveCount = outAsset.skippedMorphPrimitiveCount;
        outReport.unsupportedPrimitiveModeCount = outAsset.unsupportedPrimitiveModeCount;
        outReport.unsupportedFeatureCount = outAsset.unsupportedFeatureCount;
        for (const std::string& message : model.importDiagnostics.messages) {
            outReport.messages.push_back(message);
        }
        if (!outAsset.valid) {
            outReport.messages.push_back("no clusterable static primitive");
        }
        return outAsset.valid;
    }

} // namespace HIKARI::ASSETS::GEOMETRY
