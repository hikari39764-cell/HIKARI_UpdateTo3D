#include "Render3D/Render/HIKARI_ModelRenderer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/HIKARI_ModelAsset.h"

#undef max
#undef min

namespace HIKARI::MODELRENDERER {

    namespace {
        std::vector<ModelRenderItem> gQueue;
        ModelRendererDebugStats gDebugStats;

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

        struct ExpandedNodeMeshCacheEntry {
            std::unique_ptr<ModelAsset> asset;
            size_t sourceSignature = 0;
        };

        std::unordered_map<ExpandedNodeMeshKey, ExpandedNodeMeshCacheEntry, ExpandedNodeMeshKeyHash> gExpandedNodeMeshCache;

        size_t HashCombine(size_t seed, size_t value) {
            return seed ^ (value + 0x9e3779b9 + (seed << 6) + (seed >> 2));
        }

        size_t BuildMeshSignature(const ModelAsset& source, int meshIndex) {
            if (meshIndex < 0 || meshIndex >= static_cast<int>(source.meshes.size())) {
                return 0;
            }

            size_t seed = std::hash<const ModelAsset*>{}(&source);
            seed = HashCombine(seed, static_cast<size_t>(meshIndex));
            seed = HashCombine(seed, static_cast<size_t>(source.GetState()));
            seed = HashCombine(seed, source.materials.size());
            seed = HashCombine(seed, source.textures.size());

            const MeshAsset& mesh = source.meshes[static_cast<size_t>(meshIndex)];
            seed = HashCombine(seed, mesh.name.size());
            seed = HashCombine(seed, mesh.primitives.size());

            for (const MeshPrimitive& primitive : mesh.primitives) {
                seed = HashCombine(seed, primitive.name.size());
                seed = HashCombine(seed, static_cast<size_t>(primitive.layout));
                seed = HashCombine(seed, primitive.staticVertices.size());
                seed = HashCombine(seed, primitive.skinnedVertices.size());
                seed = HashCombine(seed, primitive.indices.size());
                seed = HashCombine(seed, static_cast<size_t>(primitive.materialIndex));
            }
            return seed;
        }

