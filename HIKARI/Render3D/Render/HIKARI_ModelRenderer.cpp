#include "Render3D/Render/HIKARI_ModelRenderer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/HIKARI_ModelAsset.h"

namespace HIKARI::MODELRENDERER {

    namespace {
        std::vector<ModelRenderItem> gQueue;

        struct ExpandedNodeMeshKey {
            const ModelAsset* source = nullptr;
            int meshIndex = -1;

            bool operator==(const ExpandedNodeMeshKey& rhs) const noexcept {
                return source == rhs.source && meshIndex == rhs.meshIndex;
            }
        };

        struct ExpandedNodeMeshKeyHash {
            size_t operator()(const ExpandedNodeMeshKey& key) const noexcept {
                size_t seed = std::hash<const ModelAsset*>{}(key.source);
                seed ^= static_cast<size_t>(key.meshIndex) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                return seed;
            }
        };

        std::unordered_map<ExpandedNodeMeshKey, std::unique_ptr<ModelAsset>, ExpandedNodeMeshKeyHash> gExpandedNodeMeshCache;

        MATH::Quat MulQuat(const MATH::Quat& a, const MATH::Quat& b) {
            return MATH::NormalizeQ({
                a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
            });
        }

        MATH::Vec3 RotateVector(const MATH::Quat& q, const MATH::Vec3& v) {
            const MATH::Quat nq = MATH::NormalizeQ(q);
            const MATH::Vec3 u{ nq.x, nq.y, nq.z };
            const float s = nq.w;
            return u * (2.0f * MATH::Dot(u, v)) + v * (s * s - MATH::Dot(u, u)) + MATH::Cross(u, v) * (2.0f * s);
        }

        Transform3D ComposeTransform(const Transform3D& parent, const Transform3D& local) {
            Transform3D out{};
            out.scale = {
                parent.scale.x * local.scale.x,
                parent.scale.y * local.scale.y,
                parent.scale.z * local.scale.z
            };
            out.rotation = MulQuat(parent.rotation, local.rotation);

            const MATH::Vec3 scaledLocalPosition{
                local.position.x * parent.scale.x,
                local.position.y * parent.scale.y,
                local.position.z * parent.scale.z
            };
            out.position = parent.position + RotateVector(parent.rotation, scaledLocalPosition);
            return out;
        }

        template<typename T>
        T Lerp(const T& a, const T& b, float t);

        template<>
        MATH::Vec3 Lerp(const MATH::Vec3& a, const MATH::Vec3& b, float t) {
            return {
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t
            };
        }

        template<>
        MATH::Quat Lerp(const MATH::Quat& a, const MATH::Quat& b, float t) {
            const MATH::Quat q{
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t
            };
            return MATH::NormalizeQ(q);
        }

        template<typename TKey, typename TValue>
        TValue SampleLinear(const std::vector<TKey>& keys, float timeSec, const TValue& fallback) {
            if (keys.empty()) return fallback;
            if (keys.size() == 1 || timeSec <= keys.front().timeSec) return keys.front().value;
            if (timeSec >= keys.back().timeSec) return keys.back().value;
            for (size_t i = 0; i + 1 < keys.size(); ++i) {
                const auto& a = keys[i];
                const auto& b = keys[i + 1];
                if (timeSec >= a.timeSec && timeSec <= b.timeSec) {
                    const float span = std::max(1e-5f, b.timeSec - a.timeSec);
                    const float t = (timeSec - a.timeSec) / span;
                    return Lerp<TValue>(a.value, b.value, t);
                }
            }
            return keys.back().value;
        }

        Transform3D BuildAnimatedTransform(const ModelRenderItem& item) {
            Transform3D out = item.worldTransform;
            if (!item.model || item.animationClipName.empty()) {
                return out;
            }
            const AnimationClip* clip = item.model->FindAnimationClip(item.animationClipName);
            if (!clip || clip->channels.empty()) {
                return out;
            }

            float sampleTime = item.animationTimeSec;
            if (item.animationLoop && clip->durationSec > 0.0001f) {
                sampleTime = std::fmod(sampleTime, clip->durationSec);
                if (sampleTime < 0.0f) sampleTime += clip->durationSec;
            }

            for (const NodeAnimationChannel& channel : clip->channels) {
                // Legacy path has a single combined mesh, so apply first node track as object animation.
                if (channel.targetNode > 0) {
                    continue;
                }
                if (channel.path == AnimationTargetPath::Translation) {
                    out.position = SampleLinear<AnimationKeyframe<MATH::Vec3>, MATH::Vec3>(channel.vec3Keys, sampleTime, out.position);
                } else if (channel.path == AnimationTargetPath::Scale) {
                    out.scale = SampleLinear<AnimationKeyframe<MATH::Vec3>, MATH::Vec3>(channel.vec3Keys, sampleTime, out.scale);
                } else if (channel.path == AnimationTargetPath::Rotation) {
                    out.rotation = SampleLinear<AnimationKeyframe<MATH::Quat>, MATH::Quat>(channel.quatKeys, sampleTime, out.rotation);
                }
            }
            return out;
        }

