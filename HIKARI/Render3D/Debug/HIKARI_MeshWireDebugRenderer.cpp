#include "Render3D/Debug/HIKARI_MeshWireDebugRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"

namespace HIKARI::MESHWIREDEBUG {
    namespace {
        struct WireLine {
            MATH::Vec3 a{};
            MATH::Vec3 b{};
        };

        struct CachedPrimitiveWire {
            std::vector<WireLine> lines;
            bool truncated = false;
            uint32_t lineLimit = 0;
        };

        std::unordered_map<const MeshPrimitive*, CachedPrimitiveWire> gCache;
        MeshWireDebugStats gStats{};
        bool gSkinnedWarningEmitted = false;

        MATH::Vec3 TransformPoint(const MATH::Mat4& m, const MATH::Vec3& p) {
            const MATH::Vec4 out = m.TransformPoint({ p.x, p.y, p.z, 1.0f });
            if (std::abs(out.w) > 1e-6f) {
                return { out.x / out.w, out.y / out.w, out.z / out.w };
            }
            return { out.x, out.y, out.z };
        }

        uint64_t MakeEdge(uint32_t a, uint32_t b) {
            const uint32_t lo = (std::min)(a, b);
            const uint32_t hi = (std::max)(a, b);
            return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
        }

        const std::vector<Vertex3D>* GetStaticVertices(const MeshPrimitive& primitive) {
            if (!primitive.staticVertices.empty()) {
                return &primitive.staticVertices;
            }
            return nullptr;
        }

        const CachedPrimitiveWire& GetOrCreateWire(const MeshPrimitive& primitive, uint32_t maxLines) {
            auto found = gCache.find(&primitive);
            if (found != gCache.end()) {
                if (!found->second.truncated || found->second.lineLimit >= maxLines) {
                    ++gStats.cacheHitCount;
                    return found->second;
                }
                gCache.erase(found);
                found = gCache.end();
            }
            if (found != gCache.end()) {
                ++gStats.cacheHitCount;
                return found->second;
            }

            ++gStats.cacheMissCount;
            CachedPrimitiveWire cached{};
            const std::vector<Vertex3D>* vertices = GetStaticVertices(primitive);
            if (vertices == nullptr || vertices->empty() || primitive.indices.size() < 3) {
                auto inserted = gCache.emplace(&primitive, std::move(cached));
                return inserted.first->second;
            }

            cached.lineLimit = maxLines;
            std::unordered_set<uint64_t> emittedEdges;
            for (size_t i = 0; i + 2 < primitive.indices.size(); i += 3) {
                const uint32_t tri[3] = { primitive.indices[i], primitive.indices[i + 1], primitive.indices[i + 2] };
                for (int e = 0; e < 3; ++e) {
                    const uint32_t a = tri[e];
                    const uint32_t b = tri[(e + 1) % 3];
                    if (a >= vertices->size() || b >= vertices->size()) {
                        continue;
                    }
                    const uint64_t edgeKey = MakeEdge(a, b);
                    if (!emittedEdges.insert(edgeKey).second) {
                        continue;
                    }
                    if (maxLines > 0 && cached.lines.size() >= maxLines) {
                        cached.truncated = true;
                        auto inserted = gCache.emplace(&primitive, std::move(cached));
                        return inserted.first->second;
                    }
                    cached.lines.push_back({ (*vertices)[a].position, (*vertices)[b].position });
                }
            }

            auto inserted = gCache.emplace(&primitive, std::move(cached));
            return inserted.first->second;
        }

        uint32_t PrimitiveColor(uint32_t baseColor, size_t primitiveIndex, bool enabled) {
            if (!enabled) {
                return baseColor;
            }
            constexpr uint32_t colors[] = {
                0x00FFAAFFu, 0x66CCFFFFu, 0xFFCC33FFu, 0xFF66CCFFu, 0x99FF66FFu
            };
            return colors[primitiveIndex % std::size(colors)];
        }