        std::unique_ptr<ModelAsset> BuildSingleMeshExpandedAsset(const ModelAsset& source, int meshIndex) {
            if (meshIndex < 0 || meshIndex >= static_cast<int>(source.meshes.size())) {
                return nullptr;
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
            return expanded;
        }

        ModelAsset* GetOrCreateSingleMeshExpandedAsset(const ModelAsset& source, int meshIndex) {
            if (meshIndex < 0 || meshIndex >= static_cast<int>(source.meshes.size())) {
                return nullptr;
            }

            const ExpandedNodeMeshKey key{ &source, meshIndex };
            const size_t sourceSignature = BuildMeshSignature(source, meshIndex);
            auto found = gExpandedNodeMeshCache.find(key);
            if (found != gExpandedNodeMeshCache.end()) {
                if (found->second.sourceSignature == sourceSignature && found->second.asset != nullptr) {
                    found->second.asset->state = source.state;
                    found->second.asset->sourcePath = source.sourcePath;
                    found->second.asset->materials = source.materials;
                    found->second.asset->textures = source.textures;
                    return found->second.asset.get();
                }

                found->second.asset = BuildSingleMeshExpandedAsset(source, meshIndex);
                found->second.sourceSignature = sourceSignature;
                return found->second.asset.get();
            }

            ExpandedNodeMeshCacheEntry entry{};
            entry.sourceSignature = sourceSignature;
            entry.asset = BuildSingleMeshExpandedAsset(source, meshIndex);
            if (!entry.asset) {
                return nullptr;
            }

            ModelAsset* raw = entry.asset.get();
            gExpandedNodeMeshCache.emplace(key, std::move(entry));
            return raw;
        }

        template<typename T>
        T LerpValue(const T& a, const T& b, float t);

        template<>
        MATH::Vec3 LerpValue(const MATH::Vec3& a, const MATH::Vec3& b, float t) {
            return {
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t
            };
        }

        template<>
        MATH::Quat LerpValue(const MATH::Quat& a, const MATH::Quat& b, float t) {
            MATH::Quat end = b;
            const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
            if (dot < 0.0f) {
                end.x = -end.x;
                end.y = -end.y;
                end.z = -end.z;
                end.w = -end.w;
            }

            const MATH::Quat q{
                a.x + (end.x - a.x) * t,
                a.y + (end.y - a.y) * t,
                a.z + (end.z - a.z) * t,
                a.w + (end.w - a.w) * t
            };
            return MATH::NormalizeQ(q);
        }

        template<typename TKey, typename TValue>
        TValue SampleKeys(const std::vector<TKey>& keys, float timeSec, AnimationInterpolation interpolation, const TValue& fallback) {
            if (keys.empty()) {
                return fallback;
            }
            if (keys.size() == 1 || timeSec <= keys.front().timeSec) {
                return keys.front().value;
            }
            if (timeSec >= keys.back().timeSec) {
                return keys.back().value;
            }

            for (size_t i = 0; i + 1 < keys.size(); ++i) {
                const auto& a = keys[i];
                const auto& b = keys[i + 1];
                if (timeSec >= a.timeSec && timeSec <= b.timeSec) {
                    if (interpolation == AnimationInterpolation::Step) {
                        return a.value;
                    }
                    const float span = std::max(1e-5f, b.timeSec - a.timeSec);
                    const float t = (timeSec - a.timeSec) / span;
                    return LerpValue<TValue>(a.value, b.value, t);
                }
            }
            return keys.back().value;
        }

        const AnimationClip* ResolveAnimationClip(const ModelRenderItem& item) {
            if (!item.model || item.animationClipName.empty()) {
                return nullptr;
            }
            return item.model->FindAnimationClip(item.animationClipName);
        }

        float ResolveAnimationSampleTime(const ModelRenderItem& item, const AnimationClip& clip) {
            float sampleTime = item.animationTimeSec;
            if (item.animationLoop && clip.durationSec > 0.0001f) {
                sampleTime = std::fmod(sampleTime, clip.durationSec);
                if (sampleTime < 0.0f) {
                    sampleTime += clip.durationSec;
                }
            }
            return sampleTime;
        }

        Transform3D BuildAnimatedTransform(const ModelRenderItem& item) {
            Transform3D out = item.worldTransform;
            const AnimationClip* clip = ResolveAnimationClip(item);
            if (!clip || clip->channels.empty()) {
                return out;
            }

            const float sampleTime = ResolveAnimationSampleTime(item, *clip);
            for (const NodeAnimationChannel& channel : clip->channels) {
                // Legacy single-mesh path: keep previous behavior by applying the first node track to object transform.
                if (channel.targetNode > 0) {
                    continue;
                }
                if (channel.path == AnimationTargetPath::Translation) {
                    out.position = SampleKeys<AnimationKeyframe<MATH::Vec3>, MATH::Vec3>(channel.vec3Keys, sampleTime, channel.interpolation, out.position);
                } else if (channel.path == AnimationTargetPath::Scale) {
                    out.scale = SampleKeys<AnimationKeyframe<MATH::Vec3>, MATH::Vec3>(channel.vec3Keys, sampleTime, channel.interpolation, out.scale);
                } else if (channel.path == AnimationTargetPath::Rotation) {
                    out.rotation = SampleKeys<AnimationKeyframe<MATH::Quat>, MATH::Quat>(channel.quatKeys, sampleTime, channel.interpolation, out.rotation);
                }
            }
            return out;
        }

        std::vector<Transform3D> BuildAnimatedNodeLocals(const ModelRenderItem& item) {
            std::vector<Transform3D> locals;
            if (!item.model) {
                return locals;
            }

            locals.reserve(item.model->nodes.size());
            for (const ModelNode& node : item.model->nodes) {
                locals.push_back(node.localTransform);
            }

            const AnimationClip* clip = ResolveAnimationClip(item);
            if (!clip || clip->channels.empty()) {
                return locals;
            }

            const float sampleTime = ResolveAnimationSampleTime(item, *clip);
            for (const NodeAnimationChannel& channel : clip->channels) {
                if (channel.targetNode < 0 || channel.targetNode >= static_cast<int>(locals.size())) {
                    continue;
                }

                Transform3D& local = locals[static_cast<size_t>(channel.targetNode)];
                if (channel.path == AnimationTargetPath::Translation) {
                    local.position = SampleKeys<AnimationKeyframe<MATH::Vec3>, MATH::Vec3>(channel.vec3Keys, sampleTime, channel.interpolation, local.position);
                } else if (channel.path == AnimationTargetPath::Scale) {
                    local.scale = SampleKeys<AnimationKeyframe<MATH::Vec3>, MATH::Vec3>(channel.vec3Keys, sampleTime, channel.interpolation, local.scale);
                } else if (channel.path == AnimationTargetPath::Rotation) {
                    local.rotation = SampleKeys<AnimationKeyframe<MATH::Quat>, MATH::Quat>(channel.quatKeys, sampleTime, channel.interpolation, local.rotation);
                }
            }
            return locals;
        }

        MATH::Mat4 GetNodeLocalMatrix(const ModelNode& node, const std::vector<Transform3D>& animatedLocals, size_t nodeIndex) {
            if (node.hasLocalMatrix) {
                return node.localMatrix;
            }
            if (nodeIndex < animatedLocals.size()) {
                return animatedLocals[nodeIndex].GetLocalMatrix();
            }
            return node.localTransform.GetLocalMatrix();
        }

        void EvaluateNodeMatrixRecursive(const ModelAsset& asset, const std::vector<Transform3D>& animatedLocals, int nodeIndex, const MATH::Mat4& parentWorld, std::vector<MATH::Mat4>& outGlobals, std::vector<bool>& visited) {
            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(asset.nodes.size())) {
                return;
            }
            if (visited[static_cast<size_t>(nodeIndex)]) {
                return;
            }

            const ModelNode& node = asset.nodes[static_cast<size_t>(nodeIndex)];
            outGlobals[static_cast<size_t>(nodeIndex)] = parentWorld * GetNodeLocalMatrix(node, animatedLocals, static_cast<size_t>(nodeIndex));
            visited[static_cast<size_t>(nodeIndex)] = true;

            for (int childIndex : node.children) {
                EvaluateNodeMatrixRecursive(asset, animatedLocals, childIndex, outGlobals[static_cast<size_t>(nodeIndex)], outGlobals, visited);
            }
        }

