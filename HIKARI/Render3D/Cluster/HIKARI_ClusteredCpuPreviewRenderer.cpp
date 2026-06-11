#include "Render3D/Cluster/HIKARI_ClusteredCpuPreviewRenderer.h"

#include <algorithm>
#include <array>
#include <unordered_map>

#include <DirectXMath.h>

#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"

namespace HIKARI::RENDER3D::CLUSTER {

    namespace {
        ClusteredCpuPreviewRenderer gPreviewRenderer{};
        constexpr uint32_t kDebugColorBucketCount = 64u;

        bool IsReferenceSupportedSurface(const ClusterSurface& surface) {
            return !HasFlag(surface.flags, ClusterSurfaceFlags::Skinned) &&
                !HasFlag(surface.flags, ClusterSurfaceFlags::Unsupported) &&
                !HasFlag(surface.flags, ClusterSurfaceFlags::Transparent) &&
                surface.indexCount > 0u &&
                surface.vertexCount > 0u;
        }

        uint32_t CountUnsupportedReferenceSurfaces(const ClusteredGeometryAsset& asset) {
            uint32_t count = 0;
            for (const ClusterSurface& surface : asset.surfaces) {
                if (!IsReferenceSupportedSurface(surface)) {
                    ++count;
                }
            }
            return count;
        }

        uint32_t CountTransparentSurfaces(const ClusteredGeometryAsset& asset) {
            uint32_t count = 0;
            for (const ClusterSurface& surface : asset.surfaces) {
                if (HasFlag(surface.flags, ClusterSurfaceFlags::Transparent)) {
                    ++count;
                }
            }
            return count;
        }

        Vertex3D ToVertex3D(const ClusterVertex& source) {
            Vertex3D out{};
            out.position = source.position;
            out.normal = source.normal;
            out.tangent = source.tangent;
            out.uv0 = source.uv0;
            out.uv1 = source.uv1;
            out.color0 = source.color;
            return out;
        }

        size_t GetRequiredMaterialCount(const ClusteredGeometryAsset& clusteredGeometry) {
            size_t requiredCount = clusteredGeometry.materialSlotMapping.size();
            for (const ClusterSurface& surface : clusteredGeometry.surfaces) {
                requiredCount = (std::max)(requiredCount, static_cast<size_t>(surface.materialIndex) + 1u);
            }
            return requiredCount;
        }

        void CopyMaterialResources(const ModelAsset* sourceModel, ModelAsset& outModel, size_t minimumCount) {
            if (sourceModel != nullptr && !sourceModel->materials.empty()) {
                outModel.materials = sourceModel->materials;
            }
            if (sourceModel != nullptr && !sourceModel->textures.empty()) {
                // CPU参照用モデルでも元モデルのテクスチャ表を保持する。
                outModel.textures = sourceModel->textures;
            }
            while (outModel.materials.size() < minimumCount) {
                MaterialAsset material{};
                material.name = "Cluster Preview Material " + std::to_string(outModel.materials.size());
                outModel.materials.push_back(std::move(material));
            }
        }

        MATH::Vec4 DebugPaletteColor(uint32_t index) {
            const std::array<MATH::Vec4, 16> palette = {
                MATH::Vec4{ 0.95f, 0.18f, 0.22f, 1.0f },
                MATH::Vec4{ 0.18f, 0.72f, 0.95f, 1.0f },
                MATH::Vec4{ 0.28f, 0.90f, 0.36f, 1.0f },
                MATH::Vec4{ 0.98f, 0.82f, 0.20f, 1.0f },
                MATH::Vec4{ 0.78f, 0.38f, 0.98f, 1.0f },
                MATH::Vec4{ 0.95f, 0.42f, 0.72f, 1.0f },
                MATH::Vec4{ 0.15f, 0.86f, 0.72f, 1.0f },
                MATH::Vec4{ 0.98f, 0.48f, 0.18f, 1.0f },
                MATH::Vec4{ 0.45f, 0.62f, 1.00f, 1.0f },
                MATH::Vec4{ 0.62f, 0.98f, 0.20f, 1.0f },
                MATH::Vec4{ 0.90f, 0.30f, 0.88f, 1.0f },
                MATH::Vec4{ 0.20f, 0.95f, 0.55f, 1.0f },
                MATH::Vec4{ 0.95f, 0.65f, 0.15f, 1.0f },
                MATH::Vec4{ 0.30f, 0.50f, 0.95f, 1.0f },
                MATH::Vec4{ 0.72f, 0.95f, 0.42f, 1.0f },
                MATH::Vec4{ 0.95f, 0.26f, 0.48f, 1.0f },
            };
            return palette[index % static_cast<uint32_t>(palette.size())];
        }

