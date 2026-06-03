#include "Render3D/HIKARI_ModelManager.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>
#include <json.hpp>
#include "Assets/Formats/HIKARI_HmodelFormat.h"
#include "Core/HIKARI_Logger.h"
#include "HIKARI_DxTexture.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/HIKARI_Material.h"
#include "HIKARI_Services.h"

namespace HIKARI {

    namespace {
        using nlohmann::json;

        struct ObjKey {
            int pos = -1;
            int uv = -1;
            int normal = -1;

            bool operator==(const ObjKey& rhs) const {
                return pos == rhs.pos && uv == rhs.uv && normal == rhs.normal;
            }
        };

        struct ObjKeyHash {
            size_t operator()(const ObjKey& key) const {
                const size_t h1 = std::hash<int>{}(key.pos);
                const size_t h2 = std::hash<int>{}(key.uv);
                const size_t h3 = std::hash<int>{}(key.normal);
                return h1 ^ (h2 << 1) ^ (h3 << 2);
            }
        };

        struct ObjMaterialInfo {
            std::string name;
            MATH::Vec4 baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
            std::string baseColorMapPath;
            std::string normalMapPath;
            std::string roughnessMapPath;
            float metallic = 0.0f;
            float roughness = 1.0f;
            float alpha = 1.0f;
            bool hasAlpha = false;
        };

        const char* ToModelTextureUsageText(ModelTextureUsage usage) {
            switch (usage) {
            case ModelTextureUsage::BaseColor: return "BaseColor";
            case ModelTextureUsage::Normal: return "Normal";
            case ModelTextureUsage::MetallicRoughness: return "MetallicRoughness";
            case ModelTextureUsage::Occlusion: return "Occlusion";
            case ModelTextureUsage::Emissive: return "Emissive";
            default: return "Unknown";
            }
        }

        bool IsHtexPath(const std::string& path) {
            std::string ext = std::filesystem::path(path).extension().string();
            for (char& c : ext) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return ext == ".htex";
        }

        DXTEX::TextureColorSpace ColorSpaceForUsage(ModelTextureUsage usage) {
            switch (usage) {
            case ModelTextureUsage::BaseColor:
            case ModelTextureUsage::Emissive:
                return DXTEX::TextureColorSpace::Srgb;
            case ModelTextureUsage::Normal:
            case ModelTextureUsage::MetallicRoughness:
            case ModelTextureUsage::Occlusion:
            default:
                return DXTEX::TextureColorSpace::Linear;
            }
        }

        void ReleaseTextureHandleOnce(int handle, std::vector<int>& releasedHandles) {
            if (handle < 0) {
                return;
            }
            if (std::find(releasedHandles.begin(), releasedHandles.end(), handle) != releasedHandles.end()) {
                return;
            }

            DXTEX::DxTextureManager::ReleaseTextureDeferred(handle);
            releasedHandles.push_back(handle);
        }

        void ReleaseRuntimeMaterialTextures(const Material* material) {
            if (!material) {
                return;
            }

            std::vector<int> releasedHandles{};
            // Model reload 時に古い material slot の GPU handle を deferred release へ渡す。
            ReleaseTextureHandleOnce(material->GetTextureSlot(ModelTextureUsage::BaseColor).handle, releasedHandles);
            ReleaseTextureHandleOnce(material->GetTextureSlot(ModelTextureUsage::Normal).handle, releasedHandles);
            ReleaseTextureHandleOnce(material->GetTextureSlot(ModelTextureUsage::MetallicRoughness).handle, releasedHandles);
            ReleaseTextureHandleOnce(material->GetTextureSlot(ModelTextureUsage::Occlusion).handle, releasedHandles);
            ReleaseTextureHandleOnce(material->GetTextureSlot(ModelTextureUsage::Emissive).handle, releasedHandles);
        }

        const TextureAsset3D* FindTextureBySlot(const ModelAsset& asset, const TextureSlot& slot) {
            if (slot.textureIndex < 0 || slot.textureIndex >= static_cast<int>(asset.textures.size())) {
                return nullptr;
            }
            return &asset.textures[static_cast<size_t>(slot.textureIndex)];
        }

        TextureAsset3D* FindTextureBySlot(ModelAsset& asset, const TextureSlot& slot) {
            if (slot.textureIndex < 0 || slot.textureIndex >= static_cast<int>(asset.textures.size())) {
                return nullptr;
            }
            return &asset.textures[static_cast<size_t>(slot.textureIndex)];
        }

        const char* GetFileExt(const std::string& path) {
            const size_t dot = path.find_last_of('.');
            if (dot == std::string::npos) {
                return "";
            }
            return path.c_str() + dot;
        }

