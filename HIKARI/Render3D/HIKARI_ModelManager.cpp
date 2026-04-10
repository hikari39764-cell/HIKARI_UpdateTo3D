#include "HIKARI_ModelManager.h"
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include "HIKARI_Services.h"

namespace HIKARI {

    namespace {
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

        if (vertices.empty() || indices.empty()) {
            return false;
        }

        auto mesh = std::make_unique<Mesh>();
        if (!mesh->CreateStatic(SERVICES::gCtx.device, vertices, indices)) {
            return false;
        }

        auto material = std::make_unique<Material>();
        material->SetBaseColor({ 1.0f, 1.0f, 1.0f, 1.0f });

        asset.SetMesh(std::move(mesh));
        asset.SetMaterial(std::move(material));
        return true;
    }

} // namespace HIKARI