        uint32_t DebugColorBucket(uint32_t id) {
            uint32_t x = id + 0x9e3779b9u;
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return x % kDebugColorBucketCount;
        }

        void BuildDebugPaletteMaterials(ModelAsset& outModel) {
            outModel.materials.clear();
            outModel.textures.clear();
            outModel.materials.reserve(kDebugColorBucketCount);
            for (uint32_t i = 0; i < kDebugColorBucketCount; ++i) {
                MaterialAsset material{};
                material.name = "Cluster Debug Color " + std::to_string(i);
                material.baseColorFactor = DebugPaletteColor(i);
                material.roughnessFactor = 0.85f;
                material.metallicFactor = 0.0f;
                material.featureBits = MATERIAL_FEATURES::Unlit;
                outModel.materials.push_back(std::move(material));
            }
        }

        bool AppendClusterToPrimitive(
            const ClusteredGeometryAsset& clusteredGeometry,
            const MeshCluster& cluster,
            MeshPrimitive& primitive,
            std::unordered_map<uint32_t, uint32_t>& remap) {

            if (cluster.indexCount == 0u ||
                cluster.vertexCount == 0u ||
                cluster.firstIndex + cluster.indexCount > clusteredGeometry.packedIndices.size() ||
                cluster.firstVertex + cluster.vertexCount > clusteredGeometry.packedVertices.size()) {
                return false;
            }

            for (uint32_t i = 0; i < cluster.indexCount; ++i) {
                const uint32_t clusterLocalIndex =
                    clusteredGeometry.packedIndices[cluster.firstIndex + i];
                if (clusterLocalIndex >= cluster.vertexCount) {
                    return false;
                }

                const uint32_t packedVertexIndex = cluster.firstVertex + clusterLocalIndex;
                auto it = remap.find(packedVertexIndex);
                if (it == remap.end()) {
                    const uint32_t primitiveLocalIndex =
                        static_cast<uint32_t>(primitive.staticVertices.size());
                    remap.emplace(packedVertexIndex, primitiveLocalIndex);
                    primitive.staticVertices.push_back(
                        ToVertex3D(clusteredGeometry.packedVertices[packedVertexIndex]));
                    primitive.indices.push_back(primitiveLocalIndex);
                } else {
                    primitive.indices.push_back(it->second);
                }
            }
            return true;
        }

        bool IsSurfaceClusterRangeValid(
            const ClusteredGeometryAsset& clusteredGeometry,
            const ClusterSurface& surface) {

            return IsReferenceSupportedSurface(surface) &&
                surface.firstCluster + surface.clusterCount <= clusteredGeometry.clusters.size();
        }

        void PrepareDebugPrimitiveBuckets(std::array<MeshPrimitive, kDebugColorBucketCount>& buckets) {
            for (uint32_t i = 0; i < kDebugColorBucketCount; ++i) {
                MeshPrimitive& primitive = buckets[i];
                primitive.name = "Cluster Debug Bucket " + std::to_string(i);
                primitive.layout = VertexLayoutKind::StaticPNTT;
                primitive.materialIndex = i;
            }
        }

