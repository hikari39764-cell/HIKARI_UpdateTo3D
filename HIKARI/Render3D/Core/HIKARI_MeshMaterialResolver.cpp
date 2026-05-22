#include "Render3D/Core/HIKARI_MeshMaterialResolver.h"

#include "HIKARI_DxTexture.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"

namespace HIKARI::MESHRENDERER {

    void MeshMaterialResolver::SetFallbacks(const MeshMaterialResolverFallbacks& fallbacks) {
        fallbacks_ = fallbacks;
    }

    void MeshMaterialResolver::ClearCache() {
        materialTextureCache_.clear();
    }

    ResolvedMaterialTextures MeshMaterialResolver::Resolve(
        const ModelAsset& asset,
        const MaterialAsset* materialAsset,
        MeshRendererDebugStats* stats) {
        ResolvedMaterialTextures textures{};
        textures.baseColor = ResolveBaseColorTexture(asset, materialAsset, stats);
        textures.normal = ResolveNormalTexture(asset, materialAsset, stats);
        textures.emissive = ResolveEmissiveTexture(asset, materialAsset, stats);
        textures.metallicRoughness = ResolveMetallicRoughnessTexture(asset, materialAsset, stats);
        textures.occlusion = ResolveOcclusionTexture(asset, materialAsset, stats);
        return textures;
    }

    int MeshMaterialResolver::GetFallbackWhite() const {
        return fallbacks_.whiteTexture;
    }

    int MeshMaterialResolver::GetFallbackNormal() const {
        return fallbacks_.normalTexture;
    }

    int MeshMaterialResolver::GetFallbackBlack() const {
        return fallbacks_.blackTexture;
    }

    int MeshMaterialResolver::ResolveBaseColorTexture(
        const ModelAsset& asset,
        const MaterialAsset* materialAsset,
        MeshRendererDebugStats* stats) {
        if (materialAsset == nullptr) {
            return fallbacks_.whiteTexture;
        }

        const int textureIndex = materialAsset->baseColorTexture.textureIndex;
        if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
            return fallbacks_.whiteTexture;
        }

        const std::string& texturePath = asset.textures[static_cast<size_t>(textureIndex)].sourcePath;
        if (texturePath.empty()) {
            return fallbacks_.whiteTexture;
        }

        auto found = materialTextureCache_.find(texturePath);
        if (found != materialTextureCache_.end()) {
            if (stats != nullptr) {
                ++stats->materialTextureCacheHitCount;
            }
            return found->second;
        }

