#include "HIKARI_Assets.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <json.hpp>

#include "Gfx/HIKARI_GpuResources.h"

namespace HIKARI::ASSET {

    namespace {
        struct ObjIndexKey {
            int p = -1;
            int t = -1;
            int n = -1;
            bool operator==(const ObjIndexKey& rhs) const {
                return p == rhs.p && t == rhs.t && n == rhs.n;
            }
        };

        struct ObjIndexKeyHasher {
            size_t operator()(const ObjIndexKey& key) const noexcept {
                size_t h = static_cast<size_t>(key.p * 73856093);
                h ^= static_cast<size_t>(key.t * 19349663);
                h ^= static_cast<size_t>(key.n * 83492791);
                return h;
            }
        };

        struct ObjRange {
            std::string material;
            uint32_t indexOffset = 0;
            uint32_t indexCount = 0;
        };

        static std::string Trim(std::string s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
            size_t i = 0;
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            return s.substr(i);
        }

        static std::vector<std::string> Split(const std::string& line) {
            std::istringstream iss(line);
            std::vector<std::string> out;
            std::string tok;
            while (iss >> tok) out.push_back(tok);
            return out;
        }

        static int FixIndex(int idx, int count) {
            if (idx > 0) return idx - 1;
            if (idx < 0) return count + idx;
            return -1;
        }

        static void AccumulateTangents(MeshAsset& mesh) {
            if (mesh.indices.size() < 3 || mesh.vertices.empty()) {
                return;
            }
            struct TanAccum { float x = 0, y = 0, z = 0; float bx = 0, by = 0, bz = 0; };
            std::vector<TanAccum> tan(mesh.vertices.size());
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const uint32_t i0 = mesh.indices[i + 0];
                const uint32_t i1 = mesh.indices[i + 1];
                const uint32_t i2 = mesh.indices[i + 2];
                if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) continue;
                const auto& v0 = mesh.vertices[i0];
                const auto& v1 = mesh.vertices[i1];
                const auto& v2 = mesh.vertices[i2];

                const float x1 = v1.px - v0.px, x2 = v2.px - v0.px;
                const float y1 = v1.py - v0.py, y2 = v2.py - v0.py;
                const float z1 = v1.pz - v0.pz, z2 = v2.pz - v0.pz;
                const float s1 = v1.u - v0.u, s2 = v2.u - v0.u;
                const float t1 = v1.v - v0.v, t2 = v2.v - v0.v;
                const float det = s1 * t2 - s2 * t1;
                if (std::abs(det) < 1e-8f) continue;
                const float r = 1.0f / det;
                const float tx = (x1 * t2 - x2 * t1) * r;
                const float ty = (y1 * t2 - y2 * t1) * r;
                const float tz = (z1 * t2 - z2 * t1) * r;
                const float bx = (x2 * s1 - x1 * s2) * r;
                const float by = (y2 * s1 - y1 * s2) * r;
                const float bz = (z2 * s1 - z1 * s2) * r;
                for (uint32_t vi : { i0, i1, i2 }) {
                    tan[vi].x += tx; tan[vi].y += ty; tan[vi].z += tz;
                    tan[vi].bx += bx; tan[vi].by += by; tan[vi].bz += bz;
                }
            }

