#include "Assets/Geometry/HIKARI_ClusteredGeometryCooker.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>

#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Tools/Geometry/HIKARI_MeshLodGenerator.h"
#include "../../../ThirdParty/meshoptimizer/src/meshoptimizer.h"

namespace HIKARI::ASSETS::GEOMETRY {

    namespace {
        using RENDER3D::CLUSTER::ClusterPage;
        using RENDER3D::CLUSTER::ClusterSurface;
        using RENDER3D::CLUSTER::ClusterSurfaceFlags;
        using RENDER3D::CLUSTER::ClusterSurfaceLodRange;
        using RENDER3D::CLUSTER::ClusterSurfaceSection;
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

        struct SurfaceLodBuildResult {
            SurfaceWork work{};
            float geometricError = 0.0f;
        };

        struct SurfaceSectionBuildSource {
            std::vector<uint32_t> triangleIndices{};
            std::vector<std::vector<uint32_t>> groups{};
        };

        std::vector<std::vector<uint32_t>> BuildClusterTriangleGroups(
            const std::vector<ClusterVertex>& vertices,
            const std::vector<SourceTriangle>& triangles,
            const ClusterCookSettings& settings);

        bool IsFiniteVec3(const MATH::Vec3& v);

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
            if (!IsFiniteVec3(outCluster.coneApex) ||
                !IsFiniteVec3(outCluster.coneAxis) ||
                !std::isfinite(outCluster.coneCutoff) ||
                MATH::Length(outCluster.coneAxis) <= 1.0e-5f) {
                outCluster.coneApex = outCluster.sphereCenter;
                outCluster.coneAxis = { 0.0f, 1.0f, 0.0f };
                outCluster.coneCutoff = 1.0f;
            }
            return true;
        }

