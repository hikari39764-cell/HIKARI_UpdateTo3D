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
            const Material* materialOverride) {

        stats_.mode = mode_;
        stats_.enabled = IsEnabled();
        if (mode_ != ClusteredRenderMode::SelectedPreview || !clusteredGeometry.valid) {
            return false;
        }

        ModelAsset* previewModel = GetOrBuildPreviewModel(clusteredGeometry, sourceModel);
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
            receiveShadow,
            debugMode,
            materialOverride);

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

        ModelAsset* previewModel = GetOrBuildPreviewModel(clusteredGeometry, sourceModel);
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
        const uint32_t cachedCount = static_cast<uint32_t>(previewModels_.size());
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
        const ModelAsset* sourceModel) {

        auto it = previewModels_.find(&clusteredGeometry);
        if (it != previewModels_.end()) {
            return it->second.get();
        }

        auto previewModel = std::make_unique<ModelAsset>();
        previewModel->SetName("HCMESH CPU Preview");
        previewModel->SetSourcePath(clusteredGeometry.sourceModelPath);
        previewModel->SetState(ModelAsset::State::Loaded);
        CopyMaterialResources(sourceModel, *previewModel, GetRequiredMaterialCount(clusteredGeometry));

        MeshAsset mesh{};
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

        if (mesh.primitives.empty()) {
            return nullptr;
        }

        mesh.bounds = BOUNDS::ComputeMeshBounds(mesh);
        previewModel->meshes.push_back(std::move(mesh));

        ModelNode node{};
        node.name = "HCMESH CPU Preview Root";
        node.meshIndex = 0;
        previewModel->nodes.push_back(std::move(node));
        previewModel->defaultSceneRootNode = 0;
        previewModel->bounds = BOUNDS::ComputeModelBounds(*previewModel);

        ModelAsset* raw = previewModel.get();
        previewModels_[&clusteredGeometry] = std::move(previewModel);
        stats_.cachedPreviewModelCount = static_cast<uint32_t>(previewModels_.size());
        ++stats_.rebuiltPreviewModelCount;
        return raw;
    }

    ClusteredCpuPreviewRenderer& GetClusteredCpuPreviewRenderer() {
        return gPreviewRenderer;
    }

} // namespace HIKARI::RENDER3D::CLUSTER
