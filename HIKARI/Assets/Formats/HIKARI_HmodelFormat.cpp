#include "HIKARI_HmodelFormat.h"

#include <fstream>
#include <limits>
#include <type_traits>
#include <utility>

namespace HIKARI {

    namespace {
        constexpr uint32_t kHmodelMagic = 0x4C444D48u; // 'HMDL'
        constexpr uint32_t kHmodelVersion = 1;
        constexpr uint32_t kMaxStringBytes = 16u * 1024u * 1024u;
        constexpr uint32_t kMaxVectorCount = 16u * 1024u * 1024u;

        struct HmodelFileHeader {
            uint32_t magic = kHmodelMagic;
            uint32_t version = kHmodelVersion;
            uint32_t headerSize = sizeof(HmodelFileHeader);
            uint32_t reserved = 0;
        };

        template<class T>
        bool WritePod(std::ofstream& ofs, const T& value) {
            static_assert(std::is_trivially_copyable_v<T>);
            ofs.write(reinterpret_cast<const char*>(&value), sizeof(T));
            return ofs.good();
        }

        template<class T>
        bool ReadPod(std::ifstream& ifs, T& value) {
            static_assert(std::is_trivially_copyable_v<T>);
            ifs.read(reinterpret_cast<char*>(&value), sizeof(T));
            return ifs.good();
        }

        bool WriteBool(std::ofstream& ofs, bool value) {
            const uint8_t stored = value ? 1u : 0u;
            return WritePod(ofs, stored);
        }

        bool ReadBool(std::ifstream& ifs, bool& value) {
            uint8_t stored = 0;
            if (!ReadPod(ifs, stored)) {
                return false;
            }
            value = stored != 0;
            return true;
        }

        bool WriteString(std::ofstream& ofs, const std::string& value) {
            if (value.size() > kMaxStringBytes) {
                return false;
            }
            const uint32_t size = static_cast<uint32_t>(value.size());
            if (!WritePod(ofs, size)) {
                return false;
            }
            if (size > 0) {
                ofs.write(value.data(), static_cast<std::streamsize>(size));
            }
            return ofs.good();
        }

        bool ReadString(std::ifstream& ifs, std::string& value) {
            uint32_t size = 0;
            if (!ReadPod(ifs, size) || size > kMaxStringBytes) {
                return false;
            }
            value.resize(size);
            if (size > 0) {
                ifs.read(value.data(), static_cast<std::streamsize>(size));
            }
            return ifs.good();
        }

        template<class T>
        bool WritePodVector(std::ofstream& ofs, const std::vector<T>& values) {
            static_assert(std::is_trivially_copyable_v<T>);
            if (values.size() > kMaxVectorCount) {
                return false;
            }
            const uint32_t count = static_cast<uint32_t>(values.size());
            if (!WritePod(ofs, count)) {
                return false;
            }
            if (!values.empty()) {
                ofs.write(
                    reinterpret_cast<const char*>(values.data()),
                    static_cast<std::streamsize>(values.size() * sizeof(T)));
            }
            return ofs.good();
        }

        template<class T>
        bool ReadPodVector(std::ifstream& ifs, std::vector<T>& values) {
            static_assert(std::is_trivially_copyable_v<T>);
            uint32_t count = 0;
            if (!ReadPod(ifs, count) || count > kMaxVectorCount) {
                return false;
            }
            values.resize(count);
            if (!values.empty()) {
                ifs.read(
                    reinterpret_cast<char*>(values.data()),
                    static_cast<std::streamsize>(values.size() * sizeof(T)));
            }
            return ifs.good();
        }

        bool WriteTextureSlot(std::ofstream& ofs, const TextureSlot& slot) {
            return WritePod(ofs, slot.textureIndex) &&
                WritePod(ofs, slot.texCoord) &&
                WritePod(ofs, slot.scale) &&
                WritePod(ofs, slot.strength);
        }

        bool ReadTextureSlot(std::ifstream& ifs, TextureSlot& slot) {
            return ReadPod(ifs, slot.textureIndex) &&
                ReadPod(ifs, slot.texCoord) &&
                ReadPod(ifs, slot.scale) &&
                ReadPod(ifs, slot.strength);
        }

