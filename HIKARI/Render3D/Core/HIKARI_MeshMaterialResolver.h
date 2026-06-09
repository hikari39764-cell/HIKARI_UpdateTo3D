#pragma once

#include <string>
#include <unordered_map>

#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI {
    class ModelAsset;
    struct MaterialAsset;
}

namespace HIKARI::MESHRENDERER {

    struct MeshMaterialResolverFallbacks {
        int whiteTexture = -1;
        int normalTexture = -1;
        int blackTexture = -1;
    };

    struct ResolvedMaterialTextures {
        int baseColor = -1;
        int normal = -1;
        int emissive = -1;
        int metallicRoughness = -1;
        int occlusion = -1;
    };

    class MeshMaterialResolver {
    public:
        void SetFallbacks(const MeshMaterialResolverFallbacks& fallbacks);
        void ClearCache();

        ResolvedMaterialTextures Resolve(
            const ModelAsset& asset,
            const MaterialAsset* materialAsset,
            MeshRendererDebugStats* stats);

        int GetFallbackWhite() const;
        int GetFallbackNormal() const;
        int GetFallbackBlack() const;

    private:
        int ResolveBaseColorTexture(
            const ModelAsset& asset,
            const MaterialAsset* materialAsset,
            MeshRendererDebugStats* stats);

        int ResolveNormalTexture(
            const ModelAsset& asset,
            const MaterialAsset* materialAsset,
            MeshRendererDebugStats* stats);

        int ResolveEmissiveTexture(
            const ModelAsset& asset,
            const MaterialAsset* materialAsset,
            MeshRendererDebugStats* stats);

        int ResolveMetallicRoughnessTexture(
            const ModelAsset& asset,
            const MaterialAsset* materialAsset,
            MeshRendererDebugStats* stats);

        int ResolveOcclusionTexture(
            const ModelAsset& asset,
            const MaterialAsset* materialAsset,
            MeshRendererDebugStats* stats);

    private:
        MeshMaterialResolverFallbacks fallbacks_{};
        std::unordered_map<std::string, RENDER3D::TextureResourceHandle> materialTextureCache_;
    };

} // namespace HIKARI::MESHRENDERER
