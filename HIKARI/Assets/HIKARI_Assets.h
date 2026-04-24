#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#undef max
#undef min

namespace HIKARI::ASSET {

    using AssetId = uint64_t;
    constexpr AssetId kInvalidAssetId = std::numeric_limits<AssetId>::max();

    enum class AssetType : uint8_t {
        Texture,
        Material,
        Mesh,
        Model,
        SpriteAtlas,
        SpriteAnimationClip,
        Skeleton,
        AnimationClip,
        Unknown
    };

    enum class AssetState : uint8_t {
        Unloaded,
        Loading,
        Ready,
        Failed
    };

    enum class AlphaMode : uint8_t {
        Opaque = 0,
        Mask,
        Blend
    };

    template<class TAsset>
    struct AssetHandle {
        AssetId id = kInvalidAssetId;

        bool IsValid() const { return id != kInvalidAssetId; }
        explicit operator bool() const { return IsValid(); }
        bool operator==(const AssetHandle& rhs) const { return id == rhs.id; }
        bool operator!=(const AssetHandle& rhs) const { return !(*this == rhs); }
    };

    struct TextureAsset {
        AssetId id = kInvalidAssetId;
        AssetState state = AssetState::Unloaded;
        std::string name{};
        std::string sourcePath{};
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t gpuResourceId = 0;
    };

    struct MaterialAsset {
        AssetId id = kInvalidAssetId;
        AssetState state = AssetState::Unloaded;
        std::string name{};
        DirectX::XMFLOAT4 baseColorFactor{ 1, 1, 1, 1 };
        DirectX::XMFLOAT3 emissiveFactor{ 0, 0, 0 };
        float normalScale = 1.0f;
        float occlusionStrength = 1.0f;
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        AlphaMode alphaMode = AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
        AssetHandle<TextureAsset> baseColorTexture{};
        AssetHandle<TextureAsset> normalTexture{};
        AssetHandle<TextureAsset> ormTexture{};
        AssetHandle<TextureAsset> emissiveTexture{};
        std::string shaderProfileId{};
        uint32_t featureBits = 0;
    };

    struct MeshAsset {
        struct Vertex {
            float px = 0, py = 0, pz = 0;
            float nx = 0, ny = 1, nz = 0;
            float tx = 1, ty = 0, tz = 0, tw = 1;
            float u = 0, v = 0;
        };
        struct Submesh {
            uint32_t indexOffset = 0;
            uint32_t indexCount = 0;
        };

        AssetId id = kInvalidAssetId;
        AssetState state = AssetState::Unloaded;
        std::string name{};
        std::vector<Vertex> vertices{};
        std::vector<uint32_t> indices{};
        std::vector<Submesh> submeshes{};
        uint32_t gpuMeshId = 0;
    };

    struct ModelAsset {
        struct Primitive {
            AssetHandle<MeshAsset> mesh{};
            AssetHandle<MaterialAsset> material{};
            DirectX::XMFLOAT4X4 localTransform{ 1, 0, 0, 0,
                                                0, 1, 0, 0,
                                                0, 0, 1, 0,
                                                0, 0, 0, 1 };
        };

        AssetId id = kInvalidAssetId;
        AssetState state = AssetState::Unloaded;
        std::string name{};
        std::string sourcePath{};
        std::vector<Primitive> primitives{};
    };

    struct SpriteAtlasAsset {
        struct FrameRect {
            float x = 0, y = 0, w = 0, h = 0;
            float pivotX = 0.5f, pivotY = 0.5f;
        };
        AssetId id = kInvalidAssetId;
        AssetState state = AssetState::Unloaded;
        AssetHandle<TextureAsset> texture{};
        std::vector<FrameRect> frames{};
    };

    struct SpriteAnimationClipAsset {
        AssetId id = kInvalidAssetId;
        AssetState state = AssetState::Unloaded;
        std::vector<uint32_t> frameIndices{};
        float fps = 12.0f;
        bool loop = true;
    };

    struct SkeletonAsset { AssetId id = kInvalidAssetId; AssetState state = AssetState::Unloaded; };
    struct AnimationClipAsset { AssetId id = kInvalidAssetId; AssetState state = AssetState::Unloaded; };

    class AssetRegistry {
    public:
        AssetHandle<TextureAsset> GetOrLoadTexture(const std::string& sourcePath);
        AssetHandle<ModelAsset> GetOrLoadModel(const std::string& sourcePath);

        TextureAsset* FindTexture(AssetHandle<TextureAsset> handle);
        ModelAsset* FindModel(AssetHandle<ModelAsset> handle);
        MaterialAsset* FindMaterial(AssetHandle<MaterialAsset> handle);
        MeshAsset* FindMesh(AssetHandle<MeshAsset> handle);

        const TextureAsset* FindTexture(AssetHandle<TextureAsset> handle) const;
        const ModelAsset* FindModel(AssetHandle<ModelAsset> handle) const;
        std::unordered_map<AssetId, TextureAsset> textures_{};
        std::unordered_map<AssetId, MaterialAsset> materials_{};
        std::unordered_map<AssetId, MeshAsset> meshes_{};
        std::unordered_map<AssetId, ModelAsset> models_{};

        std::unordered_map<std::string, AssetId> textureByPath_{};
        std::unordered_map<std::string, AssetId> modelByPath_{};
        AssetId AllocateId();

    private:

        AssetId nextId_ = 1;

        friend bool ImportModelStatic(AssetRegistry& registry, ModelAsset& model, const std::string& sourcePath);
    };

    bool ImportModelStatic(AssetRegistry& registry, ModelAsset& model, const std::string& sourcePath);

    AssetRegistry& GetGlobalAssetRegistry();

} // namespace HIKARI::ASSET