        bool WriteMaterial(std::ofstream& ofs, const MaterialAsset& material) {
            return WriteString(ofs, material.name) &&
                WritePod(ofs, material.baseColorFactor) &&
                WritePod(ofs, material.metallicFactor) &&
                WritePod(ofs, material.roughnessFactor) &&
                WritePod(ofs, material.emissiveFactor) &&
                WritePod(ofs, material.emissiveStrength) &&
                WriteTextureSlot(ofs, material.baseColorTexture) &&
                WriteTextureSlot(ofs, material.normalTexture) &&
                WriteTextureSlot(ofs, material.metallicRoughnessTexture) &&
                WriteTextureSlot(ofs, material.occlusionTexture) &&
                WriteTextureSlot(ofs, material.emissiveTexture) &&
                WritePod(ofs, material.alphaMode) &&
                WritePod(ofs, material.alphaCutoff) &&
                WriteBool(ofs, material.doubleSided) &&
                WriteString(ofs, material.shaderProfileId) &&
                WriteString(ofs, material.defaultMaterialFxProfileId) &&
                WritePod(ofs, material.featureBits);
        }

        bool ReadMaterial(std::ifstream& ifs, MaterialAsset& material) {
            return ReadString(ifs, material.name) &&
                ReadPod(ifs, material.baseColorFactor) &&
                ReadPod(ifs, material.metallicFactor) &&
                ReadPod(ifs, material.roughnessFactor) &&
                ReadPod(ifs, material.emissiveFactor) &&
                ReadPod(ifs, material.emissiveStrength) &&
                ReadTextureSlot(ifs, material.baseColorTexture) &&
                ReadTextureSlot(ifs, material.normalTexture) &&
                ReadTextureSlot(ifs, material.metallicRoughnessTexture) &&
                ReadTextureSlot(ifs, material.occlusionTexture) &&
                ReadTextureSlot(ifs, material.emissiveTexture) &&
                ReadPod(ifs, material.alphaMode) &&
                ReadPod(ifs, material.alphaCutoff) &&
                ReadBool(ifs, material.doubleSided) &&
                ReadString(ifs, material.shaderProfileId) &&
                ReadString(ifs, material.defaultMaterialFxProfileId) &&
                ReadPod(ifs, material.featureBits);
        }

        bool WritePrimitive(std::ofstream& ofs, const MeshPrimitive& primitive) {
            return WriteString(ofs, primitive.name) &&
                WritePod(ofs, primitive.layout) &&
                WritePodVector(ofs, primitive.staticVertices) &&
                WritePodVector(ofs, primitive.skinnedVertices) &&
                WritePodVector(ofs, primitive.indices) &&
                WritePod(ofs, primitive.materialIndex) &&
                WritePod(ofs, primitive.bounds);
        }

        bool ReadPrimitive(std::ifstream& ifs, MeshPrimitive& primitive) {
            return ReadString(ifs, primitive.name) &&
                ReadPod(ifs, primitive.layout) &&
                ReadPodVector(ifs, primitive.staticVertices) &&
                ReadPodVector(ifs, primitive.skinnedVertices) &&
                ReadPodVector(ifs, primitive.indices) &&
                ReadPod(ifs, primitive.materialIndex) &&
                ReadPod(ifs, primitive.bounds);
        }

        bool WriteMesh(std::ofstream& ofs, const MeshAsset& mesh) {
            if (mesh.primitives.size() > kMaxVectorCount) {
                return false;
            }
            if (!WriteString(ofs, mesh.name) || !WritePod(ofs, mesh.bounds)) {
                return false;
            }
            const uint32_t primitiveCount = static_cast<uint32_t>(mesh.primitives.size());
            if (!WritePod(ofs, primitiveCount)) {
                return false;
            }
            for (const MeshPrimitive& primitive : mesh.primitives) {
                if (!WritePrimitive(ofs, primitive)) {
                    return false;
                }
            }
            return true;
        }

        bool ReadMesh(std::ifstream& ifs, MeshAsset& mesh) {
            uint32_t primitiveCount = 0;
            if (!ReadString(ifs, mesh.name) ||
                !ReadPod(ifs, mesh.bounds) ||
                !ReadPod(ifs, primitiveCount) ||
                primitiveCount > kMaxVectorCount) {
                return false;
            }
            mesh.primitives.resize(primitiveCount);
            for (MeshPrimitive& primitive : mesh.primitives) {
                if (!ReadPrimitive(ifs, primitive)) {
                    return false;
                }
            }
            return true;
        }

