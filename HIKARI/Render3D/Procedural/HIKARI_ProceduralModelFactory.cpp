#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "Assets/Geometry/HIKARI_ClusteredGeometryCooker.h"
#include "Assets/Geometry/HIKARI_HcmeshFormat.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"

namespace HIKARI::PROCEDURAL {
    namespace {
        struct ProceduralModelKey {
            ProceduralMeshKind kind = ProceduralMeshKind::GridPlane;
            float width = 10.0f;
            float height = 10.0f;
            float depth = 1.0f;
            uint32_t segmentsX = 10;
            uint32_t segmentsY = 10;
            uint32_t segmentsZ = 1;
            uint32_t sphereSlices = 32;
            uint32_t sphereStacks = 16;
            bool doubleSided = false;
            bool generateTangents = true;

            bool operator==(const ProceduralModelKey& rhs) const = default;
        };

        struct KeyHasher {
            size_t operator()(const ProceduralModelKey& key) const noexcept {
                size_t seed = static_cast<size_t>(key.kind);
                auto mix = [&seed](size_t value) {
                    seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                };
                mix(std::hash<float>{}(key.width));
                mix(std::hash<float>{}(key.height));
                mix(std::hash<float>{}(key.depth));
                mix(key.segmentsX);
                mix(key.segmentsY);
                mix(key.segmentsZ);
                mix(key.sphereSlices);
                mix(key.sphereStacks);
                mix(key.doubleSided ? 1u : 0u);
                mix(key.generateTangents ? 1u : 0u);
                return seed;
            }
        };

        std::unordered_map<ProceduralModelKey, std::unique_ptr<ModelAsset>, KeyHasher> gCache;
        std::unordered_map<std::string, std::string> gClusterPathCache;
        ProceduralModelDebugStats gStats{};

        constexpr uint64_t kFnv64OffsetBasis = 14695981039346656037ull;
        constexpr uint64_t kFnv64Prime = 1099511628211ull;

        void HashAppendBytes(uint64_t& hash, const void* data, size_t size) {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i) {
                hash ^= static_cast<uint64_t>(bytes[i]);
                hash *= kFnv64Prime;
            }
        }

        void HashAppendU32(uint64_t& hash, uint32_t value) {
            HashAppendBytes(hash, &value, sizeof(value));
        }

        void HashAppendFloat(uint64_t& hash, float value) {
            uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            HashAppendU32(hash, bits);
        }

        void HashAppendBool(uint64_t& hash, bool value) {
            const uint32_t bits = value ? 1u : 0u;
            HashAppendU32(hash, bits);
        }

        ProceduralModelKey MakeKey(const ProceduralModelSettings& settings) {
            ProceduralModelKey key{};
            key.kind = settings.kind;
            key.width = (std::max)(0.01f, settings.width);
            key.height = (std::max)(0.01f, settings.height);
            key.depth = (std::max)(0.01f, settings.depth);
            key.segmentsX = (std::clamp)(settings.segmentsX, 1u, 512u);
            key.segmentsY = (std::clamp)(settings.segmentsY, 1u, 512u);
            key.segmentsZ = (std::clamp)(settings.segmentsZ, 1u, 64u);
            key.sphereSlices = (std::max)(3u, settings.sphereSlices);
            key.sphereStacks = (std::max)(2u, settings.sphereStacks);
            key.doubleSided = settings.doubleSided;
            key.generateTangents = settings.generateTangents;
            if (key.kind == ProceduralMeshKind::Plane) {
                key.segmentsX = 1;
                key.segmentsY = 1;
            }
            return key;
        }