        void EvaluateNodeRecursive(const ModelAsset& asset, int nodeIndex, const Transform3D& parentWorld, std::vector<Transform3D>& outGlobals, std::vector<bool>& visited) {
            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(asset.nodes.size())) {
                return;
            }
            if (visited[static_cast<size_t>(nodeIndex)]) {
                return;
            }

            const ModelNode& node = asset.nodes[static_cast<size_t>(nodeIndex)];
            outGlobals[static_cast<size_t>(nodeIndex)] = ComposeTransform(parentWorld, node.localTransform);
            visited[static_cast<size_t>(nodeIndex)] = true;

            for (int childIndex : node.children) {
                EvaluateNodeRecursive(asset, childIndex, outGlobals[static_cast<size_t>(nodeIndex)], outGlobals, visited);
            }
        }

        void BuildNodeGlobalTransforms(const ModelAsset& asset, const Transform3D& objectTransform, std::vector<Transform3D>& outGlobals) {
            outGlobals.assign(asset.nodes.size(), objectTransform);
            std::vector<bool> visited(asset.nodes.size(), false);

            for (size_t i = 0; i < asset.nodes.size(); ++i) {
                if (asset.nodes[i].parent == -1) {
                    EvaluateNodeRecursive(asset, static_cast<int>(i), objectTransform, outGlobals, visited);
                }
            }

            for (size_t i = 0; i < asset.nodes.size(); ++i) {
                if (!visited[i]) {
                    const int parent = asset.nodes[i].parent;
                    const Transform3D parentWorld = (parent >= 0 && parent < static_cast<int>(outGlobals.size())) ? outGlobals[static_cast<size_t>(parent)] : objectTransform;
                    EvaluateNodeRecursive(asset, static_cast<int>(i), parentWorld, outGlobals, visited);
                }
            }
        }

        ModelAsset* GetOrCreateSingleMeshExpandedAsset(const ModelAsset& source, int meshIndex) {
            if (meshIndex < 0 || meshIndex >= static_cast<int>(source.meshes.size())) {
                return nullptr;
            }

            const ExpandedNodeMeshKey key{ &source, meshIndex };
            auto found = gExpandedNodeMeshCache.find(key);
            if (found != gExpandedNodeMeshCache.end()) {
                return found->second.get();
            }

            auto expanded = std::make_unique<ModelAsset>();
            expanded->id.value = source.GetName() + "#mesh" + std::to_string(meshIndex);
            expanded->sourcePath = source.sourcePath;
            expanded->state = source.state;
            expanded->materials = source.materials;
            expanded->textures = source.textures;
            expanded->bounds = source.bounds;
            expanded->defaultSceneRootNode = 0;
            expanded->meshes.push_back(source.meshes[static_cast<size_t>(meshIndex)]);

            ModelAsset* raw = expanded.get();
            gExpandedNodeMeshCache.emplace(key, std::move(expanded));
            return raw;
        }

        bool SubmitStructuredModelNodes(const ModelRenderItem& item) {
            if (!item.model || item.model->nodes.empty() || item.model->meshes.empty()) {
                return false;
            }

            std::vector<Transform3D> nodeGlobals;
            BuildNodeGlobalTransforms(*item.model, item.worldTransform, nodeGlobals);

            bool submitted = false;
            for (size_t nodeIndex = 0; nodeIndex < item.model->nodes.size(); ++nodeIndex) {
                const ModelNode& node = item.model->nodes[nodeIndex];
                if (node.meshIndex < 0 || node.meshIndex >= static_cast<int>(item.model->meshes.size())) {
                    continue;
                }

                ModelAsset* expandedAsset = GetOrCreateSingleMeshExpandedAsset(*item.model, node.meshIndex);
                if (expandedAsset == nullptr || expandedAsset->meshes.empty()) {
                    continue;
                }

                MESHRENDERER::SubmitStaticMesh(
                    *expandedAsset,
                    nodeGlobals[nodeIndex],
                    item.materialFxProfileId,
                    item.postGroupMask,
                    item.materialFxParamValues,
                    item.materialFxValuesInitialized);
                submitted = true;
            }
            return submitted;
        }
    }

    void Reset() {
        gQueue.clear();
        MESHRENDERER::Reset();
    }

    void SubmitModel(const ModelRenderItem& item) {
        if (!item.model) {
            return;
        }
        gQueue.push_back(item);
    }

    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment) {
        for (const ModelRenderItem& item : gQueue) {
            if (!item.model) {
                continue;
            }

            if (SubmitStructuredModelNodes(item)) {
                continue;
            }

            MESHRENDERER::SubmitStaticMesh(
                *item.model,
                BuildAnimatedTransform(item),
                item.materialFxProfileId,
                item.postGroupMask,
                item.materialFxParamValues,
                item.materialFxValuesInitialized);
        }

        MESHRENDERER::RenderAll(camera, environment);
        gQueue.clear();
    }

}