        bool WriteNode(std::ofstream& ofs, const ModelNode& node) {
            return WriteString(ofs, node.name) &&
                WritePod(ofs, node.parent) &&
                WritePodVector(ofs, node.children) &&
                WritePod(ofs, node.localTransform.position) &&
                WritePod(ofs, node.localTransform.rotation) &&
                WritePod(ofs, node.localTransform.scale) &&
                WriteBool(ofs, node.hasLocalMatrix) &&
                WritePod(ofs, node.localMatrix) &&
                WritePod(ofs, node.globalBindMatrix) &&
                WritePod(ofs, node.meshIndex) &&
                WritePod(ofs, node.skinIndex);
        }

        bool ReadNode(std::ifstream& ifs, ModelNode& node) {
            return ReadString(ifs, node.name) &&
                ReadPod(ifs, node.parent) &&
                ReadPodVector(ifs, node.children) &&
                ReadPod(ifs, node.localTransform.position) &&
                ReadPod(ifs, node.localTransform.rotation) &&
                ReadPod(ifs, node.localTransform.scale) &&
                ReadBool(ifs, node.hasLocalMatrix) &&
                ReadPod(ifs, node.localMatrix) &&
                ReadPod(ifs, node.globalBindMatrix) &&
                ReadPod(ifs, node.meshIndex) &&
                ReadPod(ifs, node.skinIndex);
        }

        bool WriteSkin(std::ofstream& ofs, const SkeletonAsset& skin) {
            if (skin.joints.size() > kMaxVectorCount) {
                return false;
            }
            if (!WriteString(ofs, skin.name) || !WritePod(ofs, skin.skeletonRootNode)) {
                return false;
            }
            const uint32_t jointCount = static_cast<uint32_t>(skin.joints.size());
            if (!WritePod(ofs, jointCount)) {
                return false;
            }
            for (const SkeletonJoint& joint : skin.joints) {
                if (!WriteString(ofs, joint.name) ||
                    !WritePod(ofs, joint.nodeIndex) ||
                    !WritePod(ofs, joint.parentJoint) ||
                    !WritePod(ofs, joint.inverseBindMatrix)) {
                    return false;
                }
            }
            return true;
        }

        bool ReadSkin(std::ifstream& ifs, SkeletonAsset& skin) {
            uint32_t jointCount = 0;
            if (!ReadString(ifs, skin.name) ||
                !ReadPod(ifs, skin.skeletonRootNode) ||
                !ReadPod(ifs, jointCount) ||
                jointCount > kMaxVectorCount) {
                return false;
            }
            skin.joints.resize(jointCount);
            for (SkeletonJoint& joint : skin.joints) {
                if (!ReadString(ifs, joint.name) ||
                    !ReadPod(ifs, joint.nodeIndex) ||
                    !ReadPod(ifs, joint.parentJoint) ||
                    !ReadPod(ifs, joint.inverseBindMatrix)) {
                    return false;
                }
            }
            return true;
        }

        bool WriteAnimation(std::ofstream& ofs, const AnimationClip& clip) {
            if (clip.channels.size() > kMaxVectorCount) {
                return false;
            }
            if (!WriteString(ofs, clip.name) || !WritePod(ofs, clip.durationSec)) {
                return false;
            }
            const uint32_t channelCount = static_cast<uint32_t>(clip.channels.size());
            if (!WritePod(ofs, channelCount)) {
                return false;
            }
            for (const NodeAnimationChannel& channel : clip.channels) {
                if (!WritePod(ofs, channel.targetNode) ||
                    !WritePod(ofs, channel.path) ||
                    !WritePod(ofs, channel.interpolation) ||
                    !WritePodVector(ofs, channel.vec3Keys) ||
                    !WritePodVector(ofs, channel.quatKeys)) {
                    return false;
                }
            }
            return true;
        }

        bool ReadAnimation(std::ifstream& ifs, AnimationClip& clip) {
            uint32_t channelCount = 0;
            if (!ReadString(ifs, clip.name) ||
                !ReadPod(ifs, clip.durationSec) ||
                !ReadPod(ifs, channelCount) ||
                channelCount > kMaxVectorCount) {
                return false;
            }
            clip.channels.resize(channelCount);
            for (NodeAnimationChannel& channel : clip.channels) {
                if (!ReadPod(ifs, channel.targetNode) ||
                    !ReadPod(ifs, channel.path) ||
                    !ReadPod(ifs, channel.interpolation) ||
                    !ReadPodVector(ifs, channel.vec3Keys) ||
                    !ReadPodVector(ifs, channel.quatKeys)) {
                    return false;
                }
            }
            return true;
        }