        uint64_t HashProceduralModelKey(const ProceduralModelKey& key) {
            uint64_t hash = kFnv64OffsetBasis;
            HashAppendU32(hash, static_cast<uint32_t>(key.kind));
            HashAppendFloat(hash, key.width);
            HashAppendFloat(hash, key.height);
            HashAppendFloat(hash, key.depth);
            HashAppendU32(hash, key.segmentsX);
            HashAppendU32(hash, key.segmentsY);
            HashAppendU32(hash, key.segmentsZ);
            HashAppendU32(hash, key.sphereSlices);
            HashAppendU32(hash, key.sphereStacks);
            HashAppendBool(hash, key.doubleSided);
            HashAppendBool(hash, key.generateTangents);
            return hash;
        }

        std::string MakeClusterGuidValue(const ProceduralModelKey& key) {
            std::ostringstream oss;
            oss << "procedural-" << std::hex << std::setw(16) << std::setfill('0')
                << HashProceduralModelKey(key);
            return oss.str();
        }

        std::filesystem::path ResolveClusterCachePath(
            const ProceduralModelKey& key,
            const std::filesystem::path& projectRoot) {

            const std::filesystem::path libraryRoot = projectRoot.empty()
                ? std::filesystem::path("Library")
                : projectRoot / "Library";
            const std::string fileName =
                MakeClusterGuidValue(key) +
                "-hcmesh" +
                std::to_string(ASSETS::GEOMETRY::kHcmeshVersion) +
                ".hcmesh";
            return (libraryRoot / "Generated" / "ProceduralCluster" /
                    fileName).lexically_normal();
        }

        ASSETS::GEOMETRY::ClusterCookSettings MakeProceduralClusterCookSettings() {
            ASSETS::GEOMETRY::ClusterCookSettings settings{};
            settings.maxSurfaceLodCount = 1u;
            settings.buildSurfaceLods = false;
            settings.partitionLargeStaticSurfaces = true;
            settings.surfacePartitionPolicy = ASSETS::GEOMETRY::SurfacePartitionPolicy::SceneStatic;
            return settings;
        }

        bool CookClusteredGeometry(
            const ProceduralModelSettings& settings,
            const ProceduralModelKey& key,
            const std::filesystem::path& outputPath) {

            const ModelAsset* model = GetOrCreateModel(settings);
            if (model == nullptr) {
                HIKARI_LOG_WARN("[ProceduralModelFactory] failed to build procedural model for clustered geometry.");
                return false;
            }

            RENDER3D::CLUSTER::ClusteredGeometryAsset clusteredGeometry{};
            RENDER3D::CLUSTER::ClusteredGeometryBuildReport report{};
            AssetGuid sourceGuid{};
            sourceGuid.value = MakeClusterGuidValue(key);

            if (!ASSETS::GEOMETRY::CookClusteredGeometryFromModel(
                    *model,
                    sourceGuid,
                    MakeProceduralClusterCookSettings(),
                    clusteredGeometry,
                    report)) {
                HIKARI_LOG_WARN("[ProceduralModelFactory] clustered geometry cook failed: " + sourceGuid.value);
                return false;
            }

            std::string message;
            if (!ASSETS::GEOMETRY::WriteHcmeshFile(outputPath, clusteredGeometry, message)) {
                HIKARI_LOG_WARN("[ProceduralModelFactory] clustered geometry write failed: " + message);
                return false;
            }

            HIKARI_LOG_INFO("[ProceduralModelFactory] cooked clustered geometry: " + outputPath.generic_string());
            return true;
        }

        void ExpandBounds(Bounds& bounds, const MATH::Vec3& p, bool& initialized) {
            if (!initialized) {
                bounds.min = p;
                bounds.max = p;
                initialized = true;
                return;
            }
            bounds.min.x = (std::min)(bounds.min.x, p.x);
            bounds.min.y = (std::min)(bounds.min.y, p.y);
            bounds.min.z = (std::min)(bounds.min.z, p.z);
            bounds.max.x = (std::max)(bounds.max.x, p.x);
            bounds.max.y = (std::max)(bounds.max.y, p.y);
            bounds.max.z = (std::max)(bounds.max.z, p.z);
        }