        void AppendDebugCluster(
            const ClusteredGeometryAsset& clusteredGeometry,
            uint32_t clusterIndex,
            uint32_t colorId,
            std::array<MeshPrimitive, kDebugColorBucketCount>& buckets,
            std::array<std::unordered_map<uint32_t, uint32_t>, kDebugColorBucketCount>& remaps) {

            if (clusterIndex >= clusteredGeometry.clusters.size()) {
                return;
            }
            const uint32_t bucketIndex = DebugColorBucket(colorId);
            AppendClusterToPrimitive(
                clusteredGeometry,
                clusteredGeometry.clusters[clusterIndex],
                buckets[bucketIndex],
                remaps[bucketIndex]);
        }

        bool BuildDebugColorMesh(
            const ClusteredGeometryAsset& clusteredGeometry,
            ClusterDebugViewMode mode,
            MeshAsset& outMesh) {

            std::array<MeshPrimitive, kDebugColorBucketCount> buckets{};
            std::array<std::unordered_map<uint32_t, uint32_t>, kDebugColorBucketCount> remaps{};
            PrepareDebugPrimitiveBuckets(buckets);

            if (mode == ClusterDebugViewMode::PageColorMesh) {
                for (uint32_t pageIndex = 0; pageIndex < clusteredGeometry.pages.size(); ++pageIndex) {
                    const ClusterPage& page = clusteredGeometry.pages[pageIndex];
                    const uint32_t pageEnd =
                        (std::min)(page.firstCluster + page.clusterCount, static_cast<uint32_t>(clusteredGeometry.clusters.size()));
                    for (uint32_t clusterIndex = page.firstCluster; clusterIndex < pageEnd; ++clusterIndex) {
                        const MeshCluster& cluster = clusteredGeometry.clusters[clusterIndex];
                        if (cluster.surfaceIndex >= clusteredGeometry.surfaces.size() ||
                            !IsSurfaceClusterRangeValid(clusteredGeometry, clusteredGeometry.surfaces[cluster.surfaceIndex])) {
                            continue;
                        }
                        AppendDebugCluster(clusteredGeometry, clusterIndex, pageIndex, buckets, remaps);
                    }
                }
            } else {
                for (uint32_t surfaceIndex = 0; surfaceIndex < clusteredGeometry.surfaces.size(); ++surfaceIndex) {
                    const ClusterSurface& surface = clusteredGeometry.surfaces[surfaceIndex];
                    if (!IsSurfaceClusterRangeValid(clusteredGeometry, surface)) {
                        continue;
                    }
                    const uint32_t clusterEnd = surface.firstCluster + surface.clusterCount;
                    for (uint32_t clusterIndex = surface.firstCluster; clusterIndex < clusterEnd; ++clusterIndex) {
                        const uint32_t colorId =
                            mode == ClusterDebugViewMode::SurfaceColorMesh ? surfaceIndex : clusterIndex;
                        AppendDebugCluster(clusteredGeometry, clusterIndex, colorId, buckets, remaps);
                    }
                }
            }

            outMesh = {};
            outMesh.name = "HCMESH Cluster Debug Mesh";
            outMesh.primitives.reserve(kDebugColorBucketCount);
            for (MeshPrimitive& primitive : buckets) {
                if (primitive.staticVertices.empty() || primitive.indices.empty()) {
                    continue;
                }
                primitive.bounds = BOUNDS::ComputePrimitiveBounds(primitive);
                outMesh.primitives.push_back(std::move(primitive));
            }

            if (outMesh.primitives.empty()) {
                return false;
            }
            outMesh.bounds = BOUNDS::ComputeMeshBounds(outMesh);
            return true;
        }