        bool IsFiniteVec3(const MATH::Vec3& v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        MATH::Vec3 TransformVector(const MATH::Mat4& matrix, const MATH::Vec3& value) {
            const MATH::Vec4 transformed = matrix.TransformPoint({ value.x, value.y, value.z, 0.0f });
            return { transformed.x, transformed.y, transformed.z };
        }

        MATH::Mat4 BuildNormalMatrixFromWorld(const MATH::Mat4& world) {
            const float a00 = world.m[0][0];
            const float a01 = world.m[1][0];
            const float a02 = world.m[2][0];
            const float a10 = world.m[0][1];
            const float a11 = world.m[1][1];
            const float a12 = world.m[2][1];
            const float a20 = world.m[0][2];
            const float a21 = world.m[1][2];
            const float a22 = world.m[2][2];

            const float det =
                a00 * (a11 * a22 - a12 * a21) -
                a01 * (a10 * a22 - a12 * a20) +
                a02 * (a10 * a21 - a11 * a20);
            if (std::abs(det) <= 1e-6f) {
                return MATH::Mat4::Identity();
            }

            const float invDet = 1.0f / det;
            MATH::Mat4 normalMatrix = MATH::Mat4::Identity();
            normalMatrix.m[0][0] = (a11 * a22 - a12 * a21) * invDet;
            normalMatrix.m[0][1] = (a02 * a21 - a01 * a22) * invDet;
            normalMatrix.m[0][2] = (a01 * a12 - a02 * a11) * invDet;
            normalMatrix.m[1][0] = (a12 * a20 - a10 * a22) * invDet;
            normalMatrix.m[1][1] = (a00 * a22 - a02 * a20) * invDet;
            normalMatrix.m[1][2] = (a02 * a10 - a00 * a12) * invDet;
            normalMatrix.m[2][0] = (a10 * a21 - a11 * a20) * invDet;
            normalMatrix.m[2][1] = (a01 * a20 - a00 * a21) * invDet;
            normalMatrix.m[2][2] = (a00 * a11 - a01 * a10) * invDet;
            return normalMatrix;
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

        void EncapsulatePoint(Bounds& bounds, bool& hasBounds, const MATH::Vec3& point) {
            if (!IsFiniteVec3(point)) {
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
            tri.normal = MATH::Normalize(MATH::Cross(e1, e2));
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
            if (primitive.indices.size() < 3u || primitive.staticVertices.empty()) {
                RENDER3D::CLUSTER::AddFlag(flags, ClusterSurfaceFlags::Unsupported);
            }
            return flags;
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
                (std::max)(0.5f, settings.largeSurfacePartitionMaxExtent);
            return MaxBoundsExtent(bounds) > maxExtent;
        }

        void PartitionTriangleIndicesRecursive(
            const std::vector<SourceTriangle>& triangles,
            std::vector<uint32_t> triangleIndices,
            const ClusterCookSettings& settings,
            uint32_t depth,
            std::vector<std::vector<uint32_t>>& outPartitions) {

            const Bounds bounds = ComputeTriangleSubsetBounds(triangles, triangleIndices);
            const uint32_t minChunkTriangles =
                (std::max)(1u, settings.largeSurfacePartitionMinTrianglesPerChunk);
            const float maxExtent =
                (std::max)(0.5f, settings.largeSurfacePartitionMaxExtent);
            if (triangleIndices.size() < static_cast<size_t>(minChunkTriangles) * 2u ||
                depth >= settings.largeSurfacePartitionMaxDepth ||
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
                settings,
                depth + 1u,
                outPartitions);
            PartitionTriangleIndicesRecursive(
                triangles,
                std::move(right),
                settings,
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

            std::vector<std::vector<uint32_t>> partitions{};
            PartitionTriangleIndicesRecursive(
                triangles,
                std::move(triangleIndices),
                settings,
                0u,
                partitions);
            if (partitions.size() <= 1u) {
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
                    BuildClusterTriangleGroups(work.vertices, partitionTriangles, settings);
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

        std::vector<std::vector<uint32_t>> BuildSequentialTriangleGroups(
            const std::vector<SourceTriangle>& triangles,
            const ClusterCookSettings& settings) {

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
            return groups;
        }

        std::vector<std::vector<uint32_t>> BuildClusterTriangleGroups(
            const std::vector<ClusterVertex>& vertices,
            const std::vector<SourceTriangle>& triangles,
            const ClusterCookSettings& settings) {

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
                return BuildSequentialTriangleGroups(triangles, settings);
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
                return BuildSequentialTriangleGroups(triangles, settings);
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
                        return BuildSequentialTriangleGroups(triangles, settings);
                    }

                    group.push_back(it->second.back());
                    it->second.pop_back();
                }

                if (!group.empty()) {
                    groups.push_back(std::move(group));
                }
            }

            return groups.empty()
                ? BuildSequentialTriangleGroups(triangles, settings)
                : groups;
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

            const uint32_t maxPerPage = (std::max)(1u, settings.maxClustersPerPage);
            uint32_t remaining = clusterCount;
            uint32_t clusterCursor = firstCluster;
            while (remaining > 0u) {
                const uint32_t pageClusterCount = (std::min)(remaining, maxPerPage);
                ClusterPage page{};
                page.firstCluster = clusterCursor;
                page.clusterCount = pageClusterCount;
                page.firstIndex = asset.clusters[clusterCursor].firstIndex;
                page.firstVertex = asset.clusters[clusterCursor].firstVertex;
                page.firstPrimitive = asset.clusters[clusterCursor].firstPrimitive;

                Bounds bounds{};
                bool hasBounds = false;
                uint32_t endIndex = page.firstIndex;
                uint32_t endVertex = page.firstVertex;
                uint32_t endPrimitive = page.firstPrimitive;
                for (uint32_t i = 0; i < pageClusterCount; ++i) {
                    const MeshCluster& cluster = asset.clusters[clusterCursor + i];
                    bounds = hasBounds ? MergeBounds(bounds, cluster.localBounds) : cluster.localBounds;
                    hasBounds = hasBounds || BOUNDS::IsUsable(cluster.localBounds);
                    endIndex = (std::max)(endIndex, cluster.firstIndex + cluster.indexCount);
                    endVertex = (std::max)(endVertex, cluster.firstVertex + cluster.vertexCount);
                    endPrimitive = (std::max)(endPrimitive, cluster.firstPrimitive + cluster.primitiveCount);
                }
                page.indexCount = endIndex - page.firstIndex;
                page.vertexCount = endVertex - page.firstVertex;
                page.primitiveCount = endPrimitive - page.firstPrimitive;
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

            const std::vector<SourceTriangle> triangles = BuildTriangles(work.vertices, work.indices);
            if (triangles.empty()) {
                ++report.skippedInvalidPrimitiveCount;
                return false;
            }

            ClusterSurface surface{};
            surface.nodeIndex = work.nodeIndex;
            surface.meshIndex = work.meshIndex;
            surface.primitiveIndex = work.primitiveIndex;
            surface.materialIndex = work.materialIndex;
            const uint32_t surfaceIndex = static_cast<uint32_t>(asset.surfaces.size());
            surface.firstCluster = static_cast<uint32_t>(asset.clusters.size());
            surface.firstIndex = static_cast<uint32_t>(asset.packedIndices.size());
            surface.firstVertex = static_cast<uint32_t>(asset.packedVertices.size());
            surface.firstPrimitive = static_cast<uint32_t>(asset.meshletPrimitives.size());
            surface.firstSection = static_cast<uint32_t>(asset.surfaceSections.size());
            surface.flags = work.flags;

            std::vector<SurfaceSectionBuildSource> sectionSources{};
            const bool partitioned = BuildLargeStaticSurfaceSectionSources(
                work,
                triangles,
                settings,
                report,
                sectionSources);
            if (!partitioned) {
                SurfaceSectionBuildSource wholeSurface{};
                wholeSurface.triangleIndices.resize(triangles.size());
                std::iota(wholeSurface.triangleIndices.begin(), wholeSurface.triangleIndices.end(), 0u);
                wholeSurface.groups = BuildClusterTriangleGroups(work.vertices, triangles, settings);
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
                section.flags = work.flags;

                SurfaceWork sectionWork = partitioned
                    ? BuildSectionSurfaceWork(work, triangles, sectionSource.triangleIndices)
                    : work;
                if (sectionWork.vertices.empty() || sectionWork.indices.size() < 3u) {
                    continue;
                }

                for (const std::vector<uint32_t>& group : sectionSource.groups) {
                    if (group.empty()) {
                        continue;
                    }
                    AppendClusterGeometry(work, triangles, group, surfaceIndex, settings, asset);
                }

                section.clusterCount = static_cast<uint32_t>(asset.clusters.size()) - section.firstCluster;
                section.indexCount = static_cast<uint32_t>(asset.packedIndices.size()) - section.firstIndex;
                section.vertexCount = static_cast<uint32_t>(asset.packedVertices.size()) - section.firstVertex;
                section.primitiveCount =
                    static_cast<uint32_t>(asset.meshletPrimitives.size()) - section.firstPrimitive;
                section.localBounds = ComputeTriangleSubsetBounds(triangles, sectionSource.triangleIndices);
                if (!BOUNDS::IsUsable(section.localBounds)) {
                    section.localBounds = ComputeVertexBounds(work.vertices);
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
            surface.localBounds = ComputeVertexBounds(work.vertices);
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
                RENDER3D::CLUSTER::HasFlag(outWork.flags, ClusterSurfaceFlags::Skinned) ||
                RENDER3D::CLUSTER::HasFlag(outWork.flags, ClusterSurfaceFlags::Unsupported)) {
                return false;
            }

            outWork.vertices.reserve(primitive.staticVertices.size());
            // 位置は node global を焼き込むが、法線と接線は逆転置で焼き込む。
            const MATH::Mat4 normalMatrix = BuildNormalMatrixFromWorld(matrix);
            for (const Vertex3D& vertex : primitive.staticVertices) {
                outWork.vertices.push_back(ToClusterVertex(vertex, matrix, normalMatrix));
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
        // HCMESH は node 行列を焼き込んだ model local 頂点を持つ。
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
