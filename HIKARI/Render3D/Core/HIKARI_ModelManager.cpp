#include "Render3D/HIKARI_ModelManager.h"
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
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
            vertices.push_back({ p[i0], n, 0.0f, 0.0f });
            vertices.push_back({ p[i1], n, 1.0f, 0.0f });
            vertices.push_back({ p[i2], n, 1.0f, 1.0f });
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

        auto readAccessor = [&](int accessorIndex, std::vector<float>& out, int expectedComponents, int* outCount = nullptr) -> bool {
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) return false;
            const json& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const int bufferViewIndex = accessor.value("bufferView", -1);
            if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) return false;
            const json& view = bufferViews[static_cast<size_t>(bufferViewIndex)];
            const int bufferIndex = view.value("buffer", -1);
            if (bufferIndex < 0 || bufferIndex >= static_cast<int>(loadedBuffers.size())) return false;

            const int componentType = accessor.value("componentType", 0);
            if (componentType != 5126) return false; // FLOAT only for now

            const std::string type = accessor.value("type", "");
            int actualComponents = 0;
            if (type == "SCALAR") actualComponents = 1;
            else if (type == "VEC2") actualComponents = 2;
            else if (type == "VEC3") actualComponents = 3;
            else if (type == "VEC4") actualComponents = 4;
            if (actualComponents != expectedComponents) return false;

            const int count = accessor.value("count", 0);
            if (count <= 0) return false;
            if (outCount) *outCount = count;

            const size_t accessorOffset = static_cast<size_t>(accessor.value("byteOffset", 0));
            const size_t viewOffset = static_cast<size_t>(view.value("byteOffset", 0));
            const size_t stride = static_cast<size_t>(view.value("byteStride", actualComponents * 4));
            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];

            out.resize(static_cast<size_t>(count) * static_cast<size_t>(actualComponents));
            for (int i = 0; i < count; ++i) {
                const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                if (srcOffset + static_cast<size_t>(actualComponents * 4) > bufferData.size()) {
                    return false;
                }
                std::memcpy(out.data() + static_cast<size_t>(i * actualComponents), bufferData.data() + srcOffset, static_cast<size_t>(actualComponents * 4));
            }
            return true;
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

        const json& mesh0 = meshes[0];
        if (!mesh0.contains("primitives") || !mesh0["primitives"].is_array() || mesh0["primitives"].empty()) {
            return false;
        }
        const json& primitive = mesh0["primitives"][0];
        if (!primitive.contains("attributes") || !primitive["attributes"].is_object()) {
            return false;
        }
        const json& attributes = primitive["attributes"];

        std::vector<float> positions;
        std::vector<float> normals;
        std::vector<float> texcoords;
        int vertexCount = 0;
        if (!readAccessor(attributes.value("POSITION", -1), positions, 3, &vertexCount)) {
            return false;
        }
        if (!readAccessor(attributes.value("NORMAL", -1), normals, 3, nullptr)) {
            normals.assign(static_cast<size_t>(vertexCount) * 3u, 0.0f);
            for (int i = 0; i < vertexCount; ++i) {
                normals[static_cast<size_t>(i) * 3u + 1u] = 1.0f;
            }
        }
        if (!readAccessor(attributes.value("TEXCOORD_0", -1), texcoords, 2, nullptr)) {
            texcoords.assign(static_cast<size_t>(vertexCount) * 2u, 0.0f);
        }

        std::vector<uint32_t> indices;
        if (!readIndices(primitive.value("indices", -1), indices)) {
            indices.resize(static_cast<size_t>(vertexCount));
            for (int i = 0; i < vertexCount; ++i) {
                indices[static_cast<size_t>(i)] = static_cast<uint32_t>(i);
            }
        }

        std::vector<VertexStatic3D> vertices(static_cast<size_t>(vertexCount));
        for (int i = 0; i < vertexCount; ++i) {
            VertexStatic3D v{};
            const size_t p = static_cast<size_t>(i) * 3u;
            const size_t t = static_cast<size_t>(i) * 2u;
            v.position = { positions[p + 0], positions[p + 1], positions[p + 2] };
            v.normal = { normals[p + 0], normals[p + 1], normals[p + 2] };
            v.u = texcoords[t + 0];
            v.v = 1.0f - texcoords[t + 1];
            vertices[static_cast<size_t>(i)] = v;
        }

        auto mesh = std::make_unique<Mesh>();
        if (!mesh->CreateStatic(SERVICES::gCtx.device, vertices, indices)) {
            return false;
        }

        auto material = std::make_unique<Material>();
        material->SetBaseColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        material->SetBaseColorTextureHandle(-1);
        if (root.contains("materials") && root["materials"].is_array() && !root["materials"].empty()) {
            const int materialIndex = primitive.value("material", -1);
            if (materialIndex >= 0 && materialIndex < static_cast<int>(root["materials"].size())) {
                const json& matNode = root["materials"][static_cast<size_t>(materialIndex)];
                if (matNode.contains("pbrMetallicRoughness")) {
                    const json& pbr = matNode["pbrMetallicRoughness"];
                    if (pbr.contains("baseColorFactor") && pbr["baseColorFactor"].is_array() && pbr["baseColorFactor"].size() >= 4) {
                        material->SetBaseColor({
                            pbr["baseColorFactor"][0].get<float>(),
                            pbr["baseColorFactor"][1].get<float>(),
                            pbr["baseColorFactor"][2].get<float>(),
                            pbr["baseColorFactor"][3].get<float>()
                            });
                    }
                    if (pbr.contains("baseColorTexture") && pbr["baseColorTexture"].is_object()) {
                        const int textureIndex = pbr["baseColorTexture"].value("index", -1);
                        if (textureIndex >= 0 && root.contains("textures") && root["textures"].is_array() &&
                            textureIndex < static_cast<int>(root["textures"].size())) {
                            const int imageIndex = root["textures"][static_cast<size_t>(textureIndex)].value("source", -1);
                            if (imageIndex >= 0 && root.contains("images") && root["images"].is_array() &&
                                imageIndex < static_cast<int>(root["images"].size())) {
                                const std::string imageUri = root["images"][static_cast<size_t>(imageIndex)].value("uri", "");
                                if (!imageUri.empty()) {
                                    const std::string texPath = NormalizePathString(gltfPath.parent_path() / imageUri);
                                    material->SetBaseColorTexturePath(texPath);
                                    const int handle = DXTEX::DxTextureManager::LoadTexture(asset.GetName() + "/gltf_baseColor", texPath);
                                    if (handle >= 0) {
                                        material->SetBaseColorTextureHandle(handle);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Fill new CPU-side model representation with minimum viable data.
        asset.nodes.clear();
        asset.meshes.clear();
        asset.materials.clear();
        asset.textures.clear();
        asset.skins.clear();
        asset.animations.clear();
        asset.defaultSceneRootNode = 0;

        MeshAsset meshAsset{};
        meshAsset.name = mesh0.value("name", "Mesh0");
        MeshPrimitive primitiveAsset{};
        primitiveAsset.name = "Primitive0";
        primitiveAsset.layout = VertexLayoutKind::StaticPNTT;
        primitiveAsset.indices = indices;
        primitiveAsset.staticVertices.reserve(vertices.size());
        for (const VertexStatic3D& v : vertices) {
            Vertex3D out{};
            out.position = v.position;
            out.normal = v.normal;
            out.uv0 = { v.u, v.v };
            out.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
            primitiveAsset.staticVertices.push_back(out);
        }
        meshAsset.primitives.push_back(std::move(primitiveAsset));
        asset.meshes.push_back(std::move(meshAsset));

        ModelNode rootNode{};
        rootNode.name = "Root";
        rootNode.meshIndex = 0;
        asset.nodes.push_back(std::move(rootNode));

        MaterialAsset matAsset{};
        matAsset.name = "Material0";
        matAsset.baseColorFactor = material->GetBaseColor();
        asset.materials.push_back(std::move(matAsset));

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