        bool BuildPrimitiveFromSurface(
            const ClusteredGeometryAsset& clusteredGeometry,
            const ClusterSurface& surface,
            MeshPrimitive& outPrimitive) {

            if (!IsReferenceSupportedSurface(surface) ||
                surface.clusterCount == 0u ||
                surface.firstCluster + surface.clusterCount > clusteredGeometry.clusters.size()) {
                return false;
            }

            outPrimitive = {};
            outPrimitive.name = "HCMESH Surface " + std::to_string(surface.meshIndex) + "." + std::to_string(surface.primitiveIndex);
            outPrimitive.layout = VertexLayoutKind::StaticPNTT;
            outPrimitive.materialIndex = surface.materialIndex;

            std::unordered_map<uint32_t, uint32_t> remap{};
            remap.reserve(surface.indexCount);
            for (uint32_t clusterOffset = 0; clusterOffset < surface.clusterCount; ++clusterOffset) {
                const MeshCluster& cluster = clusteredGeometry.clusters[surface.firstCluster + clusterOffset];
                if (cluster.firstIndex + cluster.indexCount > clusteredGeometry.packedIndices.size() ||
                    cluster.firstVertex + cluster.vertexCount > clusteredGeometry.packedVertices.size()) {
                    return false;
                }

                for (uint32_t i = 0; i < cluster.indexCount; ++i) {
                    const uint32_t clusterLocalIndex =
                        clusteredGeometry.packedIndices[cluster.firstIndex + i];
                    if (clusterLocalIndex >= cluster.vertexCount) {
                        return false;
                    }

                    const uint32_t packedVertexIndex = cluster.firstVertex + clusterLocalIndex;
                    auto it = remap.find(packedVertexIndex);
                    if (it == remap.end()) {
                        const uint32_t primitiveLocalIndex = static_cast<uint32_t>(outPrimitive.staticVertices.size());
                        remap[packedVertexIndex] = primitiveLocalIndex;
                        outPrimitive.staticVertices.push_back(
                            ToVertex3D(clusteredGeometry.packedVertices[packedVertexIndex]));
                        outPrimitive.indices.push_back(primitiveLocalIndex);
                    } else {
                        outPrimitive.indices.push_back(it->second);
                    }
                }
            }

            outPrimitive.bounds = BOUNDS::ComputePrimitiveBounds(outPrimitive);
            return !outPrimitive.staticVertices.empty() && !outPrimitive.indices.empty();
        }
    }

    void ClusteredCpuPreviewRenderer::SetMode(ClusteredRenderMode mode) {
        mode_ = mode;
        stats_.mode = mode_;
        stats_.enabled = mode_ != ClusteredRenderMode::Off;
    }

    ClusteredRenderMode ClusteredCpuPreviewRenderer::GetMode() const {
        return mode_;
    }

    void ClusteredCpuPreviewRenderer::SetEnabled(bool enabled) {
        SetMode(enabled ? ClusteredRenderMode::SelectedPreview : ClusteredRenderMode::Off);
    }

    bool ClusteredCpuPreviewRenderer::IsEnabled() const {
        return mode_ != ClusteredRenderMode::Off;
    }

    bool ClusteredCpuPreviewRenderer::IsCpuReferenceMode() const {
        return mode_ == ClusteredRenderMode::CpuReference;
    }

    bool ClusteredCpuPreviewRenderer::SubmitSelectedObjectPreview(
        const ClusteredGeometryAsset& clusteredGeometry,
        const Transform3D& transform,
        const ModelAsset* sourceModel,
        bool receiveShadow,
        MESHRENDERER::MeshRenderDebugMode debugMode,
        const Material* materialOverride,
        const ClusterDebugOptions* debugOptions) {

        stats_.mode = mode_;
        stats_.enabled = IsEnabled();
        const bool colorDebug =
            debugOptions != nullptr &&
            IsClusterDebugColorMeshMode(debugOptions->mode);
        if ((mode_ != ClusteredRenderMode::SelectedPreview && !colorDebug) ||
            !clusteredGeometry.valid) {
            return false;
        }

        PreviewModelKind kind = PreviewModelKind::SurfaceReference;
        if (colorDebug) {
            if (debugOptions->mode == ClusterDebugViewMode::PageColorMesh) {
                kind = PreviewModelKind::PageColor;
            } else if (debugOptions->mode == ClusterDebugViewMode::SurfaceColorMesh) {
                kind = PreviewModelKind::SurfaceColor;
            } else {
                kind = PreviewModelKind::ClusterColor;
            }
        }

        ModelAsset* previewModel = GetOrBuildPreviewModel(clusteredGeometry, sourceModel, kind);
        if (previewModel == nullptr || previewModel->meshes.empty()) {
            return false;
        }

        DirectX::XMFLOAT4 fxValues[VFX::kMaterialFxUserCount]{};
        MESHRENDERER::SubmitStaticMesh(
            *previewModel,
            transform,
            {},
            0u,
            fxValues,
            false,
            colorDebug ? false : receiveShadow,
            colorDebug ? MESHRENDERER::MeshRenderDebugMode::Normal : debugMode,
            colorDebug ? nullptr : materialOverride);

        ++stats_.submittedObjectCount;
        ++stats_.selectedPreviewObjectCount;
        stats_.submittedSurfaceCount += static_cast<uint32_t>(previewModel->meshes[0].primitives.size());
        return true;
    }