        void BuildNodeGlobalMatrices(const ModelRenderItem& item, std::vector<MATH::Mat4>& outGlobals) {
            if (!item.model) {
                outGlobals.clear();
                return;
            }

            const MATH::Mat4 objectWorld = item.worldTransform.GetWorldMatrix();
            outGlobals.assign(item.model->nodes.size(), objectWorld);
            std::vector<bool> visited(item.model->nodes.size(), false);
            const std::vector<Transform3D> animatedLocals = BuildAnimatedNodeLocals(item);

            for (size_t i = 0; i < item.model->nodes.size(); ++i) {
                if (item.model->nodes[i].parent == -1) {
                    EvaluateNodeMatrixRecursive(*item.model, animatedLocals, static_cast<int>(i), objectWorld, outGlobals, visited);
                }
            }

            for (size_t i = 0; i < item.model->nodes.size(); ++i) {
                if (!visited[i]) {
                    const int parent = item.model->nodes[i].parent;
                    const MATH::Mat4 parentWorld = (parent >= 0 && parent < static_cast<int>(outGlobals.size())) ? outGlobals[static_cast<size_t>(parent)] : objectWorld;
                    EvaluateNodeMatrixRecursive(*item.model, animatedLocals, static_cast<int>(i), parentWorld, outGlobals, visited);
                }
            }
        }

        bool BuildJointPalette(const ModelAsset& model, int skinIndex, const std::vector<MATH::Mat4>& nodeGlobals, std::vector<MATH::Mat4>& outPalette) {
            outPalette.clear();
            const SkeletonAsset* skin = model.FindSkin(skinIndex);
            if (skin == nullptr) {
                return false;
            }

            outPalette.resize(skin->joints.size(), MATH::Mat4::Identity());
            for (size_t jointIndex = 0; jointIndex < skin->joints.size(); ++jointIndex) {
                const SkeletonJoint& joint = skin->joints[jointIndex];
                if (joint.nodeIndex < 0 || joint.nodeIndex >= static_cast<int>(nodeGlobals.size())) {
                    outPalette[jointIndex] = MATH::Mat4::Identity();
                    continue;
                }

                const MATH::Mat4& jointGlobal = nodeGlobals[static_cast<size_t>(joint.nodeIndex)];
                outPalette[jointIndex] = jointGlobal * joint.inverseBindMatrix;
            }
            return true;
        }

        void RecordBuiltJointPalette(int skinIndex, const std::vector<MATH::Mat4>& palette) {
            ++gDebugStats.builtPaletteCount;
            gDebugStats.totalJointMatrixCount += palette.size();
            gDebugStats.lastSkinIndex = skinIndex;
            gDebugStats.lastPaletteJointCount = palette.size();
            if (!palette.empty()) {
                gDebugStats.firstJointMatrix = palette.front();
                gDebugStats.hasFirstJointMatrix = true;
            }
        }

        bool SubmitStructuredModelNodes(const ModelRenderItem& item) {
            if (!item.model || item.model->nodes.empty() || item.model->meshes.empty()) {
                return false;
            }

            std::vector<MATH::Mat4> nodeGlobals;
            BuildNodeGlobalMatrices(item, nodeGlobals);

            bool submitted = false;
            for (size_t nodeIndex = 0; nodeIndex < item.model->nodes.size(); ++nodeIndex) {
                const ModelNode& node = item.model->nodes[nodeIndex];
                if (node.meshIndex < 0 || node.meshIndex >= static_cast<int>(item.model->meshes.size())) {
                    continue;
                }

                if (node.skinIndex >= 0) {
                    ++gDebugStats.skinnedNodeCount;
                    std::vector<MATH::Mat4> jointPalette;
                    if (BuildJointPalette(*item.model, node.skinIndex, nodeGlobals, jointPalette)) {
                        RecordBuiltJointPalette(node.skinIndex, jointPalette);
                    }
                }

                ModelAsset* expandedAsset = GetOrCreateSingleMeshExpandedAsset(*item.model, node.meshIndex);
                if (expandedAsset == nullptr || expandedAsset->meshes.empty()) {
                    continue;
                }

                Transform3D nodeTransform{};
                nodeTransform.useExplicitMatrix = true;
                nodeTransform.explicitMatrix = nodeGlobals[nodeIndex];

                MESHRENDERER::SubmitStaticMesh(
                    *expandedAsset,
                    nodeTransform,
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
        gDebugStats = {};
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

    const ModelRendererDebugStats& GetDebugStats() {
        return gDebugStats;
    }

}
