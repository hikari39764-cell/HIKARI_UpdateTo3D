#pragma once

#include <cstdint>
#include <string>

namespace HIKARI {

    enum class AssetType {
        Model,
        Sky,
        Texture,
        Material,
        Animation,
        Particle,
        VfxEffect,
        PostProfile,
        Unknown
    };

    struct AssetId {
        std::string value;

        bool operator==(const AssetId& rhs) const {
            return value == rhs.value;
        }

        bool operator!=(const AssetId& rhs) const {
            return !(*this == rhs);
        }
    };

    struct AssetDescriptor {
        virtual ~AssetDescriptor() = default;

        AssetId id{};
        AssetType type = AssetType::Unknown;
        std::string sourcePath{};
        uint32_t version = 1;
    };

    struct ModelAssetDescriptor final : AssetDescriptor {
        bool forceFlatNormals = false;
    };

    struct SkyAssetDescriptor final : AssetDescriptor {
        std::string meshAssetId{};
        std::string textureAssetId{};
    };

    struct TextureAssetDescriptor final : AssetDescriptor {
    };

    struct VfxAssetDescriptor final : AssetDescriptor {
        bool preload = true;
        bool loopByDefault = false;
        float defaultScale = 1.0f;
    };

    struct PostProfileAssetDescriptor final : AssetDescriptor {
    };

} // namespace HIKARI
