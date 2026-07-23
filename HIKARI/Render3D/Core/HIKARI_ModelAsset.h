#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Animation/Assets/HIKARI_AnimationAssetTypes.h"

#include "Assets/HIKARI_AssetTypes.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_Mesh.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI {

    enum class VertexLayoutKind {
        StaticPNTT,
        SkinnedPNTTJW,
    };

    enum class AlphaMode {
        Opaque,
        Mask,
        Blend,
    };

    namespace MATERIAL_FEATURES {
        constexpr uint32_t Unlit = 1u << 0;
        constexpr uint32_t AlphaMask = 1u << 1;
        constexpr uint32_t Emissive = 1u << 2;
        constexpr uint32_t ThinTransparentSurface = 1u << 3;
        constexpr uint32_t SpecularGlossCompatibility = 1u << 4;
    }

    struct Bounds {
        MATH::Vec3 min{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 max{ 0.0f, 0.0f, 0.0f };
    };

    struct TextureAsset3D {
        std::string name;
        std::string sourcePath;
        std::string resolvedPath;
    };

    struct TextureSlot {
        int textureIndex = -1;
        int texCoord = 0;
        MATH::Vec2 uvScale{ 1.0f, 1.0f };
        MATH::Vec2 uvOffset{ 0.0f, 0.0f };
        float uvRotation = 0.0f;
        float scale = 1.0f;
        float strength = 1.0f;
    };

    struct MaterialAsset {
        std::string name;
        MATH::Vec4 baseColorFactor{ 1, 1, 1, 1 };
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        float specularFactor = 1.0f;
        MATH::Vec3 specularColorFactor{ 1.0f, 1.0f, 1.0f };
        MATH::Vec3 emissiveFactor{ 0, 0, 0 };
        float emissiveStrength = 1.0f;
        TextureSlot baseColorTexture;
        TextureSlot normalTexture;
        TextureSlot metallicRoughnessTexture;
        TextureSlot occlusionTexture;
        TextureSlot emissiveTexture;
        TextureSlot specularTexture;
        TextureSlot specularColorTexture;
        AlphaMode alphaMode = AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
        std::string shaderProfileId = "PBR";
        std::string defaultMaterialFxProfileId;
        uint32_t featureBits = 0;
    };

    namespace MATERIAL_POLICY {
        inline bool ContainsLowerAscii(std::string_view text, std::string_view needle) {
            if (needle.empty() || text.size() < needle.size()) {
                return false;
            }
            for (size_t begin = 0; begin + needle.size() <= text.size(); ++begin) {
                bool matched = true;
                for (size_t i = 0; i < needle.size(); ++i) {
                    const char c = TEXT::ToLowerAscii(text[begin + i]);
                    if (c != needle[i]) {
                        matched = false;
                        break;
                    }
                }
                if (matched) {
                    return true;
                }
            }
            return false;
        }

        inline bool HasThinTransparentCue(std::string_view text) {
            return
                ContainsLowerAscii(text, "glass") ||
                ContainsLowerAscii(text, "window") ||
                ContainsLowerAscii(text, "pane") ||
                ContainsLowerAscii(text, "fenetre") ||
                ContainsLowerAscii(text, "fenster") ||
                ContainsLowerAscii(text, "vitre") ||
                ContainsLowerAscii(text, "headlight") ||
                ContainsLowerAscii(text, "taillight") ||
                ContainsLowerAscii(text, "lightbulb") ||
                ContainsLowerAscii(text, "light_bulb") ||
                ContainsLowerAscii(text, "foliage") ||
                ContainsLowerAscii(text, "leaf") ||
                ContainsLowerAscii(text, "leaves") ||
                ContainsLowerAscii(text, "ivy") ||
                ContainsLowerAscii(text, "hedge") ||
                ContainsLowerAscii(text, "grass") ||
                ContainsLowerAscii(text, "flower") ||
                ContainsLowerAscii(text, "curtain") ||
                ContainsLowerAscii(text, "cloth") ||
                ContainsLowerAscii(text, "fabric") ||
                ContainsLowerAscii(text, "transparent") ||
                ContainsLowerAscii(text, "translucent");
        }

        inline bool HasExplicitAlphaSurface(const MaterialAsset& material) {
            return
                material.alphaMode != AlphaMode::Opaque ||
                material.baseColorFactor.w < 0.999f ||
                (material.featureBits & MATERIAL_FEATURES::AlphaMask) != 0u;
        }

        inline bool HasThinTransparentSurfaceHint(const MaterialAsset& material) {
            return
                (material.featureBits & MATERIAL_FEATURES::ThinTransparentSurface) != 0u ||
                HasThinTransparentCue(material.name);
        }

        inline bool HasAlphaMaskedSurface(const MaterialAsset& material) {
            return
                material.alphaMode == AlphaMode::Mask ||
                (material.featureBits & MATERIAL_FEATURES::AlphaMask) != 0u;
        }

        inline bool HasBlendedSurface(const MaterialAsset& material) {
            return material.alphaMode == AlphaMode::Blend;
        }

        inline bool HasExplicitThinTransparentSurface(const MaterialAsset& material) {
            return (material.featureBits & MATERIAL_FEATURES::ThinTransparentSurface) != 0u;
        }

        inline bool ShouldRenderDoubleSided(const MaterialAsset& material) {
            if (!material.doubleSided) {
                return false;
            }

            if (HasBlendedSurface(material)) {
                return true;
            }

            if (HasAlphaMaskedSurface(material)) {
                return HasThinTransparentSurfaceHint(material);
            }

            return HasExplicitThinTransparentSurface(material);
        }

    } // namespace MATERIAL_POLICY

    struct Vertex3D {
        MATH::Vec3 position{};
        MATH::Vec3 normal{};
        MATH::Vec4 tangent{};
        MATH::Vec2 uv0{};
        MATH::Vec2 uv1{};
        MATH::Vec4 color0{ 1, 1, 1, 1 };
    };

    struct SkinnedVertex3D {
        MATH::Vec3 position{};
        MATH::Vec3 normal{};
        MATH::Vec4 tangent{};
        MATH::Vec2 uv0{};
        MATH::Vec2 uv1{};
        MATH::Vec4 color0{ 1, 1, 1, 1 };
        uint16_t joints[4]{};
        float weights[4]{};
    };

    struct MeshPrimitive {
        std::string name;
        VertexLayoutKind layout = VertexLayoutKind::StaticPNTT;
        std::vector<Vertex3D> staticVertices;
        std::vector<SkinnedVertex3D> skinnedVertices;
        std::vector<uint32_t> indices;
        uint32_t materialIndex = 0;
        Bounds bounds{};
        bool hasMorphTargets = false;
    };

    namespace SURFACE_POLICY {
        inline bool HasThinSurfaceCue(std::string_view text) {
            return
                MATERIAL_POLICY::HasThinTransparentCue(text) ||
                MATERIAL_POLICY::ContainsLowerAscii(text, "fence") ||
                MATERIAL_POLICY::ContainsLowerAscii(text, "grate") ||
                MATERIAL_POLICY::ContainsLowerAscii(text, "grille") ||
                MATERIAL_POLICY::ContainsLowerAscii(text, "leaf") ||
                MATERIAL_POLICY::ContainsLowerAscii(text, "leaves") ||
                MATERIAL_POLICY::ContainsLowerAscii(text, "foliage") ||
                MATERIAL_POLICY::ContainsLowerAscii(text, "grass") ||
                MATERIAL_POLICY::ContainsLowerAscii(text, "plant") ||
                MATERIAL_POLICY::ContainsLowerAscii(text, "curtain") ||
                MATERIAL_POLICY::ContainsLowerAscii(text, "cloth") ||
                MATERIAL_POLICY::ContainsLowerAscii(text, "fabric") ||
                MATERIAL_POLICY::ContainsLowerAscii(text, "paper") ||
                MATERIAL_POLICY::ContainsLowerAscii(text, "decal");
        }

        inline bool HasThinSurfaceMaterialCue(const MaterialAsset& material) {
            return
                MATERIAL_POLICY::HasThinTransparentSurfaceHint(material) ||
                HasThinSurfaceCue(material.name) ||
                HasThinSurfaceCue(material.shaderProfileId) ||
                HasThinSurfaceCue(material.defaultMaterialFxProfileId);
        }

        inline MATH::Vec3 BoundsExtent(const Bounds& bounds) {
            return {
                std::abs(bounds.max.x - bounds.min.x),
                std::abs(bounds.max.y - bounds.min.y),
                std::abs(bounds.max.z - bounds.min.z)
            };
        }

        inline bool IsThinPrimitivePlane(const MeshPrimitive& primitive) {
            const MATH::Vec3 extent = BoundsExtent(primitive.bounds);
            if (!MATH::IsFinite(extent)) {
                return false;
            }

            const float largest = (std::max)({ extent.x, extent.y, extent.z });
            const float smallest = (std::min)({ extent.x, extent.y, extent.z });
            const float middle = extent.x + extent.y + extent.z - largest - smallest;
            if (largest <= 1.0e-5f || middle <= 1.0e-5f) {
                return false;
            }

            const float thinRatio = smallest / largest;
            return thinRatio <= 0.025f || smallest <= middle * 0.035f;
        }

        inline bool ShouldRenderDoubleSided(
            const MaterialAsset& material,
            const MeshPrimitive& primitive) {

            if (!material.doubleSided) {
                return false;
            }

            if (MATERIAL_POLICY::HasBlendedSurface(material)) {
                return true;
            }

            if (MATERIAL_POLICY::HasAlphaMaskedSurface(material) ||
                MATERIAL_POLICY::HasExplicitThinTransparentSurface(material)) {
                return
                    HasThinSurfaceMaterialCue(material) ||
                    HasThinSurfaceCue(primitive.name) ||
                    IsThinPrimitivePlane(primitive);
            }

            return false;
        }
    } // namespace SURFACE_POLICY

    struct ModelImportDiagnostics {
        std::string sourceFormat;
        uint32_t objectCount = 0;
        uint32_t groupCount = 0;
        uint32_t triangulatedPolygonCount = 0;
        uint32_t missingNormalGeneratedCount = 0;
        uint32_t missingTangentGeneratedCount = 0;
        uint32_t unresolvedTextureCount = 0;
        uint32_t clusteredStaticPrimitiveCount = 0;
        uint32_t fallbackPrimitiveCount = 0;
        uint32_t skippedMorphPrimitiveCount = 0;
        uint32_t unsupportedPrimitiveModeCount = 0;
        uint32_t unsupportedFeatureCount = 0;
        std::vector<std::string> unsupportedExtensions{};
        std::vector<std::string> messages{};
    };

    struct MeshAsset {
        std::string name;
        std::vector<MeshPrimitive> primitives;
        Bounds bounds{};
    };

    struct ModelNode {
        std::string name;
        int parent = -1;
        std::vector<int> children;
        Transform3D localTransform;
        bool hasLocalMatrix = false;
        MATH::Mat4 localMatrix;
        MATH::Mat4 globalBindMatrix;
        int meshIndex = -1;
        int skinIndex = -1;
    };

    class ModelAsset {
    public:
        enum class State {
            Unloaded,
            Loaded,
            Failed
        };

        const std::string& GetName() const;
        const std::string& GetSourcePath() const;
        State GetState() const;

        void SetName(std::string name);
        void SetSourcePath(std::string path);
        void SetState(State state);

        bool HasSkeleton() const;
        bool HasSkinnedMesh() const;
        bool HasAnimations() const;
        size_t GetSkinCount() const;
        size_t GetAnimationCount() const;
        const SkeletonAsset* FindSkin(int skinIndex) const;
        const AnimationClip* FindAnimationClip(std::string_view name) const;
        const AnimationClip* FindAnimationClip(AnimationClipId id) const;
        const AnimationClip* GetAnimationClip(size_t index) const;
        AnimationClipId GetAnimationClipId(size_t index) const noexcept;
        float GetAnimationDuration(std::string_view name) const;

        // Legacy bridge API used by old call sites during migration.
        Mesh* GetMesh();
        const Mesh* GetMesh() const;
        Material* GetMaterial();
        const Material* GetMaterial() const;
        void SetMesh(std::unique_ptr<Mesh> mesh);
        void SetMaterial(std::unique_ptr<Material> material);

    public:
        AssetId id{};
        std::string sourcePath;
        State state = State::Unloaded;
        std::vector<ModelNode> nodes;
        std::vector<MeshAsset> meshes;
        std::vector<MaterialAsset> materials;
        std::vector<TextureAsset3D> textures;
        std::vector<SkeletonAsset> skins;
        std::vector<AnimationClip> animations;
        ModelImportDiagnostics importDiagnostics{};
        int defaultSceneRootNode = -1;
        Bounds bounds{};

    private:
        std::unique_ptr<Mesh> mesh_;
        std::unique_ptr<Material> material_;
    };

} // namespace HIKARI