        void FinalizeBounds(MeshPrimitive& primitive, MeshAsset& mesh, ModelAsset& model) {
            bool initialized = false;
            Bounds bounds{};
            for (const Vertex3D& vertex : primitive.staticVertices) {
                ExpandBounds(bounds, vertex.position, initialized);
            }
            primitive.bounds = initialized ? bounds : Bounds{};
            mesh.bounds = primitive.bounds;
            model.bounds = primitive.bounds;
        }

        void BuildGridPlane(const ProceduralModelKey& key, MeshPrimitive& primitive) {
            const uint32_t sx = key.segmentsX;
            const uint32_t sy = key.segmentsY;
            primitive.staticVertices.reserve(static_cast<size_t>(sx + 1u) * static_cast<size_t>(sy + 1u));
            for (uint32_t y = 0; y <= sy; ++y) {
                const float fy = static_cast<float>(y) / static_cast<float>(sy);
                for (uint32_t x = 0; x <= sx; ++x) {
                    const float fx = static_cast<float>(x) / static_cast<float>(sx);
                    Vertex3D vertex{};
                    vertex.position = {
                        -key.width * 0.5f + key.width * fx,
                        0.0f,
                        -key.height * 0.5f + key.height * fy
                    };
                    vertex.normal = { 0.0f, 1.0f, 0.0f };
                    vertex.tangent = key.generateTangents ? MATH::Vec4{ 1.0f, 0.0f, 0.0f, 1.0f } : MATH::Vec4{};
                    vertex.uv0 = { fx, fy };
                    primitive.staticVertices.push_back(vertex);
                }
            }

            primitive.indices.reserve(static_cast<size_t>(sx) * static_cast<size_t>(sy) * 6u);
            for (uint32_t y = 0; y < sy; ++y) {
                for (uint32_t x = 0; x < sx; ++x) {
                    const uint32_t row0 = y * (sx + 1u);
                    const uint32_t row1 = (y + 1u) * (sx + 1u);
                    const uint32_t i0 = row0 + x;
                    const uint32_t i1 = row0 + x + 1u;
                    const uint32_t i2 = row1 + x;
                    const uint32_t i3 = row1 + x + 1u;
                    primitive.indices.insert(primitive.indices.end(), { i0, i2, i1, i1, i2, i3 });
                }
            }
        }

        void AddBoxFace(MeshPrimitive& primitive, MATH::Vec3 a, MATH::Vec3 b, MATH::Vec3 c, MATH::Vec3 d, MATH::Vec3 normal, MATH::Vec4 tangent) {
            const uint32_t base = static_cast<uint32_t>(primitive.staticVertices.size());
            primitive.staticVertices.push_back({ a, normal, tangent, { 0.0f, 1.0f }, {}, { 1, 1, 1, 1 } });
            primitive.staticVertices.push_back({ b, normal, tangent, { 1.0f, 1.0f }, {}, { 1, 1, 1, 1 } });
            primitive.staticVertices.push_back({ c, normal, tangent, { 0.0f, 0.0f }, {}, { 1, 1, 1, 1 } });
            primitive.staticVertices.push_back({ d, normal, tangent, { 1.0f, 0.0f }, {}, { 1, 1, 1, 1 } });
            primitive.indices.insert(primitive.indices.end(), { base, base + 1u, base + 2u, base + 1u, base + 3u, base + 2u });
        }

