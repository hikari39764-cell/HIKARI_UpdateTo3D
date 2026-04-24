#include "HIKARI_Assets.h"

#include "Gfx/HIKARI_GpuResources.h"

namespace HIKARI::ASSET {

    AssetId AssetRegistry::AllocateId() {
        return nextId_++;
    }

    AssetHandle<TextureAsset> AssetRegistry::GetOrLoadTexture(const std::string& sourcePath) {
        if (sourcePath.empty()) {
            return {};
        }

        if (const auto it = textureByPath_.find(sourcePath); it != textureByPath_.end()) {
            return AssetHandle<TextureAsset>{ it->second };
        }

        TextureAsset texture{};
        texture.id = AllocateId();
        texture.state = AssetState::Loading;
        texture.name = sourcePath;
        texture.sourcePath = sourcePath;
        texture.gpuResourceId = GpuResources::LoadTexture(sourcePath, sourcePath);
        texture.state = (texture.gpuResourceId == 0) ? AssetState::Failed : AssetState::Ready;
        if (texture.gpuResourceId != 0) {
            uint32_t w = 0;
            uint32_t h = 0;
            GpuResources::GetTextureSize(texture.gpuResourceId, w, h);
            texture.width = w;
            texture.height = h;
        }

        textureByPath_[sourcePath] = texture.id;
        textures_.emplace(texture.id, texture);
        return AssetHandle<TextureAsset>{ texture.id };
    }

    AssetHandle<ModelAsset> AssetRegistry::GetOrLoadModel(const std::string& sourcePath) {
        if (sourcePath.empty()) {
            return {};
        }
        if (const auto it = modelByPath_.find(sourcePath); it != modelByPath_.end()) {
            return AssetHandle<ModelAsset>{ it->second };
        }

        ModelAsset model{};
        model.id = AllocateId();
        model.name = sourcePath;
        model.sourcePath = sourcePath;
        model.state = AssetState::Loading;

        const bool imported = ImportModelStatic(*this, model, sourcePath);
        model.state = imported ? AssetState::Ready : AssetState::Failed;

        modelByPath_[sourcePath] = model.id;
        models_.emplace(model.id, model);
        return AssetHandle<ModelAsset>{ model.id };
    }

    TextureAsset* AssetRegistry::FindTexture(AssetHandle<TextureAsset> handle) {
        const auto it = textures_.find(handle.id);
        return (it == textures_.end()) ? nullptr : &it->second;
    }

    ModelAsset* AssetRegistry::FindModel(AssetHandle<ModelAsset> handle) {
        const auto it = models_.find(handle.id);
        return (it == models_.end()) ? nullptr : &it->second;
    }

    MaterialAsset* AssetRegistry::FindMaterial(AssetHandle<MaterialAsset> handle) {
        const auto it = materials_.find(handle.id);
        return (it == materials_.end()) ? nullptr : &it->second;
    }

    MeshAsset* AssetRegistry::FindMesh(AssetHandle<MeshAsset> handle) {
        const auto it = meshes_.find(handle.id);
        return (it == meshes_.end()) ? nullptr : &it->second;
    }

    const TextureAsset* AssetRegistry::FindTexture(AssetHandle<TextureAsset> handle) const {
        const auto it = textures_.find(handle.id);
        return (it == textures_.end()) ? nullptr : &it->second;
    }

    const ModelAsset* AssetRegistry::FindModel(AssetHandle<ModelAsset> handle) const {
        const auto it = models_.find(handle.id);
        return (it == models_.end()) ? nullptr : &it->second;
    }

} // namespace HIKARI::ASSET