        if (stats != nullptr) {
            ++stats->materialTextureCacheMissCount;
        }
        const int handle = DXTEX::DxTextureManager::LoadTextureSrgb("model_material/" + texturePath, texturePath);
        materialTextureCache_[texturePath] = handle;
        return handle >= 0 ? handle : fallbacks_.whiteTexture;
    }

    int MeshMaterialResolver::ResolveNormalTexture(
        const ModelAsset& asset,
        const MaterialAsset* materialAsset,
        MeshRendererDebugStats* stats) {
        if (materialAsset == nullptr) {
            return fallbacks_.normalTexture;
        }

        const int textureIndex = materialAsset->normalTexture.textureIndex;
        if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
            if (stats != nullptr) {
                ++stats->normalMapFallbackCount;
            }
            return fallbacks_.normalTexture;
        }

        const std::string& texturePath = asset.textures[static_cast<size_t>(textureIndex)].sourcePath;
        if (texturePath.empty()) {
            if (stats != nullptr) {
                ++stats->normalMapFallbackCount;
            }
            return fallbacks_.normalTexture;
        }

        const std::string cacheKey = "normal:" + texturePath;
        auto found = materialTextureCache_.find(cacheKey);
        if (found != materialTextureCache_.end()) {
            if (stats != nullptr) {
                ++stats->normalTextureCacheHitCount;
            }
            return found->second >= 0 ? found->second : fallbacks_.normalTexture;
        }

        if (stats != nullptr) {
            ++stats->normalTextureCacheMissCount;
        }
        const int handle = DXTEX::DxTextureManager::LoadTextureLinear("model_material/normal/" + texturePath, texturePath);
        materialTextureCache_[cacheKey] = handle;
        return handle >= 0 ? handle : fallbacks_.normalTexture;
    }

    int MeshMaterialResolver::ResolveEmissiveTexture(
        const ModelAsset& asset,
        const MaterialAsset* materialAsset,
        MeshRendererDebugStats* stats) {
        if (materialAsset == nullptr) {
            return fallbacks_.blackTexture;
        }

        const int textureIndex = materialAsset->emissiveTexture.textureIndex;
        if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
            if (stats != nullptr) {
                ++stats->emissiveMapFallbackCount;
            }
            return fallbacks_.blackTexture;
        }

        const std::string& texturePath = asset.textures[static_cast<size_t>(textureIndex)].sourcePath;
        if (texturePath.empty()) {
            if (stats != nullptr) {
                ++stats->emissiveMapFallbackCount;
            }
            return fallbacks_.blackTexture;
        }

        const std::string cacheKey = "emissive:" + texturePath;
        auto found = materialTextureCache_.find(cacheKey);
        if (found != materialTextureCache_.end()) {
            if (stats != nullptr) {
                ++stats->emissiveTextureCacheHitCount;
            }
            return found->second >= 0 ? found->second : fallbacks_.blackTexture;
        }

        if (stats != nullptr) {
            ++stats->emissiveTextureCacheMissCount;
        }
        const int handle = DXTEX::DxTextureManager::LoadTextureSrgb("model_material/emissive/" + texturePath, texturePath);
        materialTextureCache_[cacheKey] = handle;
        return handle >= 0 ? handle : fallbacks_.blackTexture;
    }

    int MeshMaterialResolver::ResolveMetallicRoughnessTexture(
        const ModelAsset& asset,
        const MaterialAsset* materialAsset,
        MeshRendererDebugStats* stats) {
        if (materialAsset == nullptr) {
            if (stats != nullptr) {
                ++stats->metallicRoughnessFallbackCount;
            }
            return fallbacks_.whiteTexture;
        }

        const int textureIndex = materialAsset->metallicRoughnessTexture.textureIndex;
        if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
            if (stats != nullptr) {
                ++stats->metallicRoughnessFallbackCount;
            }
            return fallbacks_.whiteTexture;
        }

        const std::string& texturePath = asset.textures[static_cast<size_t>(textureIndex)].sourcePath;
        if (texturePath.empty()) {
            if (stats != nullptr) {
                ++stats->metallicRoughnessFallbackCount;
            }
            return fallbacks_.whiteTexture;
        }

        const std::string cacheKey = "metallicRoughness:" + texturePath;
        auto found = materialTextureCache_.find(cacheKey);
        if (found != materialTextureCache_.end()) {
            if (stats != nullptr) {
                ++stats->metallicRoughnessTextureCacheHitCount;
            }
            return found->second >= 0 ? found->second : fallbacks_.whiteTexture;
        }

        if (stats != nullptr) {
            ++stats->metallicRoughnessTextureCacheMissCount;
        }
        const int handle = DXTEX::DxTextureManager::LoadTextureLinear("model_material/metallic_roughness/" + texturePath, texturePath);
        materialTextureCache_[cacheKey] = handle;
        if (handle < 0) {
            DEBUGLOG::PushRenderError(std::string("[MeshRenderer][PBRTexture][WARN] metallicRoughness texture failed. material=") +
                materialAsset->name + " sourcePath=" + texturePath + " fallback used");
        }
        return handle >= 0 ? handle : fallbacks_.whiteTexture;
    }

    int MeshMaterialResolver::ResolveOcclusionTexture(
        const ModelAsset& asset,
        const MaterialAsset* materialAsset,
        MeshRendererDebugStats* stats) {
        if (materialAsset == nullptr) {
            if (stats != nullptr) {
                ++stats->occlusionFallbackCount;
            }
            return fallbacks_.whiteTexture;
        }

        const int textureIndex = materialAsset->occlusionTexture.textureIndex;
        if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
            if (stats != nullptr) {
                ++stats->occlusionFallbackCount;
            }
            return fallbacks_.whiteTexture;
        }

        const std::string& texturePath = asset.textures[static_cast<size_t>(textureIndex)].sourcePath;
        if (texturePath.empty()) {
            if (stats != nullptr) {
                ++stats->occlusionFallbackCount;
            }
            return fallbacks_.whiteTexture;
        }

        const std::string cacheKey = "occlusion:" + texturePath;
        auto found = materialTextureCache_.find(cacheKey);
        if (found != materialTextureCache_.end()) {
            if (stats != nullptr) {
                ++stats->occlusionTextureCacheHitCount;
            }
            return found->second >= 0 ? found->second : fallbacks_.whiteTexture;
        }

        if (stats != nullptr) {
            ++stats->occlusionTextureCacheMissCount;
        }
        const int handle = DXTEX::DxTextureManager::LoadTextureLinear("model_material/occlusion/" + texturePath, texturePath);
        materialTextureCache_[cacheKey] = handle;
        if (handle < 0) {
            DEBUGLOG::PushRenderError(std::string("[MeshRenderer][PBRTexture][WARN] occlusion texture failed. material=") +
                materialAsset->name + " sourcePath=" + texturePath + " fallback used");
        }
        return handle >= 0 ? handle : fallbacks_.whiteTexture;
    }

} // namespace HIKARI::MESHRENDERER
