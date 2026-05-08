#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetTypes.h"
#include "Render3D/HIKARI_Material.h"
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

    struct Bounds {
        MATH::Vec3 min{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 max{ 0.0f, 0.0f, 0.0f };
    };

    struct TextureAsset3D {
        std::string name;
        std::string sourcePath;
    };

    struct TextureSlot {
        int textureIndex = -1;
        int texCoord = 0;
        float scale = 1.0f;
        float strength = 1.0f;
    };

    struct MaterialAsset {
        std::string name;
        MATH::Vec4 baseColorFactor{ 1, 1, 1, 1 };
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        MATH::Vec3 emissiveFactor{ 0, 0, 0 };
        float emissiveStrength = 1.0f;
        TextureSlot baseColorTexture;
        TextureSlot normalTexture;
        TextureSlot metallicRoughnessTexture;
        TextureSlot occlusionTexture;
        TextureSlot emissiveTexture;
        AlphaMode alphaMode = AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
        std::string shaderProfileId = "PBR";
        std::string defaultMaterialFxProfileId;
        uint32_t featureBits = 0;
    };

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

    struct SkeletonJoint {
        std::string name;
        int nodeIndex = -1;
        int parentJoint = -1;
        MATH::Mat4 inverseBindMatrix;
    };

    struct SkeletonAsset {
        std::string name;
        int skeletonRootNode = -1;
        std::vector<SkeletonJoint> joints;
    };

    enum class AnimationTargetPath {
        Translation,
        Rotation,
        Scale,
        Weights,
    };

    enum class AnimationInterpolation {
        Step,
        Linear,
        CubicSpline,
    };

    template<class T>
    struct AnimationKeyframe {
        float timeSec = 0.0f;
        T value{};
        T inTangent{};
        T outTangent{};
    };

    struct NodeAnimationChannel {
        int targetNode = -1;
        AnimationTargetPath path = AnimationTargetPath::Translation;
        AnimationInterpolation interpolation = AnimationInterpolation::Linear;
        std::vector<AnimationKeyframe<MATH::Vec3>> vec3Keys;
        std::vector<AnimationKeyframe<MATH::Quat>> quatKeys;
    };

    struct AnimationClip {
        std::string name;
        float durationSec = 0.0f;
        std::vector<NodeAnimationChannel> channels;
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
        const AnimationClip* GetAnimationClip(size_t index) const;
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
        int defaultSceneRootNode = -1;
        Bounds bounds{};

    private:
        std::unique_ptr<Mesh> mesh_;
        std::unique_ptr<Material> material_;
    };

} // namespace HIKARI