        std::string Trim(const std::string& value) {
            size_t first = 0;
            while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
                ++first;
            }
            size_t last = value.size();
            while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
                --last;
            }
            return value.substr(first, last - first);
        }

        std::string NormalizePathString(const std::filesystem::path& path) {
            return path.lexically_normal().generic_string();
        }

        int DecodeBase64Value(char c) {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        }

        bool DecodeBase64(std::string_view input, std::vector<uint8_t>& out) {
            out.clear();
            int value = 0;
            int bits = -8;
            for (char c : input) {
                if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                    continue;
                }
                if (c == '=') {
                    break;
                }
                const int decoded = DecodeBase64Value(c);
                if (decoded < 0) {
                    return false;
                }
                value = (value << 6) | decoded;
                bits += 6;
                if (bits >= 0) {
                    out.push_back(static_cast<uint8_t>((value >> bits) & 0xFF));
                    bits -= 8;
                }
            }
            return true;
        }

        bool DecodeDataUriBytes(const std::string& uri, std::vector<uint8_t>& out) {
            const size_t comma = uri.find(',');
            if (comma == std::string::npos) {
                return false;
            }
            const std::string header = uri.substr(0, comma);
            if (header.find(";base64") == std::string::npos) {
                return false;
            }
            return DecodeBase64(std::string_view(uri).substr(comma + 1u), out);
        }

        bool MaterialHasNormalTexture(const ModelAsset& asset, uint32_t materialIndex) {
            return materialIndex < asset.materials.size() &&
                asset.materials[materialIndex].normalTexture.textureIndex >= 0;
        }

        void GenerateStaticPrimitiveNormals(MeshPrimitive& primitive) {
            for (Vertex3D& vertex : primitive.staticVertices) {
                vertex.normal = {};
            }
            for (size_t i = 0; i + 2u < primitive.indices.size(); i += 3u) {
                const uint32_t i0 = primitive.indices[i + 0u];
                const uint32_t i1 = primitive.indices[i + 1u];
                const uint32_t i2 = primitive.indices[i + 2u];
                if (i0 >= primitive.staticVertices.size() ||
                    i1 >= primitive.staticVertices.size() ||
                    i2 >= primitive.staticVertices.size()) {
                    continue;
                }

                const MATH::Vec3 p0 = primitive.staticVertices[i0].position;
                const MATH::Vec3 p1 = primitive.staticVertices[i1].position;
                const MATH::Vec3 p2 = primitive.staticVertices[i2].position;
                const MATH::Vec3 normal = MATH::Normalize(MATH::Cross(p1 - p0, p2 - p0));
                if (MATH::Length(normal) <= 1.0e-6f) {
                    continue;
                }
                primitive.staticVertices[i0].normal = primitive.staticVertices[i0].normal + normal;
                primitive.staticVertices[i1].normal = primitive.staticVertices[i1].normal + normal;
                primitive.staticVertices[i2].normal = primitive.staticVertices[i2].normal + normal;
            }

            for (Vertex3D& vertex : primitive.staticVertices) {
                vertex.normal = MATH::Normalize(vertex.normal);
                if (MATH::Length(vertex.normal) <= 1.0e-6f) {
                    vertex.normal = { 0.0f, 1.0f, 0.0f };
                }
            }
        }

        void GenerateStaticPrimitiveTangents(MeshPrimitive& primitive) {
            std::vector<MATH::Vec3> accum(primitive.staticVertices.size());
            for (size_t i = 0; i + 2u < primitive.indices.size(); i += 3u) {
                const uint32_t i0 = primitive.indices[i + 0u];
                const uint32_t i1 = primitive.indices[i + 1u];
                const uint32_t i2 = primitive.indices[i + 2u];
                if (i0 >= primitive.staticVertices.size() ||
                    i1 >= primitive.staticVertices.size() ||
                    i2 >= primitive.staticVertices.size()) {
                    continue;
                }

                const Vertex3D& v0 = primitive.staticVertices[i0];
                const Vertex3D& v1 = primitive.staticVertices[i1];
                const Vertex3D& v2 = primitive.staticVertices[i2];
                const MATH::Vec3 e1 = v1.position - v0.position;
                const MATH::Vec3 e2 = v2.position - v0.position;
                const MATH::Vec2 duv1 = v1.uv0 - v0.uv0;
                const MATH::Vec2 duv2 = v2.uv0 - v0.uv0;
                const float denom = duv1.x * duv2.y - duv1.y * duv2.x;
                if (std::fabs(denom) <= 1.0e-8f) {
                    continue;
                }
                const float inv = 1.0f / denom;
                const MATH::Vec3 tangent = (e1 * duv2.y - e2 * duv1.y) * inv;
                accum[i0] = accum[i0] + tangent;
                accum[i1] = accum[i1] + tangent;
                accum[i2] = accum[i2] + tangent;
            }

            for (size_t i = 0; i < primitive.staticVertices.size(); ++i) {
                MATH::Vec3 tangent = MATH::Normalize(accum[i]);
                if (MATH::Length(tangent) <= 1.0e-6f) {
                    tangent = { 1.0f, 0.0f, 0.0f };
                }
                primitive.staticVertices[i].tangent = { tangent.x, tangent.y, tangent.z, 1.0f };
            }
        }

        MATH::Mat4 ReadGltfNodeMatrix(const json& matrixNode) {
            MATH::Mat4 out = MATH::Mat4::Identity();
            // glTF stores node.matrix in column-major order.
            // Convert it into HIKARI Mat4 layout here.
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    out.m[col][row] = matrixNode[static_cast<size_t>(col * 4 + row)].get<float>();
                }
            }
            return out;
        }

        void ReadGltfNodes(const json& root, ModelAsset& asset) {
            if (!root.contains("nodes") || !root["nodes"].is_array()) {
                return;
            }

            const json& nodes = root["nodes"];
            asset.nodes.resize(nodes.size());
            for (size_t i = 0; i < nodes.size(); ++i) {
                const json& node = nodes[i];
                ModelNode modelNode{};
                modelNode.name = node.value("name", "");
                modelNode.localMatrix = MATH::Mat4::Identity();

                if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() >= 16) {
                    modelNode.hasLocalMatrix = true;
                    modelNode.localMatrix = ReadGltfNodeMatrix(node["matrix"]);
                } else {
                    if (node.contains("translation") && node["translation"].is_array() && node["translation"].size() >= 3) {
                        modelNode.localTransform.position = {
                            node["translation"][0].get<float>(),
                            node["translation"][1].get<float>(),
                            node["translation"][2].get<float>()
                        };
                    }
                    if (node.contains("scale") && node["scale"].is_array() && node["scale"].size() >= 3) {
                        modelNode.localTransform.scale = {
                            node["scale"][0].get<float>(),
                            node["scale"][1].get<float>(),
                            node["scale"][2].get<float>()
                        };
                    }
                    if (node.contains("rotation") && node["rotation"].is_array() && node["rotation"].size() >= 4) {
                        modelNode.localTransform.rotation = {
                            node["rotation"][0].get<float>(),
                            node["rotation"][1].get<float>(),
                            node["rotation"][2].get<float>(),
                            node["rotation"][3].get<float>()
                        };
                    }
                }

                modelNode.meshIndex = node.value("mesh", -1);
                modelNode.skinIndex = node.value("skin", -1);
                asset.nodes[i] = std::move(modelNode);
            }

            for (size_t i = 0; i < nodes.size(); ++i) {
                const json& node = nodes[i];
                if (!node.contains("children") || !node["children"].is_array()) {
                    continue;
                }
                for (const auto& child : node["children"]) {
                    const int childIndex = child.get<int>();
                    if (childIndex >= 0 && childIndex < static_cast<int>(asset.nodes.size())) {
                        asset.nodes[i].children.push_back(childIndex);
                        asset.nodes[static_cast<size_t>(childIndex)].parent = static_cast<int>(i);
                    }
                }
            }
        }

        bool ReadAccessorMat4Array(
            int accessorIndex,
            const json& accessors,
            const json& bufferViews,
            const std::vector<std::vector<uint8_t>>& loadedBuffers,
            std::vector<MATH::Mat4>& out) {
            out.clear();
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) {
                return false;
            }

            const json& accessor = accessors[static_cast<size_t>(accessorIndex)];
            if (accessor.value("componentType", 0) != 5126 || accessor.value("type", "") != "MAT4") {
                return false;
            }

            const int count = accessor.value("count", 0);
            if (count <= 0) {
                return false;
            }

            const int bufferViewIndex = accessor.value("bufferView", -1);
            if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) {
                return false;
            }

            const json& view = bufferViews[static_cast<size_t>(bufferViewIndex)];
            const int bufferIndex = view.value("buffer", -1);
            if (bufferIndex < 0 || bufferIndex >= static_cast<int>(loadedBuffers.size())) {
                return false;
            }

            constexpr size_t kMat4ByteSize = sizeof(float) * 16u;
            const size_t accessorOffset = static_cast<size_t>(accessor.value("byteOffset", 0));
            const size_t viewOffset = static_cast<size_t>(view.value("byteOffset", 0));
            const size_t stride = static_cast<size_t>(view.value("byteStride", static_cast<int>(kMat4ByteSize)));
            if (stride < kMat4ByteSize) {
                return false;
            }

            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];
            out.resize(static_cast<size_t>(count), MATH::Mat4::Identity());
            for (int i = 0; i < count; ++i) {
                const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                if (srcOffset + kMat4ByteSize > bufferData.size()) {
                    out.clear();
                    return false;
                }

                float values[16]{};
                std::memcpy(values, bufferData.data() + srcOffset, kMat4ByteSize);
                MATH::Mat4 mat = MATH::Mat4::Identity();
                for (int col = 0; col < 4; ++col) {
                    for (int row = 0; row < 4; ++row) {
                        mat.m[col][row] = values[col * 4 + row];
                    }
                }
                out[static_cast<size_t>(i)] = mat;
            }
            return true;
        }

        bool ReadAccessorJointVec4(
            int accessorIndex,
            const json& accessors,
            const json& bufferViews,
            const std::vector<std::vector<uint8_t>>& loadedBuffers,
            std::vector<std::array<uint16_t, 4>>& out) {
            out.clear();
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) {
                return false;
            }

            const json& accessor = accessors[static_cast<size_t>(accessorIndex)];
            if (accessor.value("type", "") != "VEC4") {
                return false;
            }

            const int componentType = accessor.value("componentType", 0);
            const size_t componentSize = (componentType == 5121) ? 1u : ((componentType == 5123) ? 2u : 0u);
            if (componentSize == 0u) {
                return false;
            }

            const int count = accessor.value("count", 0);
            if (count <= 0) {
                return false;
            }

            const int bufferViewIndex = accessor.value("bufferView", -1);
            if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) {
                return false;
            }

            const json& view = bufferViews[static_cast<size_t>(bufferViewIndex)];
            const int bufferIndex = view.value("buffer", -1);
            if (bufferIndex < 0 || bufferIndex >= static_cast<int>(loadedBuffers.size())) {
                return false;
            }

            const size_t elementSize = componentSize * 4u;
            const size_t accessorOffset = static_cast<size_t>(accessor.value("byteOffset", 0));
            const size_t viewOffset = static_cast<size_t>(view.value("byteOffset", 0));
            const size_t stride = static_cast<size_t>(view.value("byteStride", static_cast<int>(elementSize)));
            if (stride < elementSize) {
                return false;
            }

            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];
            out.resize(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                if (srcOffset + elementSize > bufferData.size()) {
                    out.clear();
                    return false;
                }

                std::array<uint16_t, 4> joints{};
                for (size_t c = 0; c < joints.size(); ++c) {
                    const size_t componentOffset = srcOffset + componentSize * c;
                    if (componentType == 5121) {
                        joints[c] = static_cast<uint16_t>(bufferData[componentOffset]);
                    } else {
                        uint16_t value = 0;
                        std::memcpy(&value, bufferData.data() + componentOffset, sizeof(uint16_t));
                        joints[c] = value;
                    }
                }
                out[static_cast<size_t>(i)] = joints;
            }
            return true;
        }

        bool ReadAccessorWeightVec4(
            int accessorIndex,
            const json& accessors,
            const json& bufferViews,
            const std::vector<std::vector<uint8_t>>& loadedBuffers,
            std::vector<std::array<float, 4>>& out) {
            out.clear();
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) {
                return false;
            }

            const json& accessor = accessors[static_cast<size_t>(accessorIndex)];
            if (accessor.value("type", "") != "VEC4") {
                return false;
            }

            const int componentType = accessor.value("componentType", 0);
            const size_t componentSize = (componentType == 5126) ? 4u : ((componentType == 5121) ? 1u : ((componentType == 5123) ? 2u : 0u));
            if (componentSize == 0u) {
                return false;
            }

            const int count = accessor.value("count", 0);
            if (count <= 0) {
                return false;
            }

            const int bufferViewIndex = accessor.value("bufferView", -1);
            if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) {
                return false;
            }

            const json& view = bufferViews[static_cast<size_t>(bufferViewIndex)];
            const int bufferIndex = view.value("buffer", -1);
            if (bufferIndex < 0 || bufferIndex >= static_cast<int>(loadedBuffers.size())) {
                return false;
            }

            const size_t elementSize = componentSize * 4u;
            const size_t accessorOffset = static_cast<size_t>(accessor.value("byteOffset", 0));
            const size_t viewOffset = static_cast<size_t>(view.value("byteOffset", 0));
            const size_t stride = static_cast<size_t>(view.value("byteStride", static_cast<int>(elementSize)));
            if (stride < elementSize) {
                return false;
            }

            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];
            out.resize(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                if (srcOffset + elementSize > bufferData.size()) {
                    out.clear();
                    return false;
                }

                std::array<float, 4> weights{};
                for (size_t c = 0; c < weights.size(); ++c) {
                    const size_t componentOffset = srcOffset + componentSize * c;
                    if (componentType == 5126) {
                        std::memcpy(&weights[c], bufferData.data() + componentOffset, sizeof(float));
                    } else if (componentType == 5121) {
                        weights[c] = static_cast<float>(bufferData[componentOffset]) / 255.0f;
                    } else {
                        uint16_t value = 0;
                        std::memcpy(&value, bufferData.data() + componentOffset, sizeof(uint16_t));
                        weights[c] = static_cast<float>(value) / 65535.0f;
                    }
                }

                const float sum = weights[0] + weights[1] + weights[2] + weights[3];
                if (sum > 0.00001f) {
                    for (float& weight : weights) {
                        weight /= sum;
                    }
                } else {
                    weights = { 1.0f, 0.0f, 0.0f, 0.0f };
                }
                out[static_cast<size_t>(i)] = weights;
            }
            return true;
        }

        void ReadGltfSkins(
            const json& root,
            const json& accessors,
            const json& bufferViews,
            const std::vector<std::vector<uint8_t>>& loadedBuffers,
            ModelAsset& asset) {
            if (!root.contains("skins") || !root["skins"].is_array()) {
                return;
            }

            for (const auto& skinNode : root["skins"]) {
                if (!skinNode.is_object() || !skinNode.contains("joints") || !skinNode["joints"].is_array()) {
                    continue;
                }

                std::vector<MATH::Mat4> inverseBindMatrices;
                const int inverseBindAccessor = skinNode.value("inverseBindMatrices", -1);
                if (inverseBindAccessor >= 0) {
                    ReadAccessorMat4Array(inverseBindAccessor, accessors, bufferViews, loadedBuffers, inverseBindMatrices);
                }

                SkeletonAsset skeleton{};
                skeleton.name = skinNode.value("name", "");
                skeleton.skeletonRootNode = skinNode.value("skeleton", -1);

                std::unordered_map<int, int> nodeToJoint;
                const json& joints = skinNode["joints"];
                for (size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex) {
                    if (!joints[jointIndex].is_number_integer()) {
                        continue;
                    }

                    const int nodeIndex = joints[jointIndex].get<int>();
                    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(asset.nodes.size())) {
                        continue;
                    }

                    SkeletonJoint joint{};
                    joint.name = asset.nodes[static_cast<size_t>(nodeIndex)].name;
                    joint.nodeIndex = nodeIndex;
                    joint.inverseBindMatrix = (jointIndex < inverseBindMatrices.size())
                        ? inverseBindMatrices[jointIndex]
                        : MATH::Mat4::Identity();

                    const int compactJointIndex = static_cast<int>(skeleton.joints.size());
                    nodeToJoint[nodeIndex] = compactJointIndex;
                    skeleton.joints.push_back(std::move(joint));
                }

                for (SkeletonJoint& joint : skeleton.joints) {
                    if (joint.nodeIndex < 0 || joint.nodeIndex >= static_cast<int>(asset.nodes.size())) {
                        continue;
                    }

                    const int parentNode = asset.nodes[static_cast<size_t>(joint.nodeIndex)].parent;
                    const auto foundParentJoint = nodeToJoint.find(parentNode);
                    joint.parentJoint = (foundParentJoint != nodeToJoint.end()) ? foundParentJoint->second : -1;
                }

                asset.skins.push_back(std::move(skeleton));
            }
        }

        bool ReadBinaryFile(const std::filesystem::path& path, std::vector<uint8_t>& out) {
            std::ifstream ifs(path, std::ios::binary | std::ios::ate);
            if (!ifs.is_open()) {
                return false;
            }
            const std::streamsize size = ifs.tellg();
            if (size <= 0) {
                out.clear();
                return true;
            }
            out.resize(static_cast<size_t>(size));
            ifs.seekg(0, std::ios::beg);
            return ifs.read(reinterpret_cast<char*>(out.data()), size).good();
        }

        std::string ExtractMtlTexturePath(std::stringstream& ss) {
            std::vector<std::string> tokens;
            std::string token;
            while (ss >> token) {
                tokens.push_back(token);
            }
            while (!tokens.empty() && !tokens.front().empty() && tokens.front()[0] == '-') {
                tokens.erase(tokens.begin());
                if (!tokens.empty()) {
                    tokens.erase(tokens.begin());
                }
            }
            if (tokens.empty()) {
                return {};
            }

            std::string path = tokens.front();
            for (size_t i = 1; i < tokens.size(); ++i) {
                path += " " + tokens[i];
            }
            return Trim(path);
        }

        float RoughnessFromNs(float ns) {
            ns = (std::max)(0.0f, ns);
            return (std::clamp)(std::sqrt(2.0f / (ns + 2.0f)), 0.04f, 1.0f);
        }

        bool ParseMtlLibrary(
            const std::filesystem::path& mtlPath,
            std::unordered_map<std::string, ObjMaterialInfo>& outMaterials) {

            outMaterials.clear();
            std::ifstream mtlFile(mtlPath);
            if (!mtlFile.is_open()) {
                return false;
            }

            ObjMaterialInfo* current = nullptr;
            std::string line;
            while (std::getline(mtlFile, line)) {
                std::stringstream ss(line);
                std::string tag;
                ss >> tag;
                if (tag.empty() || tag[0] == '#') {
                    continue;
                }

                if (tag == "newmtl") {
                    std::string materialName;
                    ss >> materialName;
                    if (materialName.empty()) {
                        current = nullptr;
                        continue;
                    }
                    ObjMaterialInfo info{};
                    info.name = materialName;
                    auto [it, _] = outMaterials.emplace(materialName, std::move(info));
                    current = &it->second;
                    continue;
                }
                if (current == nullptr) {
                    continue;
                }

                if (tag == "Kd") {
                    ss >> current->baseColor.x >> current->baseColor.y >> current->baseColor.z;
                } else if (tag == "d") {
                    ss >> current->alpha;
                    current->alpha = (std::clamp)(current->alpha, 0.0f, 1.0f);
                    current->baseColor.w = current->alpha;
                    current->hasAlpha = current->alpha < 0.999f;
                } else if (tag == "Tr") {
                    float tr = 0.0f;
                    ss >> tr;
                    current->alpha = (std::clamp)(1.0f - tr, 0.0f, 1.0f);
                    current->baseColor.w = current->alpha;
                    current->hasAlpha = current->alpha < 0.999f;
                } else if (tag == "Ns") {
                    float ns = 0.0f;
                    ss >> ns;
                    current->roughness = RoughnessFromNs(ns);
                } else if (tag == "map_Kd") {
                    const std::string texture = ExtractMtlTexturePath(ss);
                    if (!texture.empty()) {
                        current->baseColorMapPath = NormalizePathString(mtlPath.parent_path() / texture);
                    }
                } else if (tag == "map_Bump" || tag == "bump") {
                    const std::string texture = ExtractMtlTexturePath(ss);
                    if (!texture.empty()) {
                        current->normalMapPath = NormalizePathString(mtlPath.parent_path() / texture);
                    }
                } else if (tag == "map_Ns" || tag == "map_Pr") {
                    const std::string texture = ExtractMtlTexturePath(ss);
                    if (!texture.empty()) {
                        current->roughnessMapPath = NormalizePathString(mtlPath.parent_path() / texture);
                    }
                }
            }
            return !outMaterials.empty();
        }
    }

    ModelAsset* ModelManager::RegisterAsset(const std::string& name, const std::string& sourcePath) {
        if (auto found = FindAsset(name)) {
            found->SetSourcePath(sourcePath);
            found->SetState(ModelAsset::State::Unloaded);
            found->SetMesh(nullptr);
            found->SetMaterial(nullptr);
            return found;
        }

        auto asset = std::make_unique<ModelAsset>();
        asset->SetName(name);
        asset->SetSourcePath(sourcePath);
        ModelAsset* ptr = asset.get();
        assets_.push_back(std::move(asset));
        nameToAsset_[name] = ptr;
        return ptr;
    }

    ModelAsset* ModelManager::FindAsset(const std::string& name) {
        auto it = nameToAsset_.find(name);
        return (it == nameToAsset_.end()) ? nullptr : it->second;
    }

    const ModelAsset* ModelManager::FindAsset(const std::string& name) const {
        auto it = nameToAsset_.find(name);
        return (it == nameToAsset_.end()) ? nullptr : it->second;
    }

    const std::vector<std::unique_ptr<ModelAsset>>& ModelManager::GetAssets() const {
        return assets_;
    }

    bool ModelManager::MarkLoaded(const std::string& name) {
        if (auto* asset = FindAsset(name)) {
            asset->SetState(ModelAsset::State::Loaded);
            return true;
        }
        return false;
    }

    bool ModelManager::MarkFailed(const std::string& name) {
        if (auto* asset = FindAsset(name)) {
            asset->SetState(ModelAsset::State::Failed);
            return true;
        }
        return false;
    }

    bool ModelManager::LoadAssetNow(const std::string& name) {
        ModelAsset* asset = FindAsset(name);
        if (!asset) {
            return false;
        }

        const std::string sourcePath = asset->GetSourcePath();
        bool ok = false;
        if (sourcePath == "builtin:cube") {
            ok = BuildBuiltinCube(*asset);
        } else {
            std::string ext = GetFileExt(sourcePath);
            for (char& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
            if (ext == ".hmodel") {
                ok = LoadAsHmodel(*asset);
            } else if (ext == ".obj") {
                ok = LoadAsObj(*asset, true);
            } else if (ext == ".gltf") {
                ok = LoadAsGltf(*asset, true);
            }
        }

        asset->SetState(ok ? ModelAsset::State::Loaded : ModelAsset::State::Failed);
        return ok;
    }

    bool ModelManager::ReloadAssetNow(const std::string& name) {
        ModelAsset* asset = FindAsset(name);
        if (!asset) {
            return false;
        }

        // Asset entry 自体は保持し、中身だけを捨てて同じ id に再ロードする。
        UnloadAsset(name);
        return LoadAssetNow(name);
    }

    void ModelManager::UnloadAsset(const std::string& name) {
        ModelAsset* asset = FindAsset(name);
        if (!asset) {
            return;
        }

        asset->nodes.clear();
        asset->meshes.clear();
        asset->materials.clear();
        asset->textures.clear();
        asset->skins.clear();
        asset->animations.clear();
        asset->defaultSceneRootNode = -1;
        asset->bounds = {};
        ReleaseRuntimeMaterialTextures(asset->GetMaterial());
        asset->SetMesh(nullptr);
        asset->SetMaterial(nullptr);
        asset->SetState(ModelAsset::State::Unloaded);
    }

    bool ModelManager::LoadCpuAssetFromSource(ModelAsset& asset) {
        const std::string sourcePath = asset.GetSourcePath();
        std::string ext = GetFileExt(sourcePath);
        for (char& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));

        if (ext == ".hmodel") {
            std::string message{};
            return ReadHmodelFile(sourcePath, asset, message);
        }
        if (ext == ".obj") {
            return LoadAsObj(asset, false);
        }
        if (ext == ".gltf") {
            return LoadAsGltf(asset, false);
        }
        return false;
    }

    bool ModelManager::LoadAllRegisteredAssets() {
        bool allOk = true;
        for (const auto& asset : assets_) {
            allOk = LoadAssetNow(asset->GetName()) && allOk;
        }
        return allOk;
    }

    size_t ModelManager::CountLoadedAssets() const {
        size_t count = 0;
        for (const auto& asset : assets_) {
            if (asset->GetState() == ModelAsset::State::Loaded) {
                ++count;
            }
        }
        return count;
    }

    size_t ModelManager::CountFailedAssets() const {
        size_t count = 0;
        for (const auto& asset : assets_) {
            if (asset->GetState() == ModelAsset::State::Failed) {
                ++count;
            }
        }
        return count;
    }

    void ModelManager::SetTexturePathResolver(ModelTexturePathResolver resolver) {
        texturePathResolver_ = std::move(resolver);
    }

    void ModelManager::ClearTexturePathResolver() {
        texturePathResolver_ = {};
    }

    void ModelManager::ResetTextureResolveStats() {
        textureResolveStats_ = {};
    }

    void ModelManager::RecordTextureResolveFailure(ModelTextureResolveFailureKind kind) const {
        switch (kind) {
        case ModelTextureResolveFailureKind::Missing:
            ++textureResolveStats_.missing;
            break;
        case ModelTextureResolveFailureKind::Ambiguous:
            ++textureResolveStats_.ambiguous;
            break;
        default:
            break;
        }
    }

    const ModelTextureResolveStats& ModelManager::GetTextureResolveStats() const {
        return textureResolveStats_;
    }

    std::string ModelManager::ResolveTexturePath(
        const std::string& sourceTexturePath,
        ModelTextureUsage usage) const {

        if (sourceTexturePath.empty()) {
            return {};
        }

        ++textureResolveStats_.total;

        if (!texturePathResolver_) {
            ++textureResolveStats_.fallbackRaw;
            HIKARI_LOG_INFO("[ModelTextureResolver] fallback raw texture source=" +
                sourceTexturePath +
                " usage=" + ToModelTextureUsageText(usage) +
                " reason=resolver not configured");
            return sourceTexturePath;
        }

        const std::string resolvedPath = texturePathResolver_(sourceTexturePath, usage);
        if (resolvedPath.empty()) {
            ++textureResolveStats_.fallbackRaw;
            HIKARI_LOG_WARN("[ModelTextureResolver] fallback raw texture source=" +
                sourceTexturePath +
                " usage=" + ToModelTextureUsageText(usage) +
                " reason=resolver returned empty");
            return sourceTexturePath;
        }

        if (resolvedPath == sourceTexturePath) {
            if (IsHtexPath(resolvedPath)) {
                ++textureResolveStats_.resolvedHtex;
                HIKARI_LOG_INFO("[ModelTextureResolver] resolved HTEX: " +
                    sourceTexturePath +
                    " usage=" + ToModelTextureUsageText(usage));
            } else {
                ++textureResolveStats_.fallbackRaw;
                HIKARI_LOG_INFO("[ModelTextureResolver] fallback raw texture source=" +
                    sourceTexturePath +
                    " usage=" + ToModelTextureUsageText(usage));
            }
        } else if (IsHtexPath(resolvedPath)) {
            ++textureResolveStats_.resolvedHtex;
            HIKARI_LOG_INFO("[ModelTextureResolver] resolved HTEX: " +
                sourceTexturePath +
                " -> " + resolvedPath +
                " usage=" + ToModelTextureUsageText(usage));
        } else {
            ++textureResolveStats_.fallbackRaw;
            HIKARI_LOG_INFO("[ModelTextureResolver] source=" +
                sourceTexturePath +
                " usage=" + ToModelTextureUsageText(usage) +
                " resolved=" + resolvedPath);
        }

        return resolvedPath;
    }

    RuntimeTextureSlot ModelManager::ResolveAndLoadMaterialTexture(
        const ModelAsset& asset,
        const std::string& textureName,
        const TextureSlot& textureSlot,
        ModelTextureUsage usage) const {

        RuntimeTextureSlot slot{};
        const TextureAsset3D* texture = FindTextureBySlot(asset, textureSlot);
        if (texture == nullptr || texture->sourcePath.empty()) {
            return slot;
        }

        slot.sourcePath = texture->sourcePath;
        slot.resolvedPath = texture->resolvedPath.empty()
            ? ResolveTexturePath(texture->sourcePath, usage)
            : texture->resolvedPath;
        if (slot.resolvedPath.empty()) {
            slot.resolvedPath = slot.sourcePath;
        }

        slot.handle = DXTEX::DxTextureManager::LoadTextureWithColorSpace(
            textureName,
            slot.resolvedPath,
            ColorSpaceForUsage(usage));
        slot.enabled = slot.handle >= 0;
        return slot;
    }

    void ModelManager::LoadPbrTextureSlots(
        const ModelAsset& asset,
        const MaterialAsset& source,
        Material& runtimeMaterial,
        const std::string& materialNamePrefix) const {

        runtimeMaterial.SetBaseColor(source.baseColorFactor);
        runtimeMaterial.SetMetallicFactor(source.metallicFactor);
        runtimeMaterial.SetRoughnessFactor(source.roughnessFactor);
        runtimeMaterial.SetNormalScale(source.normalTexture.scale);
        runtimeMaterial.SetOcclusionStrength(source.occlusionTexture.strength);
        runtimeMaterial.SetEmissiveFactor(source.emissiveFactor);
        runtimeMaterial.SetEmissiveStrength(source.emissiveStrength);
        runtimeMaterial.SetFeatureBits(source.featureBits);
        runtimeMaterial.SetShaderProfileId(source.shaderProfileId);

        // 実行時 Material は、元パスと解決済み cooked パスを両方保持する。
        runtimeMaterial.SetTextureSlot(ModelTextureUsage::BaseColor, ResolveAndLoadMaterialTexture(
            asset,
            materialNamePrefix + "_baseColor",
            source.baseColorTexture,
            ModelTextureUsage::BaseColor));
        runtimeMaterial.SetTextureSlot(ModelTextureUsage::Normal, ResolveAndLoadMaterialTexture(
            asset,
            materialNamePrefix + "_normal",
            source.normalTexture,
            ModelTextureUsage::Normal));
        runtimeMaterial.SetTextureSlot(ModelTextureUsage::MetallicRoughness, ResolveAndLoadMaterialTexture(
            asset,
            materialNamePrefix + "_metallicRoughness",
            source.metallicRoughnessTexture,
            ModelTextureUsage::MetallicRoughness));
        runtimeMaterial.SetTextureSlot(ModelTextureUsage::Occlusion, ResolveAndLoadMaterialTexture(
            asset,
            materialNamePrefix + "_occlusion",
            source.occlusionTexture,
            ModelTextureUsage::Occlusion));
        runtimeMaterial.SetTextureSlot(ModelTextureUsage::Emissive, ResolveAndLoadMaterialTexture(
            asset,
            materialNamePrefix + "_emissive",
            source.emissiveTexture,
            ModelTextureUsage::Emissive));
    }

    void ModelManager::ResolvePbrTexturePaths(ModelAsset& asset) const {
        auto resolveSlot = [this, &asset](const TextureSlot& slot, ModelTextureUsage usage) {
            TextureAsset3D* texture = FindTextureBySlot(asset, slot);
            if (texture == nullptr || texture->sourcePath.empty()) {
                return;
            }
            if (texture->resolvedPath.empty()) {
                texture->resolvedPath = ResolveTexturePath(texture->sourcePath, usage);
            }
            if (texture->resolvedPath.empty()) {
                texture->resolvedPath = texture->sourcePath;
            }
        };

        // 構造化描画は ModelAsset の texture 配列から SRV を引くため、ここで cooked パスへ寄せる。
        for (const MaterialAsset& material : asset.materials) {
            resolveSlot(material.baseColorTexture, ModelTextureUsage::BaseColor);
            resolveSlot(material.normalTexture, ModelTextureUsage::Normal);
            resolveSlot(material.metallicRoughnessTexture, ModelTextureUsage::MetallicRoughness);
            resolveSlot(material.occlusionTexture, ModelTextureUsage::Occlusion);
            resolveSlot(material.emissiveTexture, ModelTextureUsage::Emissive);
        }
    }

    bool ModelManager::BuildBuiltinCube(ModelAsset& asset) {
        auto mesh = std::make_unique<Mesh>();
        std::vector<VertexStatic3D> vertices;
        std::vector<uint32_t> indices;

        const std::array<MATH::Vec3, 8> p = {
            MATH::Vec3{-0.5f,-0.5f,-0.5f}, MATH::Vec3{0.5f,-0.5f,-0.5f}, MATH::Vec3{0.5f,0.5f,-0.5f}, MATH::Vec3{-0.5f,0.5f,-0.5f},
            MATH::Vec3{-0.5f,-0.5f, 0.5f}, MATH::Vec3{0.5f,-0.5f, 0.5f}, MATH::Vec3{0.5f,0.5f, 0.5f}, MATH::Vec3{-0.5f,0.5f, 0.5f}
        };

        auto pushTri = [&](int i0, int i1, int i2, const MATH::Vec3& n) {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            VertexStatic3D v0{};
            v0.position = p[i0];
            v0.normal = n;
            v0.u = 0.0f;
            v0.v = 0.0f;
            VertexStatic3D v1{};
            v1.position = p[i1];
            v1.normal = n;
            v1.u = 1.0f;
            v1.v = 0.0f;
            VertexStatic3D v2{};
            v2.position = p[i2];
            v2.normal = n;
            v2.u = 1.0f;
            v2.v = 1.0f;
            vertices.push_back(v0);
            vertices.push_back(v1);
            vertices.push_back(v2);
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
        };

        pushTri(0, 1, 2, { 0, 0, -1 }); pushTri(0, 2, 3, { 0, 0, -1 });
        pushTri(5, 4, 7, { 0, 0, 1 });  pushTri(5, 7, 6, { 0, 0, 1 });
        pushTri(4, 0, 3, { -1, 0, 0 }); pushTri(4, 3, 7, { -1, 0, 0 });
        pushTri(1, 5, 6, { 1, 0, 0 });  pushTri(1, 6, 2, { 1, 0, 0 });
        pushTri(3, 2, 6, { 0, 1, 0 });  pushTri(3, 6, 7, { 0, 1, 0 });
        pushTri(4, 5, 1, { 0, -1, 0 }); pushTri(4, 1, 0, { 0, -1, 0 });

        if (!mesh->CreateStatic(SERVICES::gCtx.device, vertices, indices)) {
            return false;
        }

        auto material = std::make_unique<Material>();
        material->SetBaseColor({ 0.85f, 0.9f, 1.0f, 1.0f });

        asset.bounds = { { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } };
        asset.SetMesh(std::move(mesh));
        asset.SetMaterial(std::move(material));
        return true;
    }

    bool ModelManager::LoadAsHmodel(ModelAsset& asset) {
        const std::string runtimePath = asset.GetSourcePath();
        const std::string runtimeName = asset.GetName();

        ModelAsset cooked{};
        std::string message{};
        if (!ReadHmodelFile(runtimePath, cooked, message)) {
            return false;
        }

        cooked.SetName(runtimeName.empty() ? cooked.id.value : runtimeName);
        cooked.SetSourcePath(runtimePath);
        asset = std::move(cooked);
        ResolvePbrTexturePaths(asset);
        BOUNDS::EnsureModelBounds(asset);
        return BuildRuntimeResources(asset);
    }

    bool ModelManager::BuildRuntimeResources(ModelAsset& asset) {
        std::vector<VertexStatic3D> legacyVertices;
        std::vector<uint32_t> legacyIndices;

        // HMODEL は CPU データを保持し、実行時だけ従来の Mesh/Material へ橋渡しする。
        for (const MeshAsset& meshAsset : asset.meshes) {
            for (const MeshPrimitive& primitive : meshAsset.primitives) {
                const uint32_t baseVertex = static_cast<uint32_t>(legacyVertices.size());

                if (!primitive.staticVertices.empty()) {
                    legacyVertices.reserve(legacyVertices.size() + primitive.staticVertices.size());
                    for (const Vertex3D& source : primitive.staticVertices) {
                        VertexStatic3D vertex{};
                        vertex.position = source.position;
                        vertex.normal = source.normal;
                        vertex.tangent = source.tangent;
                        vertex.u = source.uv0.x;
                        vertex.v = source.uv0.y;
                        legacyVertices.push_back(vertex);
                    }
                } else if (!primitive.skinnedVertices.empty()) {
                    legacyVertices.reserve(legacyVertices.size() + primitive.skinnedVertices.size());
                    for (const SkinnedVertex3D& source : primitive.skinnedVertices) {
                        VertexStatic3D vertex{};
                        vertex.position = source.position;
                        vertex.normal = source.normal;
                        vertex.tangent = source.tangent;
                        vertex.u = source.uv0.x;
                        vertex.v = source.uv0.y;
                        legacyVertices.push_back(vertex);
                    }
                }

                const uint32_t vertexCount = static_cast<uint32_t>(legacyVertices.size() - baseVertex);
                if (!primitive.indices.empty()) {
                    legacyIndices.reserve(legacyIndices.size() + primitive.indices.size());
                    for (uint32_t index : primitive.indices) {
                        if (index < vertexCount) {
                            legacyIndices.push_back(baseVertex + index);
                        }
                    }
                } else {
                    legacyIndices.reserve(legacyIndices.size() + vertexCount);
                    for (uint32_t index = 0; index < vertexCount; ++index) {
                        legacyIndices.push_back(baseVertex + index);
                    }
                }
            }
        }

        if (legacyVertices.empty() || legacyIndices.empty()) {
            return false;
        }

        auto mesh = std::make_unique<Mesh>();
        if (!mesh->CreateStatic(SERVICES::gCtx.device, legacyVertices, legacyIndices)) {
            return false;
        }

        auto material = std::make_unique<Material>();
        material->SetBaseColor({ 1.0f, 1.0f, 1.0f, 1.0f });

        if (!asset.materials.empty()) {
            const MaterialAsset& primaryMat = asset.materials.front();
            material->SetBaseColor(primaryMat.baseColorFactor);
            LoadPbrTextureSlots(asset, primaryMat, *material, asset.GetName() + "/runtime");
        }

        asset.SetMesh(std::move(mesh));
        asset.SetMaterial(std::move(material));
        return true;
    }

    bool ModelManager::LoadAsGltf(ModelAsset& asset, bool buildRuntimeResources) {
        const std::filesystem::path gltfPath(asset.GetSourcePath());
        std::ifstream ifs(gltfPath);
        if (!ifs.is_open()) {
            return false;
        }

        json root = json::parse(ifs, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            return false;
        }
        if (!root.contains("buffers") || !root["buffers"].is_array() || root["buffers"].empty()) {
            return false;
        }
        if (!root.contains("bufferViews") || !root["bufferViews"].is_array()) {
            return false;
        }
        if (!root.contains("accessors") || !root["accessors"].is_array()) {
            return false;
        }
        if (!root.contains("meshes") || !root["meshes"].is_array() || root["meshes"].empty()) {
            return false;
        }

        asset.importDiagnostics = {};

        const json& buffers = root["buffers"];
        const json& bufferViews = root["bufferViews"];
        const json& accessors = root["accessors"];
        const json& meshes = root["meshes"];

        std::vector<std::vector<uint8_t>> loadedBuffers(buffers.size());
        for (size_t i = 0; i < buffers.size(); ++i) {
            const std::string uri = buffers[i].value("uri", "");
            if (uri.empty()) {
                asset.importDiagnostics.messages.push_back("[glTF] buffer without uri is unsupported outside GLB");
                ++asset.importDiagnostics.unsupportedFeatureCount;
                return false;
            }
            if (uri.rfind("data:", 0) == 0) {
                if (!DecodeDataUriBytes(uri, loadedBuffers[i])) {
                    asset.importDiagnostics.messages.push_back("[glTF] data uri buffer decode failed");
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    return false;
                }
            } else if (!ReadBinaryFile(gltfPath.parent_path() / uri, loadedBuffers[i])) {
                return false;
            }
        }

        auto accessorCompCount = [](const std::string& type) -> int {
            if (type == "SCALAR") return 1;
            if (type == "VEC2") return 2;
            if (type == "VEC3") return 3;
            if (type == "VEC4") return 4;
            return 0;
        };

        auto readAccessorFloats = [&](int accessorIndex, int expectedComponents, std::vector<float>& out, int* outCount = nullptr) -> bool {
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) return false;
            const json& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const int bufferViewIndex = accessor.value("bufferView", -1);
            if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) return false;
            const json& view = bufferViews[static_cast<size_t>(bufferViewIndex)];
            const int bufferIndex = view.value("buffer", -1);
            if (bufferIndex < 0 || bufferIndex >= static_cast<int>(loadedBuffers.size())) return false;

            const int componentType = accessor.value("componentType", 0);
            if (componentType != 5126) return false; // FLOAT

            const int components = accessorCompCount(accessor.value("type", ""));
            if (components != expectedComponents) return false;

            const int count = accessor.value("count", 0);
            if (count <= 0) return false;
            if (outCount) *outCount = count;

            const size_t accessorOffset = static_cast<size_t>(accessor.value("byteOffset", 0));
            const size_t viewOffset = static_cast<size_t>(view.value("byteOffset", 0));
            const size_t stride = static_cast<size_t>(view.value("byteStride", components * 4));
            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];

            out.resize(static_cast<size_t>(count) * static_cast<size_t>(components));
            for (int i = 0; i < count; ++i) {
                const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                if (srcOffset + static_cast<size_t>(components * 4) > bufferData.size()) {
                    return false;
                }
                std::memcpy(out.data() + static_cast<size_t>(i * components), bufferData.data() + srcOffset, static_cast<size_t>(components * 4));
            }
            return true;
        };

        auto readAccessorNormalizedFloats = [&](int accessorIndex, int expectedComponents, std::vector<float>& out, int* outCount = nullptr) -> bool {
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) return false;
            const json& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const int bufferViewIndex = accessor.value("bufferView", -1);
            if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) return false;
            const json& view = bufferViews[static_cast<size_t>(bufferViewIndex)];
            const int bufferIndex = view.value("buffer", -1);
            if (bufferIndex < 0 || bufferIndex >= static_cast<int>(loadedBuffers.size())) return false;

            const int componentType = accessor.value("componentType", 0);
            const int components = accessorCompCount(accessor.value("type", ""));
            if (components != expectedComponents) return false;

            const int count = accessor.value("count", 0);
            if (count <= 0) return false;
            if (outCount) *outCount = count;

            const size_t componentSize =
                (componentType == 5126) ? 4u :
                ((componentType == 5121) ? 1u :
                ((componentType == 5123) ? 2u : 0u));
            if (componentSize == 0u) return false;

            const bool normalized = accessor.value("normalized", componentType != 5126);
            const size_t accessorOffset = static_cast<size_t>(accessor.value("byteOffset", 0));
            const size_t viewOffset = static_cast<size_t>(view.value("byteOffset", 0));
            const size_t elementSize = componentSize * static_cast<size_t>(components);
            const size_t stride = static_cast<size_t>(view.value("byteStride", static_cast<int>(elementSize)));
            if (stride < elementSize) return false;

            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];
            out.resize(static_cast<size_t>(count) * static_cast<size_t>(components));
            for (int i = 0; i < count; ++i) {
                const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                if (srcOffset + elementSize > bufferData.size()) {
                    return false;
                }
                for (int c = 0; c < components; ++c) {
                    const size_t componentOffset = srcOffset + componentSize * static_cast<size_t>(c);
                    float value = 0.0f;
                    if (componentType == 5126) {
                        std::memcpy(&value, bufferData.data() + componentOffset, sizeof(float));
                    } else if (componentType == 5121) {
                        const uint8_t raw = bufferData[componentOffset];
                        value = normalized ? static_cast<float>(raw) / 255.0f : static_cast<float>(raw);
                    } else {
                        uint16_t raw = 0;
                        std::memcpy(&raw, bufferData.data() + componentOffset, sizeof(uint16_t));
                        value = normalized ? static_cast<float>(raw) / 65535.0f : static_cast<float>(raw);
                    }
                    out[static_cast<size_t>(i * components + c)] = value;
                }
            }
            return true;
        };

        auto readAccessorScalars = [&](int accessorIndex, std::vector<float>& out) -> bool {
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) return false;
            const json& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const int bufferViewIndex = accessor.value("bufferView", -1);
            if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) return false;
            const json& view = bufferViews[static_cast<size_t>(bufferViewIndex)];
            const int bufferIndex = view.value("buffer", -1);
            if (bufferIndex < 0 || bufferIndex >= static_cast<int>(loadedBuffers.size())) return false;

            const int componentType = accessor.value("componentType", 0);
            const int count = accessor.value("count", 0);
            if (count <= 0) return false;

            const size_t accessorOffset = static_cast<size_t>(accessor.value("byteOffset", 0));
            const size_t viewOffset = static_cast<size_t>(view.value("byteOffset", 0));
            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];
            out.resize(static_cast<size_t>(count));

            if (componentType == 5126) {
                const size_t stride = static_cast<size_t>(view.value("byteStride", 4));
                for (int i = 0; i < count; ++i) {
                    const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                    if (srcOffset + 4 > bufferData.size()) return false;
                    std::memcpy(&out[static_cast<size_t>(i)], bufferData.data() + srcOffset, 4);
                }
                return true;
            }
            return false;
        };

        auto readIndices = [&](int accessorIndex, std::vector<uint32_t>& out) -> bool {
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) return false;
            const json& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const int bufferViewIndex = accessor.value("bufferView", -1);
            if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) return false;
            const json& view = bufferViews[static_cast<size_t>(bufferViewIndex)];
            const int bufferIndex = view.value("buffer", -1);
            if (bufferIndex < 0 || bufferIndex >= static_cast<int>(loadedBuffers.size())) return false;

            const int componentType = accessor.value("componentType", 0);
            const int count = accessor.value("count", 0);
            if (count <= 0) return false;
            const size_t accessorOffset = static_cast<size_t>(accessor.value("byteOffset", 0));
            const size_t viewOffset = static_cast<size_t>(view.value("byteOffset", 0));
            const size_t strideDefault =
                (componentType == 5121) ? 1u :
                ((componentType == 5123) ? 2u :
                ((componentType == 5125) ? 4u : 0u));
            if (strideDefault == 0u) return false;
            const size_t stride = static_cast<size_t>(view.value("byteStride", static_cast<int>(strideDefault)));
            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];

            out.resize(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                if (srcOffset + strideDefault > bufferData.size()) return false;
                if (componentType == 5121) {
                    out[static_cast<size_t>(i)] = static_cast<uint32_t>(bufferData[srcOffset]);
                } else if (componentType == 5123) {
                    uint16_t v = 0;
                    std::memcpy(&v, bufferData.data() + srcOffset, sizeof(uint16_t));
                    out[static_cast<size_t>(i)] = static_cast<uint32_t>(v);
                } else {
                    uint32_t v = 0;
                    std::memcpy(&v, bufferData.data() + srcOffset, sizeof(uint32_t));
                    out[static_cast<size_t>(i)] = v;
                }
            }
            return true;
        };

        asset.nodes.clear();
        asset.meshes.clear();
        asset.materials.clear();
        asset.textures.clear();
        asset.skins.clear();
        asset.animations.clear();
        asset.defaultSceneRootNode = 0;

        if (root.contains("extensionsUsed") && root["extensionsUsed"].is_array()) {
            for (const auto& extensionNode : root["extensionsUsed"]) {
                if (!extensionNode.is_string()) {
                    continue;
                }
                const std::string extension = extensionNode.get<std::string>();
                if (extension == "KHR_materials_unlit" ||
                    extension == "KHR_materials_emissive_strength") {
                    continue;
                }
                ++asset.importDiagnostics.unsupportedFeatureCount;
                asset.importDiagnostics.messages.push_back("[glTF] unsupported extension: " + extension);
            }
        }

        if (root.contains("images") && root["images"].is_array()) {
            for (const auto& img : root["images"]) {
                TextureAsset3D tex{};
                tex.name = img.value("name", "");
                const std::string uri = img.value("uri", "");
                if (uri.rfind("data:", 0) == 0 || img.contains("bufferView")) {
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    asset.importDiagnostics.messages.push_back("[glTF] embedded image requires HTEX source extraction: " + tex.name);
                } else if (!uri.empty()) {
                    tex.sourcePath = NormalizePathString(gltfPath.parent_path() / uri);
                }
                asset.textures.push_back(std::move(tex));
            }
        }

        if (root.contains("materials") && root["materials"].is_array()) {
            auto readTextureSlot = [&](const json& textureInfo, TextureSlot& slot) {
                const int textureIndex = textureInfo.value("index", -1);
                if (textureIndex >= 0 && root.contains("textures") && root["textures"].is_array() &&
                    textureIndex < static_cast<int>(root["textures"].size())) {
                    const int imageIndex = root["textures"][static_cast<size_t>(textureIndex)].value("source", -1);
                    slot.textureIndex = imageIndex;
                }
                slot.texCoord = textureInfo.value("texCoord", 0);
                if (textureInfo.contains("extensions") && textureInfo["extensions"].is_object() &&
                    textureInfo["extensions"].contains("KHR_texture_transform")) {
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    asset.importDiagnostics.messages.push_back("[glTF] KHR_texture_transform is recorded as unsupported for now");
                }
            };

            for (const auto& matNode : root["materials"]) {
                MaterialAsset mat{};
                mat.name = matNode.value("name", "");
                if (matNode.contains("pbrMetallicRoughness")) {
                    const json& pbr = matNode["pbrMetallicRoughness"];
                    if (pbr.contains("baseColorFactor") && pbr["baseColorFactor"].is_array() && pbr["baseColorFactor"].size() >= 4) {
                        mat.baseColorFactor = {
                            pbr["baseColorFactor"][0].get<float>(),
                            pbr["baseColorFactor"][1].get<float>(),
                            pbr["baseColorFactor"][2].get<float>(),
                            pbr["baseColorFactor"][3].get<float>()
                        };
                    }
                    if (pbr.contains("baseColorTexture") && pbr["baseColorTexture"].is_object()) {
                        readTextureSlot(pbr["baseColorTexture"], mat.baseColorTexture);
                    }
                    mat.metallicFactor = pbr.value("metallicFactor", mat.metallicFactor);
                    mat.roughnessFactor = pbr.value("roughnessFactor", mat.roughnessFactor);
                    if (pbr.contains("metallicRoughnessTexture") && pbr["metallicRoughnessTexture"].is_object()) {
                        readTextureSlot(pbr["metallicRoughnessTexture"], mat.metallicRoughnessTexture);
                    }
                }
                if (matNode.contains("normalTexture") && matNode["normalTexture"].is_object()) {
                    readTextureSlot(matNode["normalTexture"], mat.normalTexture);
                    mat.normalTexture.scale = matNode["normalTexture"].value("scale", mat.normalTexture.scale);
                }
                if (matNode.contains("occlusionTexture") && matNode["occlusionTexture"].is_object()) {
                    readTextureSlot(matNode["occlusionTexture"], mat.occlusionTexture);
                    mat.occlusionTexture.strength = matNode["occlusionTexture"].value("strength", mat.occlusionTexture.strength);
                }
                if (matNode.contains("emissiveFactor") && matNode["emissiveFactor"].is_array() && matNode["emissiveFactor"].size() >= 3) {
                    mat.emissiveFactor = {
                        matNode["emissiveFactor"][0].get<float>(),
                        matNode["emissiveFactor"][1].get<float>(),
                        matNode["emissiveFactor"][2].get<float>()
                    };
                }
                if (matNode.contains("emissiveTexture") && matNode["emissiveTexture"].is_object()) {
                    readTextureSlot(matNode["emissiveTexture"], mat.emissiveTexture);
                }

                const std::string alphaMode = matNode.value("alphaMode", "OPAQUE");
                if (alphaMode == "MASK") {
                    mat.alphaMode = AlphaMode::Mask;
                    mat.featureBits |= MATERIAL_FEATURES::AlphaMask;
                } else if (alphaMode == "BLEND") {
                    mat.alphaMode = AlphaMode::Blend;
                } else {
                    mat.alphaMode = AlphaMode::Opaque;
                }
                mat.alphaCutoff = matNode.value("alphaCutoff", mat.alphaCutoff);
                mat.doubleSided = matNode.value("doubleSided", false);

                if (matNode.contains("extensions") && matNode["extensions"].is_object()) {
                    const json& extensions = matNode["extensions"];
                    if (extensions.contains("KHR_materials_unlit")) {
                        mat.featureBits |= MATERIAL_FEATURES::Unlit;
                    }
                    if (extensions.contains("KHR_materials_emissive_strength") && extensions["KHR_materials_emissive_strength"].is_object()) {
                        mat.emissiveStrength = extensions["KHR_materials_emissive_strength"].value("emissiveStrength", mat.emissiveStrength);
                    }
                    for (auto it = extensions.begin(); it != extensions.end(); ++it) {
                        if (it.key() == "KHR_materials_unlit" ||
                            it.key() == "KHR_materials_emissive_strength") {
                            continue;
                        }
                        ++asset.importDiagnostics.unsupportedFeatureCount;
                        asset.importDiagnostics.messages.push_back("[glTF] unsupported material extension: " + it.key());
                    }
                }

                const bool hasEmissiveFactor =
                    std::abs(mat.emissiveFactor.x) > 1e-6f ||
                    std::abs(mat.emissiveFactor.y) > 1e-6f ||
                    std::abs(mat.emissiveFactor.z) > 1e-6f;
                if (hasEmissiveFactor || mat.emissiveTexture.textureIndex >= 0) {
                    mat.featureBits |= MATERIAL_FEATURES::Emissive;
                }
                asset.materials.push_back(std::move(mat));
            }
        }
        if (asset.materials.empty()) {
            asset.materials.push_back(MaterialAsset{});
        }

        ResolvePbrTexturePaths(asset);

        ReadGltfNodes(root, asset);
        ReadGltfSkins(root, accessors, bufferViews, loadedBuffers, asset);

        std::vector<VertexStatic3D> legacyVertices;
        std::vector<uint32_t> legacyIndices;

        for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
            const json& meshNode = meshes[meshIndex];
            if (!meshNode.contains("primitives") || !meshNode["primitives"].is_array()) {
                continue;
            }

            MeshAsset meshAsset{};
            meshAsset.name = meshNode.value("name", "Mesh" + std::to_string(meshIndex));

            const json& primitives = meshNode["primitives"];
            for (size_t primitiveIndex = 0; primitiveIndex < primitives.size(); ++primitiveIndex) {
                const json& primitive = primitives[primitiveIndex];
                const int primitiveMode = primitive.value("mode", 4);
                if (primitiveMode != 4) {
                    ++asset.importDiagnostics.unsupportedPrimitiveModeCount;
                    asset.importDiagnostics.messages.push_back(
                        "[glTF] unsupported primitive mode skipped: mesh=" +
                        std::to_string(meshIndex) +
                        " primitive=" + std::to_string(primitiveIndex) +
                        " mode=" + std::to_string(primitiveMode));
                    continue;
                }
                if (!primitive.contains("attributes") || !primitive["attributes"].is_object()) {
                    continue;
                }
                const json& attributes = primitive["attributes"];

                std::vector<float> positions;
                std::vector<float> normals;
                std::vector<float> texcoords;
                std::vector<float> texcoords1;
                std::vector<float> tangents;
                std::vector<float> colors4;
                std::vector<float> colors3;
                int vertexCount = 0;
                if (!readAccessorFloats(attributes.value("POSITION", -1), 3, positions, &vertexCount)) {
                    continue;
                }
                const bool hasNormals = readAccessorFloats(attributes.value("NORMAL", -1), 3, normals, nullptr);
                if (!hasNormals) {
                    normals.assign(static_cast<size_t>(vertexCount) * 3u, 0.0f);
                }
                if (!readAccessorNormalizedFloats(attributes.value("TEXCOORD_0", -1), 2, texcoords, nullptr)) {
                    texcoords.assign(static_cast<size_t>(vertexCount) * 2u, 0.0f);
                }
                if (!readAccessorNormalizedFloats(attributes.value("TEXCOORD_1", -1), 2, texcoords1, nullptr)) {
                    texcoords1.assign(static_cast<size_t>(vertexCount) * 2u, 0.0f);
                }
                readAccessorFloats(attributes.value("TANGENT", -1), 4, tangents, nullptr);
                const bool hasColor4 = readAccessorNormalizedFloats(attributes.value("COLOR_0", -1), 4, colors4, nullptr);
                const bool hasColor3 = !hasColor4 &&
                    readAccessorNormalizedFloats(attributes.value("COLOR_0", -1), 3, colors3, nullptr);

                std::vector<uint32_t> indices;
                if (!readIndices(primitive.value("indices", -1), indices)) {
                    indices.resize(static_cast<size_t>(vertexCount));
                    for (int i = 0; i < vertexCount; ++i) {
                        indices[static_cast<size_t>(i)] = static_cast<uint32_t>(i);
                    }
                }
                if (indices.size() < 3u || (indices.size() % 3u) != 0u) {
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    asset.importDiagnostics.messages.push_back(
                        "[glTF] triangle index count is invalid: mesh=" +
                        std::to_string(meshIndex) +
                        " primitive=" + std::to_string(primitiveIndex));
                    continue;
                }
                bool indicesInRange = true;
                for (uint32_t index : indices) {
                    if (index >= static_cast<uint32_t>(vertexCount)) {
                        indicesInRange = false;
                        break;
                    }
                }
                if (!indicesInRange) {
                    ++asset.importDiagnostics.unsupportedFeatureCount;
                    asset.importDiagnostics.messages.push_back(
                        "[glTF] index accessor references missing vertex: mesh=" +
                        std::to_string(meshIndex) +
                        " primitive=" + std::to_string(primitiveIndex));
                    continue;
                }

                MeshPrimitive primitiveAsset{};
                primitiveAsset.name = "Primitive" + std::to_string(primitiveIndex);
                primitiveAsset.layout = VertexLayoutKind::StaticPNTT;
                const int materialIndex = primitive.value("material", 0);
                primitiveAsset.materialIndex = materialIndex >= 0 &&
                    materialIndex < static_cast<int>(asset.materials.size())
                    ? static_cast<uint32_t>(materialIndex)
                    : 0u;
                primitiveAsset.indices = indices;
                primitiveAsset.hasMorphTargets =
                    primitive.contains("targets") &&
                    primitive["targets"].is_array() &&
                    !primitive["targets"].empty();
                if (primitiveAsset.hasMorphTargets) {
                    ++asset.importDiagnostics.skippedMorphPrimitiveCount;
                    asset.importDiagnostics.messages.push_back(
                        "[glTF] morph target primitive uses legacy fallback only: mesh=" +
                        std::to_string(meshIndex) +
                        " primitive=" + std::to_string(primitiveIndex));
                }
                if (primitive.contains("extensions") && primitive["extensions"].is_object()) {
                    for (auto it = primitive["extensions"].begin(); it != primitive["extensions"].end(); ++it) {
                        ++asset.importDiagnostics.unsupportedFeatureCount;
                        asset.importDiagnostics.messages.push_back("[glTF] unsupported primitive extension: " + it.key());
                    }
                }
                primitiveAsset.staticVertices.reserve(static_cast<size_t>(vertexCount));

                for (int i = 0; i < vertexCount; ++i) {
                    const size_t p = static_cast<size_t>(i) * 3u;
                    const size_t t = static_cast<size_t>(i) * 2u;
                    Vertex3D out{};
                    out.position = { positions[p + 0], positions[p + 1], positions[p + 2] };
                    out.normal = { normals[p + 0], normals[p + 1], normals[p + 2] };
                    out.uv0 = { texcoords[t + 0], texcoords[t + 1] };
                    out.uv1 = { texcoords1[t + 0], texcoords1[t + 1] };
                    if (!tangents.empty()) {
                        const size_t tg = static_cast<size_t>(i) * 4u;
                        out.tangent = { tangents[tg + 0], tangents[tg + 1], tangents[tg + 2], tangents[tg + 3] };
                    } else {
                        out.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
                    }
                    if (hasColor4) {
                        const size_t c = static_cast<size_t>(i) * 4u;
                        out.color0 = { colors4[c + 0], colors4[c + 1], colors4[c + 2], colors4[c + 3] };
                    } else if (hasColor3) {
                        const size_t c = static_cast<size_t>(i) * 3u;
                        out.color0 = { colors3[c + 0], colors3[c + 1], colors3[c + 2], 1.0f };
                    }
                    primitiveAsset.staticVertices.push_back(out);
                }

                if (!hasNormals) {
                    GenerateStaticPrimitiveNormals(primitiveAsset);
                }
                if (tangents.empty() && MaterialHasNormalTexture(asset, primitiveAsset.materialIndex)) {
                    GenerateStaticPrimitiveTangents(primitiveAsset);
                }

                const uint32_t legacyBaseVertex = static_cast<uint32_t>(legacyVertices.size());
                for (const Vertex3D& out : primitiveAsset.staticVertices) {
                    VertexStatic3D legacy{};
                    legacy.position = out.position;
                    legacy.normal = out.normal;
                    legacy.tangent = out.tangent;
                    legacy.u = out.uv0.x;
                    legacy.v = out.uv0.y;
                    legacyVertices.push_back(legacy);
                }
                for (uint32_t index : indices) {
                    legacyIndices.push_back(legacyBaseVertex + index);
                }

                if (attributes.contains("JOINTS_0") && attributes.contains("WEIGHTS_0")) {
                    std::vector<std::array<uint16_t, 4>> joints;
                    std::vector<std::array<float, 4>> weights;
                    const bool hasJoints = ReadAccessorJointVec4(attributes.value("JOINTS_0", -1), accessors, bufferViews, loadedBuffers, joints);
                    const bool hasWeights = ReadAccessorWeightVec4(attributes.value("WEIGHTS_0", -1), accessors, bufferViews, loadedBuffers, weights);
                    if (hasJoints && hasWeights &&
                        joints.size() == static_cast<size_t>(vertexCount) &&
                        weights.size() == static_cast<size_t>(vertexCount)) {
                        primitiveAsset.skinnedVertices.resize(static_cast<size_t>(vertexCount));
                        for (int i = 0; i < vertexCount; ++i) {
                            const Vertex3D& staticVertex = primitiveAsset.staticVertices[static_cast<size_t>(i)];
                            SkinnedVertex3D skinned{};
                            skinned.position = staticVertex.position;
                            skinned.normal = staticVertex.normal;
                            skinned.tangent = staticVertex.tangent;
                            skinned.uv0 = staticVertex.uv0;
                            skinned.uv1 = staticVertex.uv1;
                            skinned.color0 = staticVertex.color0;
                            for (size_t c = 0; c < 4; ++c) {
                                skinned.joints[c] = joints[static_cast<size_t>(i)][c];
                                skinned.weights[c] = weights[static_cast<size_t>(i)][c];
                            }
                            primitiveAsset.skinnedVertices[static_cast<size_t>(i)] = skinned;
                        }
                    }
                }

                primitiveAsset.bounds = BOUNDS::ComputePrimitiveBounds(primitiveAsset);
                meshAsset.primitives.push_back(std::move(primitiveAsset));
            }

            if (!meshAsset.primitives.empty()) {
                meshAsset.bounds = BOUNDS::ComputeMeshBounds(meshAsset);
                asset.meshes.push_back(std::move(meshAsset));
            }
        }

        if (root.contains("animations") && root["animations"].is_array()) {
            for (const auto& animNode : root["animations"]) {
                AnimationClip clip{};
                clip.name = animNode.value("name", "Clip");

                if (!animNode.contains("channels") || !animNode["channels"].is_array() ||
                    !animNode.contains("samplers") || !animNode["samplers"].is_array()) {
                    continue;
                }

                const auto& channels = animNode["channels"];
                const auto& samplers = animNode["samplers"];
                for (const auto& ch : channels) {
                    if (!ch.is_object() || !ch.contains("target") || !ch["target"].is_object()) {
                        continue;
                    }
                    const int samplerIndex = ch.value("sampler", -1);
                    if (samplerIndex < 0 || samplerIndex >= static_cast<int>(samplers.size())) {
                        continue;
                    }
                    const auto& sampler = samplers[static_cast<size_t>(samplerIndex)];
                    const int inputAccessor = sampler.value("input", -1);
                    const int outputAccessor = sampler.value("output", -1);
                    if (inputAccessor < 0 || outputAccessor < 0) {
                        continue;
                    }

                    std::vector<float> times;
                    if (!readAccessorScalars(inputAccessor, times)) {
                        continue;
                    }

                    NodeAnimationChannel channel{};
                    channel.targetNode = ch["target"].value("node", -1);
                    const std::string path = ch["target"].value("path", "translation");
                    if (path == "rotation") {
                        channel.path = AnimationTargetPath::Rotation;
                        std::vector<float> values;
                        if (!readAccessorFloats(outputAccessor, 4, values, nullptr)) {
                            continue;
                        }
                        const size_t keyCount = std::min(times.size(), values.size() / 4u);
                        channel.quatKeys.reserve(keyCount);
                        for (size_t i = 0; i < keyCount; ++i) {
                            AnimationKeyframe<MATH::Quat> key{};
                            key.timeSec = times[i];
                            key.value = {
                                values[i * 4u + 0u],
                                values[i * 4u + 1u],
                                values[i * 4u + 2u],
                                values[i * 4u + 3u]
                            };
                            channel.quatKeys.push_back(key);
                            if (key.timeSec > clip.durationSec) {
                                clip.durationSec = key.timeSec;
                            }
                        }
                    } else {
                        channel.path = (path == "scale") ? AnimationTargetPath::Scale : AnimationTargetPath::Translation;
                        std::vector<float> values;
                        if (!readAccessorFloats(outputAccessor, 3, values, nullptr)) {
                            continue;
                        }
                        const size_t keyCount = std::min(times.size(), values.size() / 3u);
                        channel.vec3Keys.reserve(keyCount);
                        for (size_t i = 0; i < keyCount; ++i) {
                            AnimationKeyframe<MATH::Vec3> key{};
                            key.timeSec = times[i];
                            key.value = {
                                values[i * 3u + 0u],
                                values[i * 3u + 1u],
                                values[i * 3u + 2u]
                            };
                            channel.vec3Keys.push_back(key);
                            if (key.timeSec > clip.durationSec) {
                                clip.durationSec = key.timeSec;
                            }
                        }
                    }

                    const std::string interpolation = sampler.value("interpolation", "LINEAR");
                    if (interpolation == "STEP") {
                        channel.interpolation = AnimationInterpolation::Step;
                    } else if (interpolation == "CUBICSPLINE") {
                        channel.interpolation = AnimationInterpolation::CubicSpline;
                    } else {
                        channel.interpolation = AnimationInterpolation::Linear;
                    }
                    clip.channels.push_back(std::move(channel));
                }
                if (!clip.channels.empty()) {
                    asset.animations.push_back(std::move(clip));
                }
            }
        }

        if (legacyVertices.empty() || legacyIndices.empty()) {
            return false;
        }

        BOUNDS::EnsureModelBounds(asset);

        if (!buildRuntimeResources) {
            return true;
        }

        auto mesh = std::make_unique<Mesh>();
        if (!mesh->CreateStatic(SERVICES::gCtx.device, legacyVertices, legacyIndices)) {
            return false;
        }

        auto material = std::make_unique<Material>();
        const MaterialAsset& primaryMat = asset.materials.front();
        material->SetBaseColor(primaryMat.baseColorFactor);
        LoadPbrTextureSlots(asset, primaryMat, *material, asset.GetName() + "/gltf");

        asset.SetMesh(std::move(mesh));
        asset.SetMaterial(std::move(material));
        return true;
    }

    bool ModelManager::LoadAsObj(ModelAsset& asset, bool buildRuntimeResources) {
        std::ifstream file(asset.GetSourcePath());
        if (!file.is_open()) {
            return false;
        }

        std::vector<MATH::Vec3> positions;
        std::vector<MATH::Vec3> normals;
        std::vector<std::array<float, 2>> uvs;
        std::vector<std::filesystem::path> mtllibPaths;

        struct ObjPrimitiveBuilder {
            std::string objectName;
            std::string groupName;
            std::string materialName;
            std::vector<Vertex3D> vertices;
            std::vector<uint32_t> indices;
            std::unordered_map<ObjKey, uint32_t, ObjKeyHash> uniqueMap;
            bool missingNormal = false;
        };

        std::vector<ObjPrimitiveBuilder> builders;
        std::unordered_map<std::string, size_t> builderByKey;
        std::string currentObject = "Object";
        std::string currentGroup = "Group";
        std::string currentMaterial = "Default";
        bool smoothingEnabled = true;

        auto resolveObjIndex = [](int raw, size_t count) -> int {
            if (raw > 0) {
                return raw - 1;
            }
            if (raw < 0) {
                const int resolved = static_cast<int>(count) + raw;
                return resolved >= 0 ? resolved : -1;
            }
            return -1;
        };

        auto parseObjToken = [&](const std::string& token, ObjKey& key) -> bool {
            std::stringstream ss(token);
            std::string part;
            try {
                if (!std::getline(ss, part, '/')) return false;
                key.pos = part.empty() ? -1 : resolveObjIndex(std::stoi(part), positions.size());

                if (std::getline(ss, part, '/')) {
                    key.uv = part.empty() ? -1 : resolveObjIndex(std::stoi(part), uvs.size());
                }
                if (std::getline(ss, part, '/')) {
                    key.normal = part.empty() ? -1 : resolveObjIndex(std::stoi(part), normals.size());
                }
            } catch (...) {
                return false;
            }
            return key.pos >= 0;
        };

        auto getBuilder = [&]() -> ObjPrimitiveBuilder& {
            const std::string key = currentObject + "\n" + currentGroup + "\n" + currentMaterial;
            auto found = builderByKey.find(key);
            if (found != builderByKey.end()) {
                return builders[found->second];
            }

            ObjPrimitiveBuilder builder{};
            builder.objectName = currentObject;
            builder.groupName = currentGroup;
            builder.materialName = currentMaterial;
            const size_t index = builders.size();
            builders.push_back(std::move(builder));
            builderByKey.emplace(key, index);
            return builders.back();
        };

        auto appendVertex = [&](ObjPrimitiveBuilder& builder, const ObjKey& key, const MATH::Vec3& faceNormal) -> uint32_t {
            const bool forceUnique = !smoothingEnabled && key.normal < 0;
            if (!forceUnique) {
                auto found = builder.uniqueMap.find(key);
                if (found != builder.uniqueMap.end()) {
                    return found->second;
                }
            }

            Vertex3D vertex{};
            vertex.position = positions[static_cast<size_t>(key.pos)];
            if (key.normal >= 0 && key.normal < static_cast<int>(normals.size())) {
                vertex.normal = normals[static_cast<size_t>(key.normal)];
            } else {
                builder.missingNormal = true;
                vertex.normal = smoothingEnabled ? MATH::Vec3{} : faceNormal;
            }
            if (key.uv >= 0 && key.uv < static_cast<int>(uvs.size())) {
                vertex.uv0 = { uvs[static_cast<size_t>(key.uv)][0], 1.0f - uvs[static_cast<size_t>(key.uv)][1] };
            }
            vertex.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };

            const uint32_t newIndex = static_cast<uint32_t>(builder.vertices.size());
            builder.vertices.push_back(vertex);
            if (!forceUnique) {
                builder.uniqueMap.emplace(key, newIndex);
            }
            return newIndex;
        };

        std::string line;
        while (std::getline(file, line)) {
            if (line.size() < 2) continue;
            std::stringstream ss(line);
            std::string tag;
            ss >> tag;

            if (tag == "v") {
                MATH::Vec3 p{};
                ss >> p.x >> p.y >> p.z;
                positions.push_back(p);
            } else if (tag == "mtllib") {
                std::string mtlName;
                std::getline(ss, mtlName);
                mtlName = Trim(mtlName);
                if (!mtlName.empty()) {
                    const std::filesystem::path objPath(asset.GetSourcePath());
                    mtllibPaths.push_back((objPath.parent_path() / mtlName).lexically_normal());
                }
            } else if (tag == "usemtl") {
                std::string mtlName;
                ss >> mtlName;
                if (!mtlName.empty()) {
                    currentMaterial = mtlName;
                }
            } else if (tag == "o") {
                std::string name;
                std::getline(ss, name);
                name = Trim(name);
                currentObject = name.empty() ? "Object" : name;
            } else if (tag == "g") {
                std::string name;
                std::getline(ss, name);
                name = Trim(name);
                currentGroup = name.empty() ? "Group" : name;
            } else if (tag == "s") {
                std::string smoothing;
                ss >> smoothing;
                smoothingEnabled = !(smoothing == "off" || smoothing == "0");
            } else if (tag == "vn") {
                MATH::Vec3 n{};
                ss >> n.x >> n.y >> n.z;
                normals.push_back(n);
            } else if (tag == "vt") {
                std::array<float, 2> uv{};
                ss >> uv[0] >> uv[1];
                uvs.push_back(uv);
            } else if (tag == "f") {
                std::vector<ObjKey> faceKeys;
                std::string token;
                while (ss >> token) {
                    ObjKey key{};
                    if (parseObjToken(token, key)) {
                        faceKeys.push_back(key);
                    }
                }
                if (faceKeys.size() < 3) {
                    continue;
                }

                ObjPrimitiveBuilder& builder = getBuilder();
                for (size_t i = 1; i + 1 < faceKeys.size(); ++i) {
                    const ObjKey tri[3] = { faceKeys[0], faceKeys[i], faceKeys[i + 1] };
                    if (tri[0].pos < 0 || tri[0].pos >= static_cast<int>(positions.size()) ||
                        tri[1].pos < 0 || tri[1].pos >= static_cast<int>(positions.size()) ||
                        tri[2].pos < 0 || tri[2].pos >= static_cast<int>(positions.size())) {
                        return false;
                    }

                    const MATH::Vec3 p0 = positions[static_cast<size_t>(tri[0].pos)];
                    const MATH::Vec3 p1 = positions[static_cast<size_t>(tri[1].pos)];
                    const MATH::Vec3 p2 = positions[static_cast<size_t>(tri[2].pos)];
                    MATH::Vec3 faceNormal = MATH::Normalize(MATH::Cross(p1 - p0, p2 - p0));
                    if (MATH::Length(faceNormal) <= 1.0e-6f) {
                        faceNormal = { 0.0f, 1.0f, 0.0f };
                    }
                    for (const ObjKey& key : tri) {
                        builder.indices.push_back(appendVertex(builder, key, faceNormal));
                    }
                }
            }
        }

        bool hasGeometry = false;
        for (const ObjPrimitiveBuilder& builder : builders) {
            hasGeometry = hasGeometry || (!builder.vertices.empty() && !builder.indices.empty());
        }
        if (!hasGeometry) {
            return false;
        }

        asset.importDiagnostics = {};
        std::unordered_map<std::string, ObjMaterialInfo> mtlMaterials;
        for (const std::filesystem::path& mtlPath : mtllibPaths) {
            std::unordered_map<std::string, ObjMaterialInfo> parsed;
            if (ParseMtlLibrary(mtlPath, parsed)) {
                for (auto& pair : parsed) {
                    mtlMaterials[pair.first] = std::move(pair.second);
                }
            } else {
                ++asset.importDiagnostics.unsupportedFeatureCount;
                asset.importDiagnostics.messages.push_back("[OBJ] mtllib not found or empty: " + mtlPath.generic_string());
            }
        }

        asset.nodes.clear();
        asset.meshes.clear();
        asset.materials.clear();
        asset.textures.clear();
        asset.skins.clear();
        asset.animations.clear();
        asset.defaultSceneRootNode = 0;

        ModelNode rootNode{};
        rootNode.name = "OBJ Root";
        rootNode.meshIndex = 0;
        asset.nodes.push_back(std::move(rootNode));

        std::unordered_map<std::string, int> textureIndexByPath;
        auto addTexture = [&](const std::string& path, const std::string& name) -> int {
            if (path.empty()) {
                return -1;
            }
            auto found = textureIndexByPath.find(path);
            if (found != textureIndexByPath.end()) {
                return found->second;
            }

            TextureAsset3D texture{};
            texture.name = name;
            texture.sourcePath = path;
            const int index = static_cast<int>(asset.textures.size());
            asset.textures.push_back(std::move(texture));
            textureIndexByPath.emplace(path, index);
            return index;
        };

        std::unordered_map<std::string, uint32_t> materialIndexByName;
        auto resolveMaterialIndex = [&](const std::string& materialName) -> uint32_t {
            const std::string name = materialName.empty() ? "Default" : materialName;
            auto found = materialIndexByName.find(name);
            if (found != materialIndexByName.end()) {
                return found->second;
            }

            ObjMaterialInfo info{};
            info.name = name;
            if (auto mtl = mtlMaterials.find(name); mtl != mtlMaterials.end()) {
                info = mtl->second;
            }

            MaterialAsset materialAsset{};
            materialAsset.name = info.name.empty() ? name : info.name;
            materialAsset.baseColorFactor = info.baseColor;
            materialAsset.roughnessFactor = info.roughness;
            materialAsset.metallicFactor = info.metallic;
            if (info.hasAlpha) {
                materialAsset.alphaMode = AlphaMode::Blend;
            }
            const int baseColorIndex = addTexture(info.baseColorMapPath, materialAsset.name + "_baseColor");
            if (baseColorIndex >= 0) {
                materialAsset.baseColorTexture.textureIndex = baseColorIndex;
            }
            const int normalIndex = addTexture(info.normalMapPath, materialAsset.name + "_normal");
            if (normalIndex >= 0) {
                materialAsset.normalTexture.textureIndex = normalIndex;
            }
            const int roughnessIndex = addTexture(info.roughnessMapPath, materialAsset.name + "_roughness");
            if (roughnessIndex >= 0) {
                materialAsset.metallicRoughnessTexture.textureIndex = roughnessIndex;
            }

            const uint32_t index = static_cast<uint32_t>(asset.materials.size());
            asset.materials.push_back(std::move(materialAsset));
            materialIndexByName.emplace(name, index);
            return index;
        };

        MeshAsset meshAsset{};
        meshAsset.name = asset.GetName().empty() ? "OBJ Mesh" : asset.GetName();
        std::vector<VertexStatic3D> legacyVertices;
        std::vector<uint32_t> legacyIndices;
        for (ObjPrimitiveBuilder& builder : builders) {
            if (builder.vertices.empty() || builder.indices.empty()) {
                continue;
            }

            MeshPrimitive primitive{};
            primitive.name = builder.objectName + "/" + builder.groupName + "/" + builder.materialName;
            primitive.layout = VertexLayoutKind::StaticPNTT;
            primitive.materialIndex = resolveMaterialIndex(builder.materialName);
            primitive.indices = std::move(builder.indices);
            primitive.staticVertices = std::move(builder.vertices);
            if (builder.missingNormal) {
                GenerateStaticPrimitiveNormals(primitive);
            }
            if (MaterialHasNormalTexture(asset, primitive.materialIndex)) {
                GenerateStaticPrimitiveTangents(primitive);
            }
            primitive.bounds = BOUNDS::ComputePrimitiveBounds(primitive);

            const uint32_t legacyBaseVertex = static_cast<uint32_t>(legacyVertices.size());
            for (const Vertex3D& source : primitive.staticVertices) {
                VertexStatic3D legacy{};
                legacy.position = source.position;
                legacy.normal = source.normal;
                legacy.tangent = source.tangent;
                legacy.u = source.uv0.x;
                legacy.v = source.uv0.y;
                legacyVertices.push_back(legacy);
            }
            for (uint32_t index : primitive.indices) {
                legacyIndices.push_back(legacyBaseVertex + index);
            }
            meshAsset.primitives.push_back(std::move(primitive));
        }

        if (meshAsset.primitives.empty()) {
            return false;
        }
        meshAsset.bounds = BOUNDS::ComputeMeshBounds(meshAsset);
        asset.meshes.push_back(std::move(meshAsset));
        if (asset.materials.empty()) {
            resolveMaterialIndex("Default");
        }

        ResolvePbrTexturePaths(asset);
        BOUNDS::EnsureModelBounds(asset);

        if (!buildRuntimeResources) {
            return true;
        }

        auto mesh = std::make_unique<Mesh>();
        if (!mesh->CreateStatic(SERVICES::gCtx.device, legacyVertices, legacyIndices)) {
            return false;
        }

        auto material = std::make_unique<Material>();
        const MaterialAsset& primaryMat = asset.materials.front();
        material->SetBaseColor(primaryMat.baseColorFactor);
        LoadPbrTextureSlots(asset, primaryMat, *material, asset.GetName() + "/obj");
        asset.SetMesh(std::move(mesh));
        asset.SetMaterial(std::move(material));
        return true;
    }

} // namespace HIKARI
