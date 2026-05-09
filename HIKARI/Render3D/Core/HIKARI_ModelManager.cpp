#include "Render3D/HIKARI_ModelManager.h"
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <json.hpp>
#include "HIKARI_DxTexture.h"
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
        };

        bool ParseObjIndexToken(const std::string& token, ObjKey& key) {
            std::stringstream ss(token);
            std::string part;

            if (!std::getline(ss, part, '/')) return false;
            key.pos = part.empty() ? -1 : std::stoi(part) - 1;

            if (std::getline(ss, part, '/')) {
                key.uv = part.empty() ? -1 : std::stoi(part) - 1;
            }
            if (std::getline(ss, part, '/')) {
                key.normal = part.empty() ? -1 : std::stoi(part) - 1;
            }
            return key.pos >= 0;
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

        void SanitizeAndFixNormalOrientation(std::vector<VertexStatic3D>& vertices, const std::vector<uint32_t>& indices) {
            if (vertices.empty()) {
                return;
            }

            int comparedTriangleCount = 0;
            int opposedTriangleCount = 0;
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                const uint32_t i0 = indices[i + 0];
                const uint32_t i1 = indices[i + 1];
                const uint32_t i2 = indices[i + 2];
                if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                    continue;
                }

                const MATH::Vec3 p0 = vertices[i0].position;
                const MATH::Vec3 p1 = vertices[i1].position;
                const MATH::Vec3 p2 = vertices[i2].position;
                const MATH::Vec3 faceNormal = MATH::Normalize(MATH::Cross(p1 - p0, p2 - p0));
                if (MATH::Length(faceNormal) <= 1e-6f) {
                    continue;
                }

                const MATH::Vec3 avgNormal = MATH::Normalize((vertices[i0].normal + vertices[i1].normal + vertices[i2].normal) * (1.0f / 3.0f));
                if (MATH::Length(avgNormal) <= 1e-6f) {
                    continue;
                }

                ++comparedTriangleCount;
                if (MATH::Dot(avgNormal, faceNormal) < 0.0f) {
                    ++opposedTriangleCount;
                }
            }

            const bool shouldFlipAllNormals = (comparedTriangleCount > 0) && (opposedTriangleCount * 2 > comparedTriangleCount);
            for (auto& v : vertices) {
                MATH::Vec3 n = v.normal;
                if (shouldFlipAllNormals) {
                    n = n * -1.0f;
                }

                const float len = MATH::Length(n);
                if (len <= 1e-6f) {
                    v.normal = { 0.0f, 1.0f, 0.0f };
                } else {
                    v.normal = n * (1.0f / len);
                }
            }
        }

        bool ParseMtlMaterial(const std::filesystem::path& mtlPath, const std::string& targetMtlName, ObjMaterialInfo& outInfo) {
            std::ifstream mtlFile(mtlPath);
            if (!mtlFile.is_open()) {
                return false;
            }

            bool inTargetMaterial = false;
            bool foundTargetMaterial = false;
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
                    inTargetMaterial = materialName == targetMtlName;
                    if (inTargetMaterial) {
                        outInfo = ObjMaterialInfo{};
                        outInfo.name = materialName;
                        foundTargetMaterial = true;
                    } else if (foundTargetMaterial) {
                        break;
                    }
                } else if (inTargetMaterial && tag == "Kd") {
                    float r = 1.0f;
                    float g = 1.0f;
                    float b = 1.0f;
                    ss >> r >> g >> b;
                    outInfo.baseColor = { r, g, b, 1.0f };
                } else if (inTargetMaterial && tag == "map_Kd") {
                    std::string mapValue;
                    std::getline(ss, mapValue);
                    mapValue = Trim(mapValue);
                    if (!mapValue.empty()) {
                        const std::filesystem::path mapPath = mtlPath.parent_path() / mapValue;
                        outInfo.baseColorMapPath = NormalizePathString(mapPath);
                    }
                }
            }
            return foundTargetMaterial;
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
            if (ext == ".obj") {
                ok = LoadAsObj(*asset);
            } else if (ext == ".gltf") {
                ok = LoadAsGltf(*asset);
            }
        }

        asset->SetState(ok ? ModelAsset::State::Loaded : ModelAsset::State::Failed);
        return ok;
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

        asset.SetMesh(std::move(mesh));
        asset.SetMaterial(std::move(material));
        return true;
    }

    bool ModelManager::LoadAsGltf(ModelAsset& asset) {
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

        const json& buffers = root["buffers"];
        const json& bufferViews = root["bufferViews"];
        const json& accessors = root["accessors"];
        const json& meshes = root["meshes"];

        std::vector<std::vector<uint8_t>> loadedBuffers(buffers.size());
        for (size_t i = 0; i < buffers.size(); ++i) {
            const std::string uri = buffers[i].value("uri", "");
            if (uri.empty() || uri.rfind("data:", 0) == 0) {
                return false;
            }
            if (!ReadBinaryFile(gltfPath.parent_path() / uri, loadedBuffers[i])) {
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
            const size_t strideDefault = (componentType == 5123) ? 2u : ((componentType == 5125) ? 4u : 0u);
            if (strideDefault == 0u) return false;
            const size_t stride = static_cast<size_t>(view.value("byteStride", static_cast<int>(strideDefault)));
            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];

            out.resize(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                if (srcOffset + strideDefault > bufferData.size()) return false;
                if (componentType == 5123) {
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

        if (root.contains("images") && root["images"].is_array()) {
            for (const auto& img : root["images"]) {
                TextureAsset3D tex{};
                tex.name = img.value("name", "");
                const std::string uri = img.value("uri", "");
                if (!uri.empty()) {
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
                if (!primitive.contains("attributes") || !primitive["attributes"].is_object()) {
                    continue;
                }
                const json& attributes = primitive["attributes"];

                std::vector<float> positions;
                std::vector<float> normals;
                std::vector<float> texcoords;
                std::vector<float> tangents;
                int vertexCount = 0;
                if (!readAccessorFloats(attributes.value("POSITION", -1), 3, positions, &vertexCount)) {
                    continue;
                }
                if (!readAccessorFloats(attributes.value("NORMAL", -1), 3, normals, nullptr)) {
                    normals.assign(static_cast<size_t>(vertexCount) * 3u, 0.0f);
                    for (int i = 0; i < vertexCount; ++i) {
                        normals[static_cast<size_t>(i) * 3u + 1u] = 1.0f;
                    }
                }
                if (!readAccessorFloats(attributes.value("TEXCOORD_0", -1), 2, texcoords, nullptr)) {
                    texcoords.assign(static_cast<size_t>(vertexCount) * 2u, 0.0f);
                }
                readAccessorFloats(attributes.value("TANGENT", -1), 4, tangents, nullptr);

                std::vector<uint32_t> indices;
                if (!readIndices(primitive.value("indices", -1), indices)) {
                    indices.resize(static_cast<size_t>(vertexCount));
                    for (int i = 0; i < vertexCount; ++i) {
                        indices[static_cast<size_t>(i)] = static_cast<uint32_t>(i);
                    }
                }

                MeshPrimitive primitiveAsset{};
                primitiveAsset.name = "Primitive" + std::to_string(primitiveIndex);
                primitiveAsset.layout = VertexLayoutKind::StaticPNTT;
                primitiveAsset.materialIndex = static_cast<uint32_t>(std::max(0, primitive.value("material", 0)));
                primitiveAsset.indices = indices;
                primitiveAsset.staticVertices.reserve(static_cast<size_t>(vertexCount));

                const uint32_t legacyBaseVertex = static_cast<uint32_t>(legacyVertices.size());
                for (int i = 0; i < vertexCount; ++i) {
                    const size_t p = static_cast<size_t>(i) * 3u;
                    const size_t t = static_cast<size_t>(i) * 2u;
                    Vertex3D out{};
                    out.position = { positions[p + 0], positions[p + 1], positions[p + 2] };
                    out.normal = { normals[p + 0], normals[p + 1], normals[p + 2] };
                    out.uv0 = { texcoords[t + 0], texcoords[t + 1] };
                    if (!tangents.empty()) {
                        const size_t tg = static_cast<size_t>(i) * 4u;
                        out.tangent = { tangents[tg + 0], tangents[tg + 1], tangents[tg + 2], tangents[tg + 3] };
                    } else {
                        out.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
                    }
                    primitiveAsset.staticVertices.push_back(out);

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

                meshAsset.primitives.push_back(std::move(primitiveAsset));
            }

            if (!meshAsset.primitives.empty()) {
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

        auto mesh = std::make_unique<Mesh>();
        if (!mesh->CreateStatic(SERVICES::gCtx.device, legacyVertices, legacyIndices)) {
            return false;
        }

        auto material = std::make_unique<Material>();
        const MaterialAsset& primaryMat = asset.materials.front();
        material->SetBaseColor(primaryMat.baseColorFactor);
        material->SetBaseColorTextureHandle(-1);
        if (primaryMat.baseColorTexture.textureIndex >= 0 &&
            primaryMat.baseColorTexture.textureIndex < static_cast<int>(asset.textures.size())) {
            const std::string& texPath = asset.textures[static_cast<size_t>(primaryMat.baseColorTexture.textureIndex)].sourcePath;
            if (!texPath.empty()) {
                material->SetBaseColorTexturePath(texPath);
                const int handle = DXTEX::DxTextureManager::LoadTexture(asset.GetName() + "/gltf_baseColor", texPath);
                if (handle >= 0) {
                    material->SetBaseColorTextureHandle(handle);
                }
            }
        }

        asset.SetMesh(std::move(mesh));
        asset.SetMaterial(std::move(material));
        return true;
    }

    bool ModelManager::LoadAsObj(ModelAsset& asset) {
        std::ifstream file(asset.GetSourcePath());
        if (!file.is_open()) {
            return false;
        }

        std::vector<MATH::Vec3> positions;
        std::vector<MATH::Vec3> normals;
        std::vector<std::array<float, 2>> uvs;

        std::vector<VertexStatic3D> vertices;
        std::vector<uint32_t> indices;
        std::unordered_map<ObjKey, uint32_t, ObjKeyHash> uniqueMap;
        bool hasAnyFaceNormalRef = false;
        bool allFaceNormalsValid = true;
        std::string mtllibPath;
        std::string firstUsedMaterialName;

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
                if (mtllibPath.empty()) {
                    std::string mtlName;
                    std::getline(ss, mtlName);
                    mtlName = Trim(mtlName);
                    if (!mtlName.empty()) {
                        const std::filesystem::path objPath(asset.GetSourcePath());
                        mtllibPath = NormalizePathString(objPath.parent_path() / mtlName);
                    }
                }
            } else if (tag == "usemtl") {
                if (firstUsedMaterialName.empty()) {
                    std::string mtlName;
                    ss >> mtlName;
                    if (!mtlName.empty()) {
                        firstUsedMaterialName = mtlName;
                    }
                }
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
                    if (ParseObjIndexToken(token, key)) {
                        faceKeys.push_back(key);
                        if (key.normal >= 0) {
                            hasAnyFaceNormalRef = true;
                            if (key.normal >= static_cast<int>(normals.size())) {
                                allFaceNormalsValid = false;
                            }
                        } else {
                            allFaceNormalsValid = false;
                        }
                    }
                }
                if (faceKeys.size() < 3) {
                    continue;
                }

                for (size_t i = 1; i + 1 < faceKeys.size(); ++i) {
                    const ObjKey tri[3] = { faceKeys[0], faceKeys[i], faceKeys[i + 1] };
                    for (const ObjKey& key : tri) {
                        auto it = uniqueMap.find(key);
                        if (it != uniqueMap.end()) {
                            indices.push_back(it->second);
                            continue;
                        }

                        if (key.pos < 0 || key.pos >= static_cast<int>(positions.size())) {
                            return false;
                        }

                        VertexStatic3D v{};
                        v.position = positions[static_cast<size_t>(key.pos)];
                        if (key.normal >= 0 && key.normal < static_cast<int>(normals.size())) {
                            v.normal = normals[static_cast<size_t>(key.normal)];
                        }
                        if (key.uv >= 0 && key.uv < static_cast<int>(uvs.size())) {
                            v.u = uvs[static_cast<size_t>(key.uv)][0];
                            v.v = 1.0f - uvs[static_cast<size_t>(key.uv)][1];
                        }

                        const uint32_t newIndex = static_cast<uint32_t>(vertices.size());
                        vertices.push_back(v);
                        uniqueMap[key] = newIndex;
                        indices.push_back(newIndex);
                    }
                }
            }
        }

        const bool useFlatNormalFallback = !hasAnyFaceNormalRef || !allFaceNormalsValid;
        if (useFlatNormalFallback) {
            file.clear();
            file.seekg(0, std::ios::beg);
            vertices.clear();
            indices.clear();
            uniqueMap.clear();

            while (std::getline(file, line)) {
                if (line.size() < 2) continue;
                std::stringstream ss(line);
                std::string tag;
                ss >> tag;
                if (tag != "f") {
                    continue;
                }

                std::vector<ObjKey> faceKeys;
                std::string token;
                while (ss >> token) {
                    ObjKey key{};
                    if (ParseObjIndexToken(token, key)) {
                        faceKeys.push_back(key);
                    }
                }
                if (faceKeys.size() < 3) {
                    continue;
                }

                for (size_t i = 1; i + 1 < faceKeys.size(); ++i) {
                    const ObjKey tri[3] = { faceKeys[0], faceKeys[i], faceKeys[i + 1] };
                    const int p0 = tri[0].pos;
                    const int p1 = tri[1].pos;
                    const int p2 = tri[2].pos;
                    if (p0 < 0 || p0 >= static_cast<int>(positions.size()) ||
                        p1 < 0 || p1 >= static_cast<int>(positions.size()) ||
                        p2 < 0 || p2 >= static_cast<int>(positions.size())) {
                        return false;
                    }

                    const MATH::Vec3 pos0 = positions[static_cast<size_t>(p0)];
                    const MATH::Vec3 pos1 = positions[static_cast<size_t>(p1)];
                    const MATH::Vec3 pos2 = positions[static_cast<size_t>(p2)];
                    MATH::Vec3 faceNormal = MATH::Normalize(MATH::Cross(pos1 - pos0, pos2 - pos0));
                    if (MATH::Length(faceNormal) <= 1e-6f) {
                        faceNormal = { 0.0f, 1.0f, 0.0f };
                    }

                    for (int j = 0; j < 3; ++j) {
                        VertexStatic3D v{};
                        v.position = positions[static_cast<size_t>(tri[j].pos)];
                        v.normal = faceNormal;
                        if (tri[j].uv >= 0 && tri[j].uv < static_cast<int>(uvs.size())) {
                            v.u = uvs[static_cast<size_t>(tri[j].uv)][0];
                            v.v = 1.0f - uvs[static_cast<size_t>(tri[j].uv)][1];
                        }
                        const uint32_t newIndex = static_cast<uint32_t>(vertices.size());
                        vertices.push_back(v);
                        indices.push_back(newIndex);
                    }
                }
            }
        }

        if (vertices.empty() || indices.empty()) {
            return false;
        }

        SanitizeAndFixNormalOrientation(vertices, indices);

        auto mesh = std::make_unique<Mesh>();
        if (!mesh->CreateStatic(SERVICES::gCtx.device, vertices, indices)) {
            return false;
        }

        auto material = std::make_unique<Material>();
        material->SetBaseColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        material->SetBaseColorTexturePath("");
        material->SetBaseColorTextureHandle(-1);

        if (!mtllibPath.empty() && !firstUsedMaterialName.empty()) {
            ObjMaterialInfo materialInfo{};
            if (ParseMtlMaterial(std::filesystem::path(mtllibPath), firstUsedMaterialName, materialInfo)) {
                material->SetBaseColor(materialInfo.baseColor);
                if (!materialInfo.baseColorMapPath.empty()) {
                    material->SetBaseColorTexturePath(materialInfo.baseColorMapPath);
                    const std::string textureName = asset.GetName() + "/baseColor";
                    const int textureHandle = DXTEX::DxTextureManager::LoadTexture(textureName, materialInfo.baseColorMapPath);
                    if (textureHandle >= 0) {
                        material->SetBaseColorTextureHandle(textureHandle);
                    }
                }
            }
        }

        asset.SetMesh(std::move(mesh));
        asset.SetMaterial(std::move(material));
        return true;
    }

} // namespace HIKARI