    bool ClusteredCpuPreviewRenderer::CanSubmitCompleteReference(const ClusteredGeometryAsset& clusteredGeometry) const {
        return clusteredGeometry.valid &&
            !clusteredGeometry.surfaces.empty() &&
            clusteredGeometry.skippedPrimitiveCount == 0u &&
            clusteredGeometry.skippedSkinnedPrimitiveCount == 0u &&
            clusteredGeometry.skippedMorphPrimitiveCount == 0u &&
            clusteredGeometry.skippedInvalidPrimitiveCount == 0u &&
            CountUnsupportedReferenceSurfaces(clusteredGeometry) == 0u;
    }

    bool ClusteredCpuPreviewRenderer::SubmitReferenceObject(
        const ClusteredGeometryAsset& clusteredGeometry,
        const Transform3D& transform,
        const ModelAsset* sourceModel,
        const std::string& materialFxProfileId,
        uint32_t postGroupMask,
        const DirectX::XMFLOAT4(&materialFxParamValues)[VFX::kMaterialFxUserCount],
        bool materialFxValuesInitialized,
        bool receiveShadow,
        MESHRENDERER::MeshRenderDebugMode debugMode,
        const Material* materialOverride) {

        stats_.mode = mode_;
        stats_.enabled = IsEnabled();
        if (mode_ != ClusteredRenderMode::CpuReference) {
            return false;
        }

        if (!CanSubmitCompleteReference(clusteredGeometry)) {
            RecordFallbackObject(static_cast<uint32_t>(clusteredGeometry.surfaces.size()));
            stats_.transparentFallbackSurfaceCount += CountTransparentSurfaces(clusteredGeometry);
            stats_.unsupportedFallbackSurfaceCount += CountUnsupportedReferenceSurfaces(clusteredGeometry);
            return false;
        }

        ModelAsset* previewModel = GetOrBuildPreviewModel(
            clusteredGeometry,
            sourceModel,
            PreviewModelKind::SurfaceReference);
        if (previewModel == nullptr || previewModel->meshes.empty()) {
            RecordFallbackObject(static_cast<uint32_t>(clusteredGeometry.surfaces.size()));
            return false;
        }

        MESHRENDERER::SubmitStaticMesh(
            *previewModel,
            // HCMESH 頂点は node bake 済みなので object transform のみ掛ける。
            transform,
            materialFxProfileId,
            postGroupMask,
            materialFxParamValues,
            materialFxValuesInitialized,
            receiveShadow,
            debugMode,
            materialOverride);

        ++stats_.submittedObjectCount;
        stats_.submittedSurfaceCount += static_cast<uint32_t>(previewModel->meshes[0].primitives.size());
        return true;
    }

    void ClusteredCpuPreviewRenderer::RecordReferenceCandidate() {
        ++stats_.candidateObjectCount;
    }

    void ClusteredCpuPreviewRenderer::RecordFallbackObject(uint32_t surfaceCount) {
        ++stats_.fallbackObjectCount;
        stats_.fallbackSurfaceCount += surfaceCount;
    }

