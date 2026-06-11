#include "Assets/Geometry/HIKARI_ClusteredGeometryCooker.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::GEOMETRY {

    namespace {
        using RENDER3D::CLUSTER::ClusterPage;
        using RENDER3D::CLUSTER::ClusterSurface;
        using RENDER3D::CLUSTER::ClusterSurfaceFlags;
        using RENDER3D::CLUSTER::ClusterVertex;
        using RENDER3D::CLUSTER::ClusteredGeometryAsset;
        using RENDER3D::CLUSTER::ClusteredGeometryBuildReport;
        using RENDER3D::CLUSTER::MeshCluster;

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

        uint32_t CountSharedVertices(
            const SourceTriangle& tri,
            const std::unordered_set<uint32_t>& clusterVertices) {

            uint32_t shared = 0;
            shared += clusterVertices.find(tri.i0) != clusterVertices.end() ? 1u : 0u;
            shared += clusterVertices.find(tri.i1) != clusterVertices.end() ? 1u : 0u;
            shared += clusterVertices.find(tri.i2) != clusterVertices.end() ? 1u : 0u;
            return shared;
        }

        uint32_t CountNewVertices(
            const SourceTriangle& tri,
            const std::unordered_set<uint32_t>& clusterVertices) {

            uint32_t added = 0;
            added += clusterVertices.find(tri.i0) == clusterVertices.end() ? 1u : 0u;
            added += clusterVertices.find(tri.i1) == clusterVertices.end() ? 1u : 0u;
            added += clusterVertices.find(tri.i2) == clusterVertices.end() ? 1u : 0u;
            return added;
        }

        void AddTriangleVertices(const SourceTriangle& tri, std::unordered_set<uint32_t>& clusterVertices) {
            clusterVertices.insert(tri.i0);
            clusterVertices.insert(tri.i1);
            clusterVertices.insert(tri.i2);
        }

        void AddNeighborCandidates(
            uint32_t triIndex,
            const std::vector<SourceTriangle>& triangles,
            const std::vector<std::vector<uint32_t>>& vertexToTriangles,
            const std::vector<uint8_t>& assigned,
            std::vector<uint32_t>& candidates) {

            const SourceTriangle& tri = triangles[triIndex];
            const uint32_t ids[3] = { tri.i0, tri.i1, tri.i2 };
            for (uint32_t vertexIndex : ids) {
                if (vertexIndex >= vertexToTriangles.size()) {
                    continue;
                }
                for (uint32_t candidate : vertexToTriangles[vertexIndex]) {
                    if (candidate < assigned.size() && assigned[candidate] == 0u) {
                        candidates.push_back(candidate);
                    }
                }
            }
        }

        std::vector<std::vector<uint32_t>> BuildVertexAdjacency(
            const std::vector<SourceTriangle>& triangles,
            size_t vertexCount) {

            std::vector<std::vector<uint32_t>> vertexToTriangles(vertexCount);
            for (uint32_t i = 0; i < triangles.size(); ++i) {
                const SourceTriangle& tri = triangles[i];
                vertexToTriangles[tri.i0].push_back(i);
                vertexToTriangles[tri.i1].push_back(i);
                vertexToTriangles[tri.i2].push_back(i);
            }
            return vertexToTriangles;
        }

        float ScoreCandidate(
            const SourceTriangle& tri,
            const Bounds& currentBounds,
            const std::unordered_set<uint32_t>& clusterVertices,
            uint32_t maxVerticesPerCluster) {

            const uint32_t shared = CountSharedVertices(tri, clusterVertices);
            const uint32_t newVertices = CountNewVertices(tri, clusterVertices);
            const Bounds expanded = MergeBounds(currentBounds, tri.bounds);
            const float expansionPenalty = BoundsVolume(expanded) - BoundsVolume(currentBounds);
            const float vertexPenalty =
                static_cast<float>(clusterVertices.size() + newVertices) /
                static_cast<float>((std::max)(1u, maxVerticesPerCluster));
            return static_cast<float>(shared) * 12.0f - expansionPenalty * 0.02f - vertexPenalty * 2.0f;
        }

        std::vector<uint32_t> GrowCluster(
            uint32_t seedIndex,
            const std::vector<SourceTriangle>& triangles,
            const std::vector<std::vector<uint32_t>>& vertexToTriangles,
            std::vector<uint8_t>& assigned,
            const ClusterCookSettings& settings) {

            std::vector<uint32_t> clusterTriangles{};
            std::vector<uint32_t> candidates{};
            std::unordered_set<uint32_t> clusterVertices{};

            assigned[seedIndex] = 1u;
            clusterTriangles.push_back(seedIndex);
            AddTriangleVertices(triangles[seedIndex], clusterVertices);
            Bounds currentBounds = triangles[seedIndex].bounds;
            AddNeighborCandidates(seedIndex, triangles, vertexToTriangles, assigned, candidates);

            while (clusterTriangles.size() < settings.maxTrianglesPerCluster) {
                uint32_t bestTri = RENDER3D::CLUSTER::kInvalidClusterIndex;
                float bestScore = -std::numeric_limits<float>::infinity();

                std::sort(candidates.begin(), candidates.end());
                candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

                for (uint32_t candidate : candidates) {
                    if (candidate >= triangles.size() || assigned[candidate] != 0u) {
                        continue;
                    }
                    const SourceTriangle& tri = triangles[candidate];
                    const uint32_t newVertices = CountNewVertices(tri, clusterVertices);
                    if (clusterVertices.size() + newVertices > settings.maxVerticesPerCluster) {
                        continue;
                    }
                    const uint32_t shared = CountSharedVertices(tri, clusterVertices);
                    if (settings.buildAdjacency && shared == 0u) {
                        continue;
                    }

                    const float score = ScoreCandidate(tri, currentBounds, clusterVertices, settings.maxVerticesPerCluster);
                    if (score > bestScore) {
                        bestScore = score;
                        bestTri = candidate;
                    }
                }

                if (bestTri == RENDER3D::CLUSTER::kInvalidClusterIndex) {
                    break;
                }

                assigned[bestTri] = 1u;
                clusterTriangles.push_back(bestTri);
                AddTriangleVertices(triangles[bestTri], clusterVertices);
                currentBounds = MergeBounds(currentBounds, triangles[bestTri].bounds);
                AddNeighborCandidates(bestTri, triangles, vertexToTriangles, assigned, candidates);
            }

            return clusterTriangles;
        }

        std::vector<std::vector<uint32_t>> BuildClusterTriangleGroups(
            const std::vector<SourceTriangle>& triangles,
            size_t vertexCount,
            const ClusterCookSettings& settings) {

            std::vector<std::vector<uint32_t>> groups{};
            std::vector<uint8_t> assigned(triangles.size(), 0u);
            const std::vector<std::vector<uint32_t>> adjacency =
                settings.buildAdjacency
                    ? BuildVertexAdjacency(triangles, vertexCount)
                    : std::vector<std::vector<uint32_t>>(vertexCount);

            for (uint32_t i = 0; i < triangles.size(); ++i) {
                if (assigned[i] != 0u) {
                    continue;
                }
                groups.push_back(GrowCluster(i, triangles, adjacency, assigned, settings));
            }
            return groups;
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

            Bounds bounds{};
            bool hasBounds = false;
            MATH::Vec3 normalSum{};
            for (uint32_t triangleIndex : group) {
                const SourceTriangle& tri = triangles[triangleIndex];
                const uint32_t sourceIndices[3] = { tri.i0, tri.i1, tri.i2 };
                for (uint32_t sourceIndex : sourceIndices) {
                    auto it = vertexRemap.find(sourceIndex);
                    if (it == vertexRemap.end()) {
                        const uint32_t localIndex =
                            static_cast<uint32_t>(asset.packedVertices.size()) - cluster.firstVertex;
                        vertexRemap[sourceIndex] = localIndex;
                        asset.packedVertices.push_back(work.vertices[sourceIndex]);
                        EncapsulatePoint(bounds, hasBounds, work.vertices[sourceIndex].position);
                        // cluster 内 index は meshlet と同じく局所 index として保存する。
                        asset.packedIndices.push_back(localIndex);
                    } else {
                        asset.packedIndices.push_back(it->second);
                    }
                }
                normalSum = normalSum + tri.normal;
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

            if (settings.buildNormalCone) {
                cluster.coneAxis = MATH::Normalize(normalSum);
                if (MATH::Length(cluster.coneAxis) <= 1e-5f) {
                    cluster.coneAxis = { 0.0f, 1.0f, 0.0f };
                }
                cluster.coneCutoff = 1.0f;
                for (uint32_t triangleIndex : group) {
                    cluster.coneCutoff = (std::min)(cluster.coneCutoff, MATH::Dot(cluster.coneAxis, triangles[triangleIndex].normal));
                }
            }

            cluster.flags = work.flags;
            asset.clusters.push_back(cluster);
            return static_cast<uint32_t>(asset.clusters.size() - 1u);
        }

        void BuildPagesForSurface(
            ClusterSurface& surface,
            const ClusterCookSettings& settings,
            ClusteredGeometryAsset& asset) {

            surface.firstPage = static_cast<uint32_t>(asset.pages.size());
            surface.pageCount = 0u;
            if (!settings.buildClusterPages || surface.clusterCount == 0u) {
                return;
            }

            const uint32_t maxPerPage = (std::max)(1u, settings.maxClustersPerPage);
            uint32_t remaining = surface.clusterCount;
            uint32_t clusterCursor = surface.firstCluster;
            while (remaining > 0u) {
                const uint32_t pageClusterCount = (std::min)(remaining, maxPerPage);
                ClusterPage page{};
                page.firstCluster = clusterCursor;
                page.clusterCount = pageClusterCount;
                page.firstIndex = asset.clusters[clusterCursor].firstIndex;
                page.firstVertex = asset.clusters[clusterCursor].firstVertex;

                Bounds bounds{};
                bool hasBounds = false;
                uint32_t endIndex = page.firstIndex;
                uint32_t endVertex = page.firstVertex;
                for (uint32_t i = 0; i < pageClusterCount; ++i) {
                    const MeshCluster& cluster = asset.clusters[clusterCursor + i];
                    bounds = hasBounds ? MergeBounds(bounds, cluster.localBounds) : cluster.localBounds;
                    hasBounds = hasBounds || BOUNDS::IsUsable(cluster.localBounds);
                    endIndex = (std::max)(endIndex, cluster.firstIndex + cluster.indexCount);
                    endVertex = (std::max)(endVertex, cluster.firstVertex + cluster.vertexCount);
                }
                page.indexCount = endIndex - page.firstIndex;
                page.vertexCount = endVertex - page.firstVertex;
                page.localBounds = hasBounds ? bounds : Bounds{};
                asset.pages.push_back(page);

                clusterCursor += pageClusterCount;
                remaining -= pageClusterCount;
            }
            surface.pageCount = static_cast<uint32_t>(asset.pages.size()) - surface.firstPage;
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
            surface.firstCluster = static_cast<uint32_t>(asset.clusters.size());
            surface.firstIndex = static_cast<uint32_t>(asset.packedIndices.size());
            surface.firstVertex = static_cast<uint32_t>(asset.packedVertices.size());
            surface.flags = work.flags;

            const std::vector<std::vector<uint32_t>> groups =
                BuildClusterTriangleGroups(triangles, work.vertices.size(), settings);
            for (const std::vector<uint32_t>& group : groups) {
                if (group.empty()) {
                    continue;
                }
                AppendClusterGeometry(work, triangles, group, static_cast<uint32_t>(asset.surfaces.size()), settings, asset);
            }

            surface.clusterCount = static_cast<uint32_t>(asset.clusters.size()) - surface.firstCluster;
            surface.indexCount = static_cast<uint32_t>(asset.packedIndices.size()) - surface.firstIndex;
            surface.vertexCount = static_cast<uint32_t>(asset.packedVertices.size()) - surface.firstVertex;
            surface.localBounds = ComputeVertexBounds(work.vertices);
            if (surface.clusterCount == 0u) {
                ++report.skippedInvalidPrimitiveCount;
                return false;
            }

            BuildPagesForSurface(surface, settings, asset);
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
            report.clusterCount = static_cast<uint32_t>(asset.clusters.size());
            report.pageCount = static_cast<uint32_t>(asset.pages.size());
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
        outAsset.valid =
            !outAsset.surfaces.empty() &&
            !outAsset.clusters.empty() &&
            !outAsset.packedVertices.empty() &&
            !outAsset.packedIndices.empty();

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