        template<class T, class WriteFn>
        bool WriteObjectVector(std::ofstream& ofs, const std::vector<T>& values, WriteFn writeFn) {
            if (values.size() > kMaxVectorCount) {
                return false;
            }
            const uint32_t count = static_cast<uint32_t>(values.size());
            if (!WritePod(ofs, count)) {
                return false;
            }
            for (const T& value : values) {
                if (!writeFn(ofs, value)) {
                    return false;
                }
            }
            return true;
        }

        template<class T, class ReadFn>
        bool ReadObjectVector(std::ifstream& ifs, std::vector<T>& values, ReadFn readFn) {
            uint32_t count = 0;
            if (!ReadPod(ifs, count) || count > kMaxVectorCount) {
                return false;
            }
            values.resize(count);
            for (T& value : values) {
                if (!readFn(ifs, value)) {
                    return false;
                }
            }
            return true;
        }
    }

    bool WriteHmodelFile(
        const std::filesystem::path& path,
        const ModelAsset& model,
        std::string& outMessage) {

        if (model.meshes.empty()) {
            outMessage = "[HMODEL] invalid model data";
            return false;
        }

        std::error_code ec{};
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            outMessage = "[HMODEL] failed to create output directory: " + ec.message();
            return false;
        }

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open()) {
            outMessage = "[HMODEL] failed to open for write: " + path.generic_string();
            return false;
        }

        const HmodelFileHeader header{};
        if (!WritePod(ofs, header) ||
            !WriteString(ofs, model.id.value) ||
            !WriteString(ofs, model.sourcePath) ||
            !WritePod(ofs, model.defaultSceneRootNode) ||
            !WritePod(ofs, model.bounds)) {
            outMessage = "[HMODEL] failed while writing header payload: " + path.generic_string();
            return false;
        }

        // CPU 側のモデル構造をそのまま保存し、実行時に GPU リソースへ変換する。
        const bool ok =
            WriteObjectVector(ofs, model.nodes, WriteNode) &&
            WriteObjectVector(ofs, model.meshes, WriteMesh) &&
            WriteObjectVector(ofs, model.materials, WriteMaterial) &&
            WriteObjectVector(ofs, model.textures, [](std::ofstream& stream, const TextureAsset3D& texture) {
                return WriteString(stream, texture.name) && WriteString(stream, texture.sourcePath);
            }) &&
            WriteObjectVector(ofs, model.skins, WriteSkin) &&
            WriteObjectVector(ofs, model.animations, WriteAnimation);

        if (!ok || !ofs.good()) {
            outMessage = "[HMODEL] failed while writing: " + path.generic_string();
            return false;
        }

        outMessage = "[HMODEL] wrote " + path.generic_string();
        return true;
    }

    bool ReadHmodelFile(
        const std::filesystem::path& path,
        ModelAsset& outModel,
        std::string& outMessage) {

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            outMessage = "[HMODEL] failed to open: " + path.generic_string();
            return false;
        }

        HmodelFileHeader header{};
        if (!ReadPod(ifs, header) ||
            header.magic != kHmodelMagic ||
            header.version != kHmodelVersion ||
            header.headerSize != sizeof(HmodelFileHeader)) {
            outMessage = "[HMODEL] invalid or unsupported file: " + path.generic_string();
            return false;
        }

        ModelAsset model{};
        if (!ReadString(ifs, model.id.value) ||
            !ReadString(ifs, model.sourcePath) ||
            !ReadPod(ifs, model.defaultSceneRootNode) ||
            !ReadPod(ifs, model.bounds)) {
            outMessage = "[HMODEL] failed to read header payload: " + path.generic_string();
            return false;
        }

        const bool ok =
            ReadObjectVector(ifs, model.nodes, ReadNode) &&
            ReadObjectVector(ifs, model.meshes, ReadMesh) &&
            ReadObjectVector(ifs, model.materials, ReadMaterial) &&
            ReadObjectVector(ifs, model.textures, [](std::ifstream& stream, TextureAsset3D& texture) {
                return ReadString(stream, texture.name) && ReadString(stream, texture.sourcePath);
            }) &&
            ReadObjectVector(ifs, model.skins, ReadSkin) &&
            ReadObjectVector(ifs, model.animations, ReadAnimation);

        if (!ok || !ifs.good()) {
            outMessage = "[HMODEL] failed while reading: " + path.generic_string();
            return false;
        }

        outModel = std::move(model);
        outMessage = "[HMODEL] read " + path.generic_string();
        return true;
    }

} // namespace HIKARI
