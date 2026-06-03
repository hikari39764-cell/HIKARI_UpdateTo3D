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

        void CopyMaterials(const ModelAsset* sourceModel, ModelAsset& outModel, size_t minimumCount) {
            if (sourceModel != nullptr && !sourceModel->materials.empty()) {
                outModel.materials = sourceModel->materials;
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

            if (HasFlag(surface.flags, ClusterSurfaceFlags::Skinned) ||
                HasFlag(surface.flags, ClusterSurfaceFlags::Unsupported) ||
                surface.indexCount == 0u ||
                surface.firstIndex + surface.indexCount > clusteredGeometry.packedIndices.size()) {
                return false;
            }

            outPrimitive = {};
            outPrimitive.name = "HCMESH Surface " + std::to_string(surface.meshIndex) + "." + std::to_string(surface.primitiveIndex);
            outPrimitive.layout = VertexLayoutKind::StaticPNTT;
            outPrimitive.materialIndex = surface.materialIndex;

            std::unordered_map<uint32_t, uint32_t> remap{};
            remap.reserve(surface.indexCount);
            for (uint32_t i = 0; i < surface.indexCount; ++i) {
                const uint32_t packedIndex = clusteredGeometry.packedIndices[surface.firstIndex + i];
                if (packedIndex >= clusteredGeometry.packedVertices.size()) {
                    return false;
                }
                auto it = remap.find(packedIndex);
                if (it == remap.end()) {
                    const uint32_t localIndex = static_cast<uint32_t>(outPrimitive.staticVertices.size());
                    remap[packedIndex] = localIndex;
                    outPrimitive.staticVertices.push_back(ToVertex3D(clusteredGeometry.packedVertices[packedIndex]));
                    outPrimitive.indices.push_back(localIndex);
                } else {
                    outPrimitive.indices.push_back(it->second);
                }
            }

            outPrimitive.bounds = BOUNDS::ComputePrimitiveBounds(outPrimitive);
            return !outPrimitive.staticVertices.empty() && !outPrimitive.indices.empty();
        }
    }

    void ClusteredCpuPreviewRenderer::SetEnabled(bool enabled) {
        enabled_ = enabled;
        stats_.enabled = enabled_;
    }

    bool ClusteredCpuPreviewRenderer::IsEnabled() const {
        return enabled_;
    }

    bool ClusteredCpuPreviewRenderer::SubmitSelectedObjectPreview(
        const ClusteredGeometryAsset& clusteredGeometry,
            const Transform3D& transform,
            const ModelAsset* sourceModel,
            bool receiveShadow,
            MESHRENDERER::MeshRenderDebugMode debugMode,
            const Material* materialOverride) {

        stats_.enabled = enabled_;
        if (!enabled_ || !clusteredGeometry.valid) {
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
        stats_.submittedSurfaceCount += static_cast<uint32_t>(previewModel->meshes[0].primitives.size());
        return true;
    }

    void ClusteredCpuPreviewRenderer::ResetFrameStats() {
        const bool wasEnabled = enabled_;
        const uint32_t cachedCount = static_cast<uint32_t>(previewModels_.size());
        stats_ = {};
        stats_.enabled = wasEnabled;
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
        CopyMaterials(sourceModel, *previewModel, clusteredGeometry.materialSlotMapping.size());

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