        void BuildBox(const ProceduralModelKey& key, MeshPrimitive& primitive) {
            const float x = key.width * 0.5f;
            const float y = key.height * 0.5f;
            const float z = key.depth * 0.5f;
            AddBoxFace(primitive, { -x, -y, z }, { x, -y, z }, { -x, y, z }, { x, y, z }, { 0, 0, 1 }, { 1, 0, 0, 1 });
            AddBoxFace(primitive, { x, -y, -z }, { -x, -y, -z }, { x, y, -z }, { -x, y, -z }, { 0, 0, -1 }, { -1, 0, 0, 1 });
            AddBoxFace(primitive, { -x, -y, -z }, { -x, -y, z }, { -x, y, -z }, { -x, y, z }, { -1, 0, 0 }, { 0, 0, 1, 1 });
            AddBoxFace(primitive, { x, -y, z }, { x, -y, -z }, { x, y, z }, { x, y, -z }, { 1, 0, 0 }, { 0, 0, -1, 1 });
            AddBoxFace(primitive, { -x, y, z }, { x, y, z }, { -x, y, -z }, { x, y, -z }, { 0, 1, 0 }, { 1, 0, 0, 1 });
            AddBoxFace(primitive, { -x, -y, -z }, { x, -y, -z }, { -x, -y, z }, { x, -y, z }, { 0, -1, 0 }, { 1, 0, 0, 1 });
        }

        std::unique_ptr<ModelAsset> BuildModel(const ProceduralModelKey& key) {
            auto model = std::make_unique<ModelAsset>();
            model->SetName("procedural_model");
            model->SetSourcePath("procedural");
            model->SetState(ModelAsset::State::Loaded);

            MaterialAsset material{};
            material.name = "Procedural Default";
            material.shaderProfileId = "PBR";
            material.doubleSided = key.doubleSided;
            material.baseColorFactor = { 1, 1, 1, 1 };
            model->materials.push_back(material);

            MeshPrimitive primitive{};
            primitive.name = "Procedural Primitive";
            primitive.layout = VertexLayoutKind::StaticPNTT;
            primitive.materialIndex = 0;
            if (key.kind == ProceduralMeshKind::Box || key.kind == ProceduralMeshKind::Sphere) {
                BuildBox(key, primitive);
            } else {
                BuildGridPlane(key, primitive);
            }

            MeshAsset mesh{};
            mesh.name = "Procedural Mesh";
            FinalizeBounds(primitive, mesh, *model);
            gStats.generatedVertexCount += primitive.staticVertices.size();
            gStats.generatedIndexCount += primitive.indices.size();
            mesh.primitives.push_back(std::move(primitive));
            model->meshes.push_back(std::move(mesh));

            ModelNode node{};
            node.name = "Procedural Root";
            node.meshIndex = 0;
            model->nodes.push_back(node);
            model->defaultSceneRootNode = 0;
            ++gStats.generatedModelCount;
            return model;
        }
    }

    const ModelAsset* GetOrCreateModel(const ProceduralModelSettings& settings) {
        const ProceduralModelKey key = MakeKey(settings);
        auto found = gCache.find(key);
        if (found != gCache.end()) {
            ++gStats.cacheHitCount;
            return found->second.get();
        }
        ++gStats.cacheMissCount;
        auto model = BuildModel(key);
        const ModelAsset* raw = model.get();
        gCache.emplace(key, std::move(model));
        return raw;
    }

    std::string GetOrCreateClusteredGeometryPath(
        const ProceduralModelSettings& settings,
        const std::filesystem::path& projectRoot) {

        const ProceduralModelKey key = MakeKey(settings);
        const std::filesystem::path path = ResolveClusterCachePath(key, projectRoot);
        const std::string normalizedPath = path.generic_string();
        const auto cached = gClusterPathCache.find(normalizedPath);
        if (cached != gClusterPathCache.end()) {
            return cached->second;
        }

        std::error_code existsError{};
        if (std::filesystem::exists(path, existsError) && !existsError) {
            gClusterPathCache.emplace(normalizedPath, normalizedPath);
            return normalizedPath;
        }

        if (!CookClusteredGeometry(settings, key, path)) {
            return {};
        }

        gClusterPathCache.emplace(normalizedPath, normalizedPath);
        return normalizedPath;
    }

    void ClearCache() {
        gCache.clear();
        gClusterPathCache.clear();
        gStats = {};
    }

    const ProceduralModelDebugStats& GetDebugStats() {
        return gStats;
    }
} // namespace HIKARI::PROCEDURAL
