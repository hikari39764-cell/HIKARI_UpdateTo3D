#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Render3D/Core/HIKARI_MaterialTextureUsage.h"
#include "Render3D/HIKARI_ModelAsset.h"

namespace HIKARI {

    using ModelTexturePathResolver = std::function<std::string(
        const std::string& sourceTexturePath,
        ModelTextureUsage usage)>;

    enum class ModelTextureResolveFailureKind {
        Missing,
        Ambiguous,
    };

    struct ModelTextureResolveStats {
        int total = 0;
        int resolvedHtex = 0;
        int fallbackRaw = 0;
        int missing = 0;
        int ambiguous = 0;
    };

    class ModelManager {
    public:
        ModelAsset* RegisterAsset(const std::string& name, const std::string& sourcePath);
        ModelAsset* FindAsset(const std::string& name);
        const ModelAsset* FindAsset(const std::string& name) const;

        const std::vector<std::unique_ptr<ModelAsset>>& GetAssets() const;

        bool MarkLoaded(const std::string& name);
        bool MarkFailed(const std::string& name);

        bool LoadAssetNow(const std::string& name);
        bool LoadAllRegisteredAssets();
        bool LoadCpuAssetFromSource(ModelAsset& asset);

        size_t CountLoadedAssets() const;
        size_t CountFailedAssets() const;

        void SetTexturePathResolver(ModelTexturePathResolver resolver);
        void ClearTexturePathResolver();
        void ResetTextureResolveStats();
        void RecordTextureResolveFailure(ModelTextureResolveFailureKind kind) const;
        const ModelTextureResolveStats& GetTextureResolveStats() const;

    private:
        bool LoadAsObj(ModelAsset& asset, bool buildRuntimeResources);
        bool LoadAsGltf(ModelAsset& asset, bool buildRuntimeResources);
        bool LoadAsHmodel(ModelAsset& asset);
        bool BuildRuntimeResources(ModelAsset& asset);
        bool BuildBuiltinCube(ModelAsset& asset);
        RuntimeTextureSlot ResolveAndLoadMaterialTexture(
            const ModelAsset& asset,
            const std::string& textureName,
            const TextureSlot& textureSlot,
            ModelTextureUsage usage) const;
        void LoadPbrTextureSlots(
            const ModelAsset& asset,
            const MaterialAsset& source,
            Material& runtimeMaterial,
            const std::string& materialNamePrefix) const;
        void ResolvePbrTexturePaths(ModelAsset& asset) const;
        std::string ResolveTexturePath(
            const std::string& sourceTexturePath,
            ModelTextureUsage usage) const;

    private:
        std::vector<std::unique_ptr<ModelAsset>> assets_;
        std::unordered_map<std::string, ModelAsset*> nameToAsset_;
        ModelTexturePathResolver texturePathResolver_{};
        mutable ModelTextureResolveStats textureResolveStats_{};
    };

} // namespace HIKARI