    void ClusteredCpuPreviewRenderer::ResetFrameStats() {
        const ClusteredRenderMode mode = mode_;
        uint32_t cachedCount = 0;
        for (const auto& entry : previewModels_) {
            for (const auto& model : entry.second.models) {
                if (model != nullptr) {
                    ++cachedCount;
                }
            }
        }
        stats_ = {};
        stats_.mode = mode;
        stats_.enabled = mode != ClusteredRenderMode::Off;
        stats_.cachedPreviewModelCount = cachedCount;
    }

    void ClusteredCpuPreviewRenderer::ClearCache() {
        previewModels_.clear();
        stats_.cachedPreviewModelCount = 0;
    }

    const ClusteredCpuPreviewStats& ClusteredCpuPreviewRenderer::GetStats() const {
        return stats_;
    }

    ModelAsset* ClusteredCpuPreviewRenderer::GetOrBuildPreviewModel(
        const ClusteredGeometryAsset& clusteredGeometry,
        const ModelAsset* sourceModel,
        PreviewModelKind kind) {

        PreviewModelCache& cache = previewModels_[&clusteredGeometry];
        const size_t kindIndex = static_cast<size_t>(kind);
        if (kindIndex >= cache.models.size()) {
            return nullptr;
        }
        if (cache.models[kindIndex] != nullptr) {
            return cache.models[kindIndex].get();
        }

        auto previewModel = std::make_unique<ModelAsset>();
        previewModel->SetName("HCMESH CPU Preview");
        previewModel->SetSourcePath(clusteredGeometry.sourceModelPath);
        previewModel->SetState(ModelAsset::State::Loaded);

        MeshAsset mesh{};
        if (kind == PreviewModelKind::SurfaceReference) {
            CopyMaterialResources(sourceModel, *previewModel, GetRequiredMaterialCount(clusteredGeometry));
            mesh.name = "HCMESH CPU Preview Mesh";
            mesh.primitives.reserve(clusteredGeometry.surfaces.size());

            for (const ClusterSurface& surface : clusteredGeometry.surfaces) {
                MeshPrimitive primitive{};
                if (BuildPrimitiveFromSurface(clusteredGeometry, surface, primitive)) {
                    mesh.primitives.push_back(std::move(primitive));
                } else {
                    ++stats_.fallbackSurfaceCount;
                }
            }

            if (!mesh.primitives.empty()) {
                mesh.bounds = BOUNDS::ComputeMeshBounds(mesh);
            }
        } else {
            BuildDebugPaletteMaterials(*previewModel);
            ClusterDebugViewMode colorMode = ClusterDebugViewMode::ClusterColorMesh;
            if (kind == PreviewModelKind::PageColor) {
                colorMode = ClusterDebugViewMode::PageColorMesh;
            } else if (kind == PreviewModelKind::SurfaceColor) {
                colorMode = ClusterDebugViewMode::SurfaceColorMesh;
            }
            if (!BuildDebugColorMesh(clusteredGeometry, colorMode, mesh)) {
                return nullptr;
            }
        }

        if (mesh.primitives.empty()) {
            return nullptr;
        }
        previewModel->meshes.push_back(std::move(mesh));

        ModelNode node{};
        node.name = "HCMESH CPU Preview Root";
        node.meshIndex = 0;
        previewModel->nodes.push_back(std::move(node));
        previewModel->defaultSceneRootNode = 0;
        previewModel->bounds = BOUNDS::ComputeModelBounds(*previewModel);

        ModelAsset* raw = previewModel.get();
        cache.models[kindIndex] = std::move(previewModel);
        uint32_t cachedCount = 0;
        for (const auto& entry : previewModels_) {
            for (const auto& model : entry.second.models) {
                if (model != nullptr) {
                    ++cachedCount;
                }
            }
        }
        stats_.cachedPreviewModelCount = cachedCount;
        ++stats_.rebuiltPreviewModelCount;
        return raw;
    }

    ClusteredCpuPreviewRenderer& GetClusteredCpuPreviewRenderer() {
        return gPreviewRenderer;
    }

} // namespace HIKARI::RENDER3D::CLUSTER