            for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                auto& v = mesh.vertices[i];
                const auto& a = tan[i];
                float tx = a.x, ty = a.y, tz = a.z;
                const float nx = v.nx, ny = v.ny, nz = v.nz;
                const float ndott = nx * tx + ny * ty + nz * tz;
                tx -= nx * ndott; ty -= ny * ndott; tz -= nz * ndott;
                const float len = std::sqrt(tx * tx + ty * ty + tz * tz);
                if (len > 1e-6f) { tx /= len; ty /= len; tz /= len; } else { tx = 1; ty = 0; tz = 0; }
                const float cx = ny * tz - nz * ty;
                const float cy = nz * tx - nx * tz;
                const float cz = nx * ty - ny * tx;
                const float handed = (cx * a.bx + cy * a.by + cz * a.bz) < 0.0f ? -1.0f : 1.0f;
                v.tx = tx; v.ty = ty; v.tz = tz; v.tw = handed;
            }
        }

        static AssetHandle<MaterialAsset> MakeDefaultMaterial(AssetRegistry& registry, const std::string& name) {
            MaterialAsset material{};
            material.id = registry.AllocateId();
            material.state = AssetState::Ready;
            material.name = name;
            auto handle = AssetHandle<MaterialAsset>{ material.id };
            registry.materials_.emplace(material.id, std::move(material));
            return handle;
        }

        static AssetHandle<MeshAsset> PushMesh(AssetRegistry& registry, MeshAsset&& mesh) {
            mesh.id = registry.AllocateId();
            mesh.state = AssetState::Ready;
            mesh.gpuMeshId = GpuResources::UploadStaticMesh(mesh.name, mesh.vertices, mesh.indices);
            auto handle = AssetHandle<MeshAsset>{ mesh.id };
            registry.meshes_.emplace(mesh.id, std::move(mesh));
            return handle;
        }

        static bool LoadObjMtl(AssetRegistry& registry, const std::filesystem::path& objPath, ModelAsset& model) {
            std::ifstream ifs(objPath);
            if (!ifs.is_open()) return false;

            std::vector<std::array<float, 3>> positions;
            std::vector<std::array<float, 3>> normals;
            std::vector<std::array<float, 2>> uvs;
            MeshAsset mesh{};
            mesh.name = objPath.filename().string();
            std::unordered_map<ObjIndexKey, uint32_t, ObjIndexKeyHasher> dedup;
            std::vector<ObjRange> ranges;
            ranges.push_back({});
            ranges.back().indexOffset = 0;

            std::filesystem::path mtlPath;
            std::string line;
            while (std::getline(ifs, line)) {
                line = Trim(line);
                if (line.empty() || line[0] == '#') continue;
                auto parts = Split(line);
                if (parts.empty()) continue;
                if (parts[0] == "v" && parts.size() >= 4) {
                    positions.push_back({ std::stof(parts[1]), std::stof(parts[2]), std::stof(parts[3]) });
                } else if (parts[0] == "vn" && parts.size() >= 4) {
                    normals.push_back({ std::stof(parts[1]), std::stof(parts[2]), std::stof(parts[3]) });
                } else if (parts[0] == "vt" && parts.size() >= 3) {
                    uvs.push_back({ std::stof(parts[1]), 1.0f - std::stof(parts[2]) });
                } else if (parts[0] == "mtllib" && parts.size() >= 2) {
                    mtlPath = objPath.parent_path() / parts[1];
                } else if (parts[0] == "usemtl" && parts.size() >= 2) {
                    if (ranges.back().indexCount == 0 && ranges.back().material.empty()) {
                        ranges.back().material = parts[1];
                    } else {
                        ranges.push_back({ parts[1], static_cast<uint32_t>(mesh.indices.size()), 0u });
                    }
                } else if (parts[0] == "f" && parts.size() >= 4) {
                    std::vector<uint32_t> face;
                    face.reserve(parts.size() - 1);
                    for (size_t i = 1; i < parts.size(); ++i) {
                        ObjIndexKey key{};
                        std::string vtx = parts[i];
                        size_t p1 = vtx.find('/');
                        size_t p2 = (p1 == std::string::npos) ? std::string::npos : vtx.find('/', p1 + 1);
                        key.p = FixIndex(std::stoi(vtx.substr(0, p1)), static_cast<int>(positions.size()));
                        if (p1 != std::string::npos && p2 > p1 + 1) key.t = FixIndex(std::stoi(vtx.substr(p1 + 1, p2 - p1 - 1)), static_cast<int>(uvs.size()));
                        if (p2 != std::string::npos && p2 + 1 < vtx.size()) key.n = FixIndex(std::stoi(vtx.substr(p2 + 1)), static_cast<int>(normals.size()));

                        auto it = dedup.find(key);
                        if (it == dedup.end()) {
                            MeshAsset::Vertex out{};
                            if (key.p >= 0 && key.p < static_cast<int>(positions.size())) {
                                const auto& p = positions[static_cast<size_t>(key.p)];
                                out.px = p[0]; out.py = p[1]; out.pz = p[2];
                            }
                            if (key.n >= 0 && key.n < static_cast<int>(normals.size())) {
                                const auto& n = normals[static_cast<size_t>(key.n)];
                                out.nx = n[0]; out.ny = n[1]; out.nz = n[2];
                            }
                            if (key.t >= 0 && key.t < static_cast<int>(uvs.size())) {
                                const auto& uv = uvs[static_cast<size_t>(key.t)];
                                out.u = uv[0]; out.v = uv[1];
                            }
                            uint32_t idx = static_cast<uint32_t>(mesh.vertices.size());
                            mesh.vertices.push_back(out);
                            dedup.emplace(key, idx);
                            face.push_back(idx);
                        } else {
                            face.push_back(it->second);
                        }
                    }
                    for (size_t i = 1; i + 1 < face.size(); ++i) {
                        mesh.indices.push_back(face[0]);
                        mesh.indices.push_back(face[i]);
                        mesh.indices.push_back(face[i + 1]);
                        ranges.back().indexCount += 3;
                    }
                }
            }

            std::unordered_map<std::string, AssetHandle<MaterialAsset>> materials;
            if (!mtlPath.empty()) {
                std::ifstream mtl(mtlPath);
                if (mtl.is_open()) {
                    MaterialAsset current{};
                    bool hasCurrent = false;
                    std::string mline;
                    auto flush = [&]() {
                        if (!hasCurrent) return;
                        current.id = registry.AllocateId();
                        current.state = AssetState::Ready;
                        materials[current.name] = AssetHandle<MaterialAsset>{ current.id };
                        registry.materials_.emplace(current.id, current);
                    };
                    while (std::getline(mtl, mline)) {
                        mline = Trim(mline);
                        if (mline.empty() || mline[0] == '#') continue;
                        auto parts = Split(mline);
                        if (parts.empty()) continue;
                        if (parts[0] == "newmtl" && parts.size() >= 2) {
                            flush();
                            current = {};
                            current.name = parts[1];
                            hasCurrent = true;
                        } else if (parts[0] == "Kd" && parts.size() >= 4 && hasCurrent) {
                            current.baseColorFactor = { std::stof(parts[1]), std::stof(parts[2]), std::stof(parts[3]), 1.0f };
                        } else if (parts[0] == "map_Kd" && parts.size() >= 2 && hasCurrent) {
                            current.baseColorTexture = registry.GetOrLoadTexture((objPath.parent_path() / parts[1]).string());
                        }
                    }
                    flush();
                }
            }

            if (mesh.vertices.empty() || mesh.indices.empty()) return false;
            AccumulateTangents(mesh);
            auto meshHandle = PushMesh(registry, std::move(mesh));
            for (const ObjRange& range : ranges) {
                ModelAsset::Primitive primitive{};
                primitive.mesh = meshHandle;
                auto it = materials.find(range.material);
                primitive.material = (it != materials.end()) ? it->second : MakeDefaultMaterial(registry, "obj_default");
                model.primitives.push_back(primitive);
            }
            if (model.primitives.empty()) {
                ModelAsset::Primitive primitive{};
                primitive.mesh = meshHandle;
                primitive.material = MakeDefaultMaterial(registry, "obj_default");
                model.primitives.push_back(primitive);
            }
            return true;
        }

        static std::vector<uint8_t> ReadAllBinary(const std::filesystem::path& path) {
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs.is_open()) return {};
            ifs.seekg(0, std::ios::end);
            const auto sz = static_cast<size_t>(ifs.tellg());
            ifs.seekg(0, std::ios::beg);
            std::vector<uint8_t> bytes(sz);
            if (sz > 0) ifs.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(sz));
            return bytes;
        }

        static bool DecodeGlb(const std::vector<uint8_t>& bytes, std::string& outJson, std::vector<uint8_t>& outBin) {
            if (bytes.size() < 20) return false;
            auto readU32 = [&](size_t off) -> uint32_t {
                return static_cast<uint32_t>(bytes[off]) |
                    (static_cast<uint32_t>(bytes[off + 1]) << 8) |
                    (static_cast<uint32_t>(bytes[off + 2]) << 16) |
                    (static_cast<uint32_t>(bytes[off + 3]) << 24);
            };
            const uint32_t magic = readU32(0);
            if (magic != 0x46546C67u) return false;
            size_t off = 12;
            while (off + 8 <= bytes.size()) {
                const uint32_t len = readU32(off + 0);
                const uint32_t type = readU32(off + 4);
                off += 8;
                if (off + len > bytes.size()) return false;
                if (type == 0x4E4F534Au) {
                    outJson.assign(reinterpret_cast<const char*>(bytes.data() + off), reinterpret_cast<const char*>(bytes.data() + off + len));
                } else if (type == 0x004E4942u) {
                    outBin.assign(bytes.begin() + static_cast<std::ptrdiff_t>(off), bytes.begin() + static_cast<std::ptrdiff_t>(off + len));
                }
                off += len;
            }
            return !outJson.empty();
        }

        static DirectX::XMFLOAT4X4 NodeLocalMatrix(const nlohmann::json& node) {
            if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() == 16) {
                DirectX::XMFLOAT4X4 m{};
                for (int i = 0; i < 16; ++i) {
                    reinterpret_cast<float*>(&m)[i] = node["matrix"][i].get<float>();
                }
                return m;
            }
            float tx = 0, ty = 0, tz = 0;
            float qx = 0, qy = 0, qz = 0, qw = 1;
            float sx = 1, sy = 1, sz = 1;
            if (node.contains("translation") && node["translation"].is_array() && node["translation"].size() >= 3) {
                tx = node["translation"][0].get<float>(); ty = node["translation"][1].get<float>(); tz = node["translation"][2].get<float>();
            }
            if (node.contains("rotation") && node["rotation"].is_array() && node["rotation"].size() >= 4) {
                qx = node["rotation"][0].get<float>(); qy = node["rotation"][1].get<float>(); qz = node["rotation"][2].get<float>(); qw = node["rotation"][3].get<float>();
            }
            if (node.contains("scale") && node["scale"].is_array() && node["scale"].size() >= 3) {
                sx = node["scale"][0].get<float>(); sy = node["scale"][1].get<float>(); sz = node["scale"][2].get<float>();
            }
            DirectX::XMMATRIX m = DirectX::XMMatrixScaling(sx, sy, sz) *
                DirectX::XMMatrixRotationQuaternion(DirectX::XMVectorSet(qx, qy, qz, qw)) *
                DirectX::XMMatrixTranslation(tx, ty, tz);
            DirectX::XMFLOAT4X4 out{};
            DirectX::XMStoreFloat4x4(&out, m);
            return out;
        }

        static bool LoadGltfStatic(AssetRegistry& registry, const std::filesystem::path& path, ModelAsset& model) {
            std::string jsonText;
            std::vector<uint8_t> glbBin;
            if (path.extension() == ".glb") {
                const auto bytes = ReadAllBinary(path);
                if (!DecodeGlb(bytes, jsonText, glbBin)) return false;
            } else {
                std::ifstream ifs(path);
                if (!ifs.is_open()) return false;
                std::stringstream ss;
                ss << ifs.rdbuf();
                jsonText = ss.str();
            }
            nlohmann::json doc = nlohmann::json::parse(jsonText, nullptr, false);
            if (doc.is_discarded()) return false;

            std::vector<std::vector<uint8_t>> buffers;
            if (doc.contains("buffers") && doc["buffers"].is_array()) {
                for (const auto& b : doc["buffers"]) {
                    if (b.contains("uri")) {
                        buffers.push_back(ReadAllBinary(path.parent_path() / b["uri"].get<std::string>()));
                    } else {
                        buffers.push_back(glbBin);
                    }
                }
            }

            auto accessorRead = [&](int accessorIndex, std::vector<float>& out, int& outCompCount) -> bool {
                if (!doc.contains("accessors") || accessorIndex < 0 || accessorIndex >= static_cast<int>(doc["accessors"].size())) return false;
                const auto& accessor = doc["accessors"][accessorIndex];
                const int viewIndex = accessor.value("bufferView", -1);
                if (viewIndex < 0 || !doc.contains("bufferViews") || viewIndex >= static_cast<int>(doc["bufferViews"].size())) return false;
                const auto& view = doc["bufferViews"][viewIndex];
                const int bufferIndex = view.value("buffer", -1);
                if (bufferIndex < 0 || bufferIndex >= static_cast<int>(buffers.size())) return false;
                const auto& data = buffers[bufferIndex];
                const size_t viewOffset = view.value("byteOffset", 0);
                const size_t accessorOffset = accessor.value("byteOffset", 0);
                const size_t offset = viewOffset + accessorOffset;
                const int count = accessor.value("count", 0);
                const std::string type = accessor.value("type", std::string("SCALAR"));
                outCompCount = (type == "VEC2") ? 2 : (type == "VEC3") ? 3 : (type == "VEC4") ? 4 : 1;
                const int componentType = accessor.value("componentType", 5126);
                if (componentType != 5126) return false;
                const size_t stride = view.value("byteStride", static_cast<int>(sizeof(float) * outCompCount));
                out.resize(static_cast<size_t>(count * outCompCount));
                for (int i = 0; i < count; ++i) {
                    const size_t src = offset + static_cast<size_t>(i) * stride;
                    if (src + sizeof(float) * static_cast<size_t>(outCompCount) > data.size()) return false;
                    std::memcpy(out.data() + static_cast<size_t>(i) * outCompCount, data.data() + src, sizeof(float) * static_cast<size_t>(outCompCount));
                }
                return true;
            };

            auto readIndices = [&](int accessorIndex, std::vector<uint32_t>& out) -> bool {
                if (!doc.contains("accessors") || accessorIndex < 0 || accessorIndex >= static_cast<int>(doc["accessors"].size())) return false;
                const auto& accessor = doc["accessors"][accessorIndex];
                const int viewIndex = accessor.value("bufferView", -1);
                if (viewIndex < 0 || !doc.contains("bufferViews") || viewIndex >= static_cast<int>(doc["bufferViews"].size())) return false;
                const auto& view = doc["bufferViews"][viewIndex];
                const int bufferIndex = view.value("buffer", -1);
                if (bufferIndex < 0 || bufferIndex >= static_cast<int>(buffers.size())) return false;
                const auto& data = buffers[bufferIndex];
                const size_t offset = view.value("byteOffset", 0) + accessor.value("byteOffset", 0);
                const int count = accessor.value("count", 0);
                const int componentType = accessor.value("componentType", 5125);
                out.resize(static_cast<size_t>(count));
                for (int i = 0; i < count; ++i) {
                    const size_t src = offset + static_cast<size_t>(i) * ((componentType == 5123) ? 2 : 4);
                    if (componentType == 5123) {
                        if (src + 2 > data.size()) return false;
                        const uint16_t v = static_cast<uint16_t>(data[src] | (data[src + 1] << 8));
                        out[static_cast<size_t>(i)] = v;
                    } else {
                        if (src + 4 > data.size()) return false;
                        out[static_cast<size_t>(i)] = static_cast<uint32_t>(data[src]) |
                            (static_cast<uint32_t>(data[src + 1]) << 8) |
                            (static_cast<uint32_t>(data[src + 2]) << 16) |
                            (static_cast<uint32_t>(data[src + 3]) << 24);
                    }
                }
                return true;
            };

            std::vector<AssetHandle<MaterialAsset>> gltfMaterials;
            if (doc.contains("materials") && doc["materials"].is_array()) {
                for (const auto& m : doc["materials"]) {
                    MaterialAsset material{};
                    material.id = registry.AllocateId();
                    material.state = AssetState::Ready;
                    material.name = m.value("name", std::string("gltf_mat"));
                    material.doubleSided = m.value("doubleSided", false);
                    const std::string alpha = m.value("alphaMode", std::string("OPAQUE"));
                    material.alphaMode = (alpha == "BLEND") ? AlphaMode::Blend : (alpha == "MASK") ? AlphaMode::Mask : AlphaMode::Opaque;
                    material.alphaCutoff = m.value("alphaCutoff", 0.5f);
                    if (m.contains("emissiveFactor") && m["emissiveFactor"].is_array() && m["emissiveFactor"].size() >= 3) {
                        material.emissiveFactor = { m["emissiveFactor"][0].get<float>(), m["emissiveFactor"][1].get<float>(), m["emissiveFactor"][2].get<float>() };
                    }
                    if (m.contains("normalTexture") && m["normalTexture"].contains("scale")) {
                        material.normalScale = m["normalTexture"]["scale"].get<float>();
                    }
                    if (m.contains("occlusionTexture") && m["occlusionTexture"].contains("strength")) {
                        material.occlusionStrength = m["occlusionTexture"]["strength"].get<float>();
                    }
                    if (m.contains("pbrMetallicRoughness")) {
                        const auto& pbr = m["pbrMetallicRoughness"];
                        if (pbr.contains("baseColorFactor") && pbr["baseColorFactor"].is_array() && pbr["baseColorFactor"].size() >= 4) {
                            material.baseColorFactor = { pbr["baseColorFactor"][0].get<float>(), pbr["baseColorFactor"][1].get<float>(), pbr["baseColorFactor"][2].get<float>(), pbr["baseColorFactor"][3].get<float>() };
                        }
                        material.metallicFactor = pbr.value("metallicFactor", 1.0f);
                        material.roughnessFactor = pbr.value("roughnessFactor", 1.0f);
                    }

                    auto getTexturePath = [&](int texInfoIndex) -> std::string {
                        if (!doc.contains("textures") || texInfoIndex < 0 || texInfoIndex >= static_cast<int>(doc["textures"].size())) return {};
                        const int imageIndex = doc["textures"][texInfoIndex].value("source", -1);
                        if (!doc.contains("images") || imageIndex < 0 || imageIndex >= static_cast<int>(doc["images"].size())) return {};
                        if (!doc["images"][imageIndex].contains("uri")) return {};
                        return (path.parent_path() / doc["images"][imageIndex]["uri"].get<std::string>()).string();
                    };

                    if (m.contains("pbrMetallicRoughness") && m["pbrMetallicRoughness"].contains("baseColorTexture")) {
                        const int ti = m["pbrMetallicRoughness"]["baseColorTexture"].value("index", -1);
                        material.baseColorTexture = registry.GetOrLoadTexture(getTexturePath(ti));
                    }
                    if (m.contains("normalTexture")) {
                        const int ti = m["normalTexture"].value("index", -1);
                        material.normalTexture = registry.GetOrLoadTexture(getTexturePath(ti));
                    }
                    if (m.contains("pbrMetallicRoughness") && m["pbrMetallicRoughness"].contains("metallicRoughnessTexture")) {
                        const int ti = m["pbrMetallicRoughness"]["metallicRoughnessTexture"].value("index", -1);
                        material.ormTexture = registry.GetOrLoadTexture(getTexturePath(ti));
                    }
                    if (m.contains("emissiveTexture")) {
                        const int ti = m["emissiveTexture"].value("index", -1);
                        material.emissiveTexture = registry.GetOrLoadTexture(getTexturePath(ti));
                    }

                    gltfMaterials.push_back(AssetHandle<MaterialAsset>{ material.id });
                    registry.materials_.emplace(material.id, std::move(material));
                }
            }

            if (doc.contains("nodes") && doc["nodes"].is_array()) {
                for (const auto& node : doc["nodes"]) {
                    const int meshIndex = node.value("mesh", -1);
                    if (meshIndex < 0 || !doc.contains("meshes") || meshIndex >= static_cast<int>(doc["meshes"].size())) continue;
                    const auto& gltfMesh = doc["meshes"][meshIndex];
                    if (!gltfMesh.contains("primitives") || !gltfMesh["primitives"].is_array()) continue;
                    const auto local = NodeLocalMatrix(node);
                    for (const auto& prim : gltfMesh["primitives"]) {
                        const int posAccessor = prim["attributes"].value("POSITION", -1);
                        if (posAccessor < 0) continue;
                        std::vector<float> pos; int posComp = 0;
                        if (!accessorRead(posAccessor, pos, posComp) || posComp != 3) continue;

                        std::vector<float> normal; int nComp = 0;
                        accessorRead(prim["attributes"].value("NORMAL", -1), normal, nComp);
                        std::vector<float> uv; int uvComp = 0;
                        accessorRead(prim["attributes"].value("TEXCOORD_0", -1), uv, uvComp);
                        std::vector<float> tangent; int tComp = 0;
                        accessorRead(prim["attributes"].value("TANGENT", -1), tangent, tComp);

                        MeshAsset mesh{};
                        mesh.name = model.name + "_prim";
                        const size_t vcount = pos.size() / 3;
                        mesh.vertices.resize(vcount);
                        for (size_t i = 0; i < vcount; ++i) {
                            auto& v = mesh.vertices[i];
                            v.px = pos[i * 3 + 0]; v.py = pos[i * 3 + 1]; v.pz = pos[i * 3 + 2];
                            if (normal.size() >= (i + 1) * 3) {
                                v.nx = normal[i * 3 + 0]; v.ny = normal[i * 3 + 1]; v.nz = normal[i * 3 + 2];
                            }
                            if (uv.size() >= (i + 1) * 2) {
                                v.u = uv[i * 2 + 0]; v.v = uv[i * 2 + 1];
                            }
                            if (tangent.size() >= (i + 1) * 4) {
                                v.tx = tangent[i * 4 + 0]; v.ty = tangent[i * 4 + 1]; v.tz = tangent[i * 4 + 2]; v.tw = tangent[i * 4 + 3];
                            }
                        }
                        if (prim.contains("indices")) {
                            readIndices(prim["indices"].get<int>(), mesh.indices);
                        } else {
                            mesh.indices.resize(vcount);
                            for (size_t i = 0; i < vcount; ++i) mesh.indices[i] = static_cast<uint32_t>(i);
                        }
                        if (tangent.empty()) {
                            AccumulateTangents(mesh);
                        }
                        auto meshHandle = PushMesh(registry, std::move(mesh));
                        ModelAsset::Primitive primitive{};
                        primitive.mesh = meshHandle;
                        const int matIndex = prim.value("material", -1);
                        primitive.material = (matIndex >= 0 && matIndex < static_cast<int>(gltfMaterials.size())) ? gltfMaterials[matIndex] : MakeDefaultMaterial(registry, "gltf_default");
                        primitive.localTransform = local;
                        model.primitives.push_back(primitive);
                    }
                }
            }

            return !model.primitives.empty();
        }

        static bool BuildBuiltin(AssetRegistry& registry, ModelAsset& model, const std::string& sourcePath) {
            MeshAsset mesh{};
            mesh.name = sourcePath;
            if (sourcePath == "builtin:plane") {
                mesh.vertices = {
                    {-0.5f, 0.0f, -0.5f, 0,1,0, 1,0,0,1, 0,1},
                    { 0.5f, 0.0f, -0.5f, 0,1,0, 1,0,0,1, 1,1},
                    { 0.5f, 0.0f,  0.5f, 0,1,0, 1,0,0,1, 1,0},
                    {-0.5f, 0.0f,  0.5f, 0,1,0, 1,0,0,1, 0,0},
                };
                mesh.indices = { 0, 1, 2, 0, 2, 3 };
            } else if (sourcePath == "builtin:cube") {
                const std::array<std::array<float, 3>, 8> p = {{
                    {-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},
                    {-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},{0.5f,0.5f,0.5f},{-0.5f,0.5f,0.5f}
                }};
                mesh.vertices.reserve(24);
                auto addFace = [&](int a, int b, int c, int d, float nx, float ny, float nz) {
                    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back({ p[a][0],p[a][1],p[a][2], nx,ny,nz, 1,0,0,1, 0,1 });
                    mesh.vertices.push_back({ p[b][0],p[b][1],p[b][2], nx,ny,nz, 1,0,0,1, 1,1 });
                    mesh.vertices.push_back({ p[c][0],p[c][1],p[c][2], nx,ny,nz, 1,0,0,1, 1,0 });
                    mesh.vertices.push_back({ p[d][0],p[d][1],p[d][2], nx,ny,nz, 1,0,0,1, 0,0 });
                    mesh.indices.insert(mesh.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
                };
                addFace(0, 1, 2, 3, 0, 0, -1);
                addFace(5, 4, 7, 6, 0, 0, 1);
                addFace(4, 0, 3, 7, -1, 0, 0);
                addFace(1, 5, 6, 2, 1, 0, 0);
                addFace(3, 2, 6, 7, 0, 1, 0);
                addFace(4, 5, 1, 0, 0, -1, 0);
            } else {
                return false;
            }
            AccumulateTangents(mesh);
            const auto meshHandle = PushMesh(registry, std::move(mesh));
            ModelAsset::Primitive primitive{};
            primitive.mesh = meshHandle;
            primitive.material = MakeDefaultMaterial(registry, sourcePath + "_mat");
            model.primitives.push_back(primitive);
            return true;
        }
    }

    bool ImportModelStatic(AssetRegistry& registry, ModelAsset& model, const std::string& sourcePath) {
        if (sourcePath.rfind("builtin:", 0) == 0) {
            return BuildBuiltin(registry, model, sourcePath);
        }

        const std::filesystem::path path = sourcePath;
        const std::string ext = path.has_extension() ? path.extension().string() : std::string{};
        if (ext == ".obj") {
            return LoadObjMtl(registry, path, model);
        }
        if (ext == ".gltf" || ext == ".glb") {
            return LoadGltfStatic(registry, path, model);
        }
        return false;
    }

} // namespace HIKARI::ASSET