        void SubmitBoundsLines(const Bounds& bounds, const MATH::Mat4& world, uint32_t color) {
            const MATH::Vec3 corners[8] = {
                { bounds.min.x, bounds.min.y, bounds.min.z },
                { bounds.max.x, bounds.min.y, bounds.min.z },
                { bounds.min.x, bounds.max.y, bounds.min.z },
                { bounds.max.x, bounds.max.y, bounds.min.z },
                { bounds.min.x, bounds.min.y, bounds.max.z },
                { bounds.max.x, bounds.min.y, bounds.max.z },
                { bounds.min.x, bounds.max.y, bounds.max.z },
                { bounds.max.x, bounds.max.y, bounds.max.z },
            };
            constexpr int edges[12][2] = {
                {0,1},{1,3},{3,2},{2,0},
                {4,5},{5,7},{7,6},{6,4},
                {0,4},{1,5},{2,6},{3,7}
            };
            for (const auto& edge : edges) {
                RENDERER3D::DEBUG::SubmitLine3D({
                    TransformPoint(world, corners[edge[0]]),
                    TransformPoint(world, corners[edge[1]]),
                    color,
                    RENDERER3D::DEBUG::DebugDepthMode::XRay
                });
                ++gStats.submittedLineCount;
            }
        }
    }

    void BeginFrame() {
        gStats.submittedModelCount = 0;
        gStats.submittedLineCount = 0;
        gStats.truncatedModelCount = 0;
        gStats.cacheHitCount = 0;
        gStats.cacheMissCount = 0;
    }

    void SubmitModelWire(const ModelAsset& asset, const Transform3D& world, uint32_t color, uint32_t maxLines, bool perPrimitiveColor) {
        ++gStats.submittedModelCount;
        const MATH::Mat4 worldMatrix = world.GetWorldMatrix();
        constexpr uint32_t kHardWireLineCap = 200000;
        const uint32_t effectiveMaxLines = (maxLines == 0 || maxLines > kHardWireLineCap) ? kHardWireLineCap : maxLines;
        uint32_t remaining = effectiveMaxLines;
        for (const MeshAsset& mesh : asset.meshes) {
            for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
                const MeshPrimitive& primitive = mesh.primitives[primitiveIndex];
                if (!primitive.skinnedVertices.empty() && !gSkinnedWarningEmitted) {
                    DEBUGLOG::PushRenderError("[MeshWireDebug] Skinned mesh wire uses bind-pose geometry.");
                    gSkinnedWarningEmitted = true;
                }
                const CachedPrimitiveWire& cached = GetOrCreateWire(primitive, remaining);
                const uint32_t primitiveColor = PrimitiveColor(color, primitiveIndex, perPrimitiveColor);
                for (const WireLine& line : cached.lines) {
                    if (remaining == 0) {
                        ++gStats.truncatedModelCount;
                        return;
                    }
                    RENDERER3D::DEBUG::SubmitLine3D({
                        TransformPoint(worldMatrix, line.a),
                        TransformPoint(worldMatrix, line.b),
                        primitiveColor,
                        RENDERER3D::DEBUG::DebugDepthMode::XRay
                    });
                    ++gStats.submittedLineCount;
                    --remaining;
                }
                if (cached.truncated) {
                    ++gStats.truncatedModelCount;
                }
            }
        }
    }

    void SubmitModelBounds(const ModelAsset& asset, const Transform3D& world, uint32_t color) {
        ++gStats.submittedModelCount;
        SubmitBoundsLines(asset.bounds, world.GetWorldMatrix(), color);
    }

    const MeshWireDebugStats& GetDebugStats() {
        return gStats;
    }

    void ClearCache() {
        gCache.clear();
        gSkinnedWarningEmitted = false;
        gStats = {};
    }
} // namespace HIKARI::MESHWIREDEBUG
