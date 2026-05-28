#include "Render3D/Render/HIKARI_ModelRenderer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/HIKARI_ModelAsset.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"

#undef max
#undef min

namespace HIKARI::MODELRENDERER {

    namespace {
        std::vector<ModelRenderItem> gQueue;
        ModelRendererDebugStats gDebugStats;
        uint64_t gFrameIndex = 0;

        MESHRENDERER::MeshRenderDebugMode ToMeshRenderDebugMode(ModelGeometryDebugMode mode) {
            switch (mode) {
            case ModelGeometryDebugMode::WireOverlay:
                return MESHRENDERER::MeshRenderDebugMode::WireOverlay;
            case ModelGeometryDebugMode::WireOnly:
                return MESHRENDERER::MeshRenderDebugMode::WireOnly;
            case ModelGeometryDebugMode::Normal:
            default:
                return MESHRENDERER::MeshRenderDebugMode::Normal;
            }
        }

        struct AnimationLodSettings {
            bool enabled = true;
            float nearDistance = 8.0f;
            float midDistance = 18.0f;
            float farDistance = 35.0f;
            uint32_t nearUpdateInterval = 1;
            uint32_t midUpdateInterval = 2;
            uint32_t farUpdateInterval = 4;
            uint32_t veryFarUpdateInterval = 8;
        };

        struct CachedSkinPose {
            const ModelAsset* model = nullptr;
            std::string clipName;
            bool loop = true;
            float lastSampleTimeSec = -1.0f;
            uint64_t lastFrameUpdated = 0;
            uint64_t lastSubmittedFrame = 0;
            std::vector<Transform3D> animatedLocals;
            std::vector<MATH::Mat4> nodeGlobals;
            std::vector<MATH::Mat4> localNodeGlobals;
            std::unordered_map<int, std::vector<MATH::Mat4>> jointPalettes;
            bool valid = false;
        };

        enum class AnimationLodTier {
            Near,
            Mid,
            Far,
            VeryFar
        };

        AnimationLodSettings gAnimationLodSettings;
        std::unordered_map<uint64_t, CachedSkinPose> gPoseCache;

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
        };

        struct ModelPoseEvaluationScratch {
            std::vector<Transform3D> animatedLocals;
            std::vector<MATH::Mat4> nodeGlobals;
            std::vector<MATH::Mat4> localNodeGlobals;
            std::vector<MATH::Mat4> jointPalette;
            std::vector<uint8_t> visited;
        };

        std::unordered_map<ExpandedNodeMeshKey, ExpandedNodeMeshCacheEntry, ExpandedNodeMeshKeyHash> gExpandedNodeMeshCache;

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
            auto found = gExpandedNodeMeshCache.find(key);
            if (found != gExpandedNodeMeshCache.end()) {
                if (found->second.asset != nullptr) {
                    ++gDebugStats.expandedMeshCacheHitCount;
                    return found->second.asset.get();
                }

                ++gDebugStats.expandedMeshCacheMissCount;
                found->second.asset = BuildSingleMeshExpandedAsset(source, meshIndex);
                return found->second.asset.get();
            }

            // Expanded node mesh assets are treated as immutable after model load.
            // If runtime asset hot-reload is added later, invalidate this cache explicitly.
            ++gDebugStats.expandedMeshCacheMissCount;
            ExpandedNodeMeshCacheEntry entry{};
            entry.asset = BuildSingleMeshExpandedAsset(source, meshIndex);
            if (!entry.asset) {
                return nullptr;
            }

            ModelAsset* raw = entry.asset.get();
            gExpandedNodeMeshCache.emplace(key, std::move(entry));
            return raw;
        }

        bool MeshHasSkinnedPrimitives(const ModelAsset& source, int meshIndex) {
            if (meshIndex < 0 || meshIndex >= static_cast<int>(source.meshes.size())) {
                return false;
            }
            const MeshAsset& mesh = source.meshes[static_cast<size_t>(meshIndex)];
            for (const MeshPrimitive& primitive : mesh.primitives) {
                if (!primitive.skinnedVertices.empty()) {
                    return true;
                }
            }
            return false;
        }

        bool ShouldUsePoseCache(const ModelRenderItem& item) {
            return item.model != nullptr && item.model->HasSkinnedMesh() && !item.animationClipName.empty();
        }

        float DistanceToCamera(const ModelRenderItem& item, const Camera3D& camera) {
            const MATH::Vec3 cameraPos = camera.GetPosition();
            const MATH::Vec3 objectPos = item.worldTransform.position;
            const float dx = objectPos.x - cameraPos.x;
            const float dy = objectPos.y - cameraPos.y;
            const float dz = objectPos.z - cameraPos.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        AnimationLodTier ResolveAnimationLodTier(const ModelRenderItem& item, const Camera3D& camera) {
            const float distance = DistanceToCamera(item, camera);
            if (distance < gAnimationLodSettings.nearDistance) {
                return AnimationLodTier::Near;
            }
            if (distance < gAnimationLodSettings.midDistance) {
                return AnimationLodTier::Mid;
            }
            if (distance < gAnimationLodSettings.farDistance) {
                return AnimationLodTier::Far;
            }
            return AnimationLodTier::VeryFar;
        }

        uint32_t ResolveAnimationUpdateInterval(AnimationLodTier tier) {
            switch (tier) {
            case AnimationLodTier::Near:
                ++gDebugStats.lodNearCount;
                return std::max<uint32_t>(1, gAnimationLodSettings.nearUpdateInterval);
            case AnimationLodTier::Mid:
                ++gDebugStats.lodMidCount;
                return std::max<uint32_t>(1, gAnimationLodSettings.midUpdateInterval);
            case AnimationLodTier::Far:
                ++gDebugStats.lodFarCount;
                return std::max<uint32_t>(1, gAnimationLodSettings.farUpdateInterval);
            case AnimationLodTier::VeryFar:
            default:
                ++gDebugStats.lodVeryFarCount;
                return std::max<uint32_t>(1, gAnimationLodSettings.veryFarUpdateInterval);
            }
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

            ++gDebugStats.sampledKeySearchCount;
            const auto it = std::lower_bound(
                keys.begin(),
                keys.end(),
                timeSec,
                [](const TKey& key, float t) {
                    return key.timeSec < t;
                });
            if (it == keys.begin()) {
                return it->value;
            }
            const auto& a = *(it - 1);
            const auto& b = *it;
            if (interpolation == AnimationInterpolation::Step) {
                return a.value;
            }

            const float span = std::max(1e-5f, b.timeSec - a.timeSec);
            const float t = (timeSec - a.timeSec) / span;
            return LerpValue<TValue>(a.value, b.value, t);
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

        void BuildAnimatedNodeLocals(const ModelRenderItem& item, std::vector<Transform3D>& outLocals) {
            outLocals.clear();
            if (!item.model) {
                return;
            }

            ++gDebugStats.animatedLocalBuildCount;
            outLocals.reserve(item.model->nodes.size());
            for (const ModelNode& node : item.model->nodes) {
                outLocals.push_back(node.localTransform);
            }

            const AnimationClip* clip = ResolveAnimationClip(item);
            if (!clip || clip->channels.empty()) {
                return;
            }

            const float sampleTime = ResolveAnimationSampleTime(item, *clip);
            for (const NodeAnimationChannel& channel : clip->channels) {
                ++gDebugStats.sampledChannelCount;
                if (channel.targetNode < 0 || channel.targetNode >= static_cast<int>(outLocals.size())) {
                    continue;
                }

                Transform3D& local = outLocals[static_cast<size_t>(channel.targetNode)];
                if (channel.path == AnimationTargetPath::Translation) {
                    local.position = SampleKeys<AnimationKeyframe<MATH::Vec3>, MATH::Vec3>(channel.vec3Keys, sampleTime, channel.interpolation, local.position);
                } else if (channel.path == AnimationTargetPath::Scale) {
                    local.scale = SampleKeys<AnimationKeyframe<MATH::Vec3>, MATH::Vec3>(channel.vec3Keys, sampleTime, channel.interpolation, local.scale);
                } else if (channel.path == AnimationTargetPath::Rotation) {
                    local.rotation = SampleKeys<AnimationKeyframe<MATH::Quat>, MATH::Quat>(channel.quatKeys, sampleTime, channel.interpolation, local.rotation);
                }
            }
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

        void EvaluateNodeMatrixRecursive(const ModelAsset& asset, const std::vector<Transform3D>& animatedLocals, int nodeIndex, const MATH::Mat4& parentWorld, std::vector<MATH::Mat4>& outGlobals, std::vector<uint8_t>& visited) {
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

        void BuildNodeGlobalMatricesWithRoot(const ModelAsset& model, const std::vector<Transform3D>& animatedLocals, const MATH::Mat4& rootWorld, std::vector<MATH::Mat4>& outGlobals, std::vector<uint8_t>& visited) {
            if (model.nodes.empty()) {
                outGlobals.clear();
                return;
            }

            ++gDebugStats.nodeGlobalMatrixBuildCount;
            gDebugStats.nodeGlobalMatrixCount += model.nodes.size();
            outGlobals.assign(model.nodes.size(), rootWorld);
            visited.assign(model.nodes.size(), 0);

            for (size_t i = 0; i < model.nodes.size(); ++i) {
                if (model.nodes[i].parent == -1) {
                    EvaluateNodeMatrixRecursive(model, animatedLocals, static_cast<int>(i), rootWorld, outGlobals, visited);
                }
            }

            for (size_t i = 0; i < model.nodes.size(); ++i) {
                if (!visited[i]) {
                    const int parent = model.nodes[i].parent;
                    const MATH::Mat4 parentWorld = (parent >= 0 && parent < static_cast<int>(outGlobals.size())) ? outGlobals[static_cast<size_t>(parent)] : rootWorld;
                    EvaluateNodeMatrixRecursive(model, animatedLocals, static_cast<int>(i), parentWorld, outGlobals, visited);
                }
            }
        }

        void BuildNodeGlobalMatrices(const ModelRenderItem& item, std::vector<MATH::Mat4>& outGlobals) {
            if (!item.model) {
                outGlobals.clear();
                return;
            }
            std::vector<Transform3D> animatedLocals;
            std::vector<uint8_t> visited;
            BuildAnimatedNodeLocals(item, animatedLocals);
            BuildNodeGlobalMatricesWithRoot(*item.model, animatedLocals, item.worldTransform.GetWorldMatrix(), outGlobals, visited);
        }

        void BuildNodeGlobalMatricesLocal(const ModelRenderItem& item, std::vector<MATH::Mat4>& outGlobals) {
            if (!item.model) {
                outGlobals.clear();
                return;
            }
            std::vector<Transform3D> animatedLocals;
            std::vector<uint8_t> visited;
            BuildAnimatedNodeLocals(item, animatedLocals);
            BuildNodeGlobalMatricesWithRoot(*item.model, animatedLocals, MATH::Mat4::Identity(), outGlobals, visited);
        }

        bool BuildJointPalette(const ModelAsset& model, int skinIndex, const std::vector<MATH::Mat4>& nodeGlobals, std::vector<MATH::Mat4>& outPalette) {
            outPalette.clear();
            const SkeletonAsset* skin = model.FindSkin(skinIndex);
            if (skin == nullptr) {
                return false;
            }

            ++gDebugStats.jointPaletteBuildCount;
            gDebugStats.jointPaletteMatrixCount += skin->joints.size();
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

        bool ShouldUpdatePoseThisFrame(const ModelRenderItem& item, const Camera3D& camera, const CachedSkinPose& cache) {
            if (!gAnimationLodSettings.enabled) {
                return true;
            }

            const AnimationLodTier tier = ResolveAnimationLodTier(item, camera);
            const uint32_t interval = ResolveAnimationUpdateInterval(tier);
            if (item.showSkeletonDebug || !cache.valid) {
                return true;
            }
            return ((gFrameIndex + item.instanceKey) % interval) == 0;
        }

        CachedSkinPose& ResolveOrUpdatePose(const ModelRenderItem& item, const Camera3D& camera, ModelPoseEvaluationScratch& scratch) {
            const uint64_t cacheKey = item.instanceKey != 0 ? item.instanceKey : reinterpret_cast<uint64_t>(item.model);
            auto found = gPoseCache.find(cacheKey);
            if (found == gPoseCache.end()) {
                ++gDebugStats.poseCacheMissCount;
                found = gPoseCache.emplace(cacheKey, CachedSkinPose{}).first;
            } else {
                ++gDebugStats.poseCacheHitCount;
            }

            CachedSkinPose& cache = found->second;
            cache.lastSubmittedFrame = gFrameIndex;

            const bool mustRebuild =
                !cache.valid ||
                cache.model != item.model ||
                cache.clipName != item.animationClipName ||
                cache.loop != item.animationLoop;
            const bool dueThisFrame = ShouldUpdatePoseThisFrame(item, camera, cache);
            const bool updatePose = mustRebuild || dueThisFrame;

            if (updatePose) {
                cache.model = item.model;
                cache.clipName = item.animationClipName;
                cache.loop = item.animationLoop;
                cache.lastSampleTimeSec = item.animationTimeSec;
                cache.lastFrameUpdated = gFrameIndex;

                BuildAnimatedNodeLocals(item, cache.animatedLocals);
                BuildNodeGlobalMatricesWithRoot(*item.model, cache.animatedLocals, MATH::Mat4::Identity(), cache.localNodeGlobals, scratch.visited);
                cache.jointPalettes.clear();
                cache.valid = true;
                ++gDebugStats.poseUpdatedCount;
            } else {
                ++gDebugStats.poseReusedCount;
            }

            // World globals depend on the object transform, so rebuild them each frame even when the pose is reused.
            BuildNodeGlobalMatricesWithRoot(*item.model, cache.animatedLocals, item.worldTransform.GetWorldMatrix(), cache.nodeGlobals, scratch.visited);
            return cache;
        }

        void PrunePoseCache() {
            constexpr uint64_t kCacheKeepFrames = 300;
            for (auto it = gPoseCache.begin(); it != gPoseCache.end();) {
                if (gFrameIndex > it->second.lastSubmittedFrame &&
                    gFrameIndex - it->second.lastSubmittedFrame > kCacheKeepFrames) {
                    it = gPoseCache.erase(it);
                } else {
                    ++it;
                }
            }
        }

        MATH::Vec3 ExtractTranslation(const MATH::Mat4& m) {
            return { m.m[3][0], m.m[3][1], m.m[3][2] };
        }

        void SubmitSkeletonDebugLines(const SkeletonAsset& skin, const std::vector<MATH::Mat4>& nodeGlobals, bool xray, uint32_t color) {
            const auto mode = xray
                ? RENDERER3D::DEBUG::DebugDepthMode::XRay
                : RENDERER3D::DEBUG::DebugDepthMode::DepthTest;

            for (size_t jointIndex = 0; jointIndex < skin.joints.size(); ++jointIndex) {
                const SkeletonJoint& joint = skin.joints[jointIndex];
                if (joint.parentJoint < 0 || joint.parentJoint >= static_cast<int>(skin.joints.size())) {
                    continue;
                }

                const SkeletonJoint& parent = skin.joints[static_cast<size_t>(joint.parentJoint)];
                if (joint.nodeIndex < 0 || parent.nodeIndex < 0 ||
                    joint.nodeIndex >= static_cast<int>(nodeGlobals.size()) ||
                    parent.nodeIndex >= static_cast<int>(nodeGlobals.size())) {
                    continue;
                }

                RENDERER3D::DEBUG::SubmitLine3D({
                    ExtractTranslation(nodeGlobals[static_cast<size_t>(parent.nodeIndex)]),
                    ExtractTranslation(nodeGlobals[static_cast<size_t>(joint.nodeIndex)]),
                    color,
                    mode
                });
                ++gDebugStats.skeletonDebugLineCount;
            }
        }

        bool SubmitStructuredModelNodes(const ModelRenderItem& item, const Camera3D& camera) {
            if (!item.model || item.model->nodes.empty() || item.model->meshes.empty()) {
                return false;
            }

            ++gDebugStats.structuredModelCount;
            ModelPoseEvaluationScratch scratch;
            scratch.animatedLocals.reserve(item.model->nodes.size());
            scratch.nodeGlobals.reserve(item.model->nodes.size());
            scratch.localNodeGlobals.reserve(item.model->nodes.size());
            scratch.jointPalette.reserve(128u);
            scratch.visited.reserve(item.model->nodes.size());

            const bool usePoseCache = ShouldUsePoseCache(item);
            CachedSkinPose* poseCache = nullptr;
            if (usePoseCache) {
                poseCache = &ResolveOrUpdatePose(item, camera, scratch);
            } else {
                BuildAnimatedNodeLocals(item, scratch.animatedLocals);
                BuildNodeGlobalMatricesWithRoot(*item.model, scratch.animatedLocals, item.worldTransform.GetWorldMatrix(), scratch.nodeGlobals, scratch.visited);
                BuildNodeGlobalMatricesWithRoot(*item.model, scratch.animatedLocals, MATH::Mat4::Identity(), scratch.localNodeGlobals, scratch.visited);
            }

            const std::vector<MATH::Mat4>& nodeGlobals = poseCache ? poseCache->nodeGlobals : scratch.nodeGlobals;
            const std::vector<MATH::Mat4>& localNodeGlobals = poseCache ? poseCache->localNodeGlobals : scratch.localNodeGlobals;

            bool submitted = false;
            std::unordered_set<int> submittedDebugSkins;
            for (size_t nodeIndex = 0; nodeIndex < item.model->nodes.size(); ++nodeIndex) {
                const ModelNode& node = item.model->nodes[nodeIndex];
                if (node.meshIndex < 0 || node.meshIndex >= static_cast<int>(item.model->meshes.size())) {
                    continue;
                }

                bool submittedSkinned = false;
                if (node.skinIndex >= 0) {
                    if (item.showSkeletonDebug && submittedDebugSkins.insert(node.skinIndex).second) {
                        if (const SkeletonAsset* skin = item.model->FindSkin(node.skinIndex)) {
                            SubmitSkeletonDebugLines(*skin, nodeGlobals, item.skeletonDebugXRay, item.skeletonDebugColor);
                        }
                    }

                    ++gDebugStats.skinnedNodeCount;
                    const std::vector<MATH::Mat4>* jointPalette = nullptr;
                    if (poseCache != nullptr) {
                        auto paletteIt = poseCache->jointPalettes.find(node.skinIndex);
                        if (paletteIt != poseCache->jointPalettes.end() && !paletteIt->second.empty()) {
                            ++gDebugStats.jointPaletteCacheHitCount;
                            jointPalette = &paletteIt->second;
                        } else {
                            ++gDebugStats.jointPaletteCacheMissCount;
                            std::vector<MATH::Mat4>& cachedPalette = poseCache->jointPalettes[node.skinIndex];
                            // Palette is built from model-local node globals. The skinned VS then applies object world once.
                            if (BuildJointPalette(*item.model, node.skinIndex, localNodeGlobals, cachedPalette)) {
                                jointPalette = &cachedPalette;
                            }
                        }
                    } else {
                        scratch.jointPalette.clear();
                        // Palette is built from model-local node globals. The skinned VS then applies object world once.
                        if (BuildJointPalette(*item.model, node.skinIndex, localNodeGlobals, scratch.jointPalette)) {
                            jointPalette = &scratch.jointPalette;
                        }
                    }

                    if (jointPalette != nullptr) {
                        RecordBuiltJointPalette(node.skinIndex, *jointPalette);
                        ModelAsset* expandedAsset = GetOrCreateSingleMeshExpandedAsset(*item.model, node.meshIndex);
                        if (expandedAsset != nullptr && !expandedAsset->meshes.empty() && MeshHasSkinnedPrimitives(*item.model, node.meshIndex)) {
                            Transform3D skinnedTransform = item.worldTransform;
                            MESHRENDERER::SubmitSkinnedMesh(
                                *expandedAsset,
                                skinnedTransform,
                                *jointPalette,
                                item.materialFxProfileId,
                                item.postGroupMask,
                                item.materialFxParamValues,
                                item.materialFxValuesInitialized,
                                item.receiveShadow,
                                ToMeshRenderDebugMode(item.geometryDebugMode),
                                item.materialOverride);
                            SHADOW::SubmitSkinnedMesh(*expandedAsset, skinnedTransform, *jointPalette, item.castShadow);
                            submittedSkinned = true;
                            submitted = true;
                        }
                    }
                }
                if (submittedSkinned) {
                    continue;
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
                    item.materialFxValuesInitialized,
                    item.receiveShadow,
                    ToMeshRenderDebugMode(item.geometryDebugMode),
                    item.materialOverride);
                SHADOW::SubmitStaticMesh(*expandedAsset, nodeTransform, item.castShadow);
                submitted = true;
            }
            return submitted;
        }
    }

    void Reset() {
        gQueue.clear();
        gDebugStats = {};
        MESHRENDERER::Reset();
        SHADOW::Reset();
    }

    void SubmitModel(const ModelRenderItem& item) {
        if (!item.model) {
            return;
        }
        ++gDebugStats.submittedModelItemCount;
        gQueue.push_back(item);
    }

    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment) {
        ++gFrameIndex;
        SHADOW::BeginFrame(environment, camera);

        for (const ModelRenderItem& item : gQueue) {
            if (!item.model) {
                continue;
            }

            if (SubmitStructuredModelNodes(item, camera)) {
                continue;
            }

            const Transform3D animatedTransform = BuildAnimatedTransform(item);
            MESHRENDERER::SubmitStaticMesh(
                *item.model,
                animatedTransform,
                item.materialFxProfileId,
                item.postGroupMask,
                item.materialFxParamValues,
                item.materialFxValuesInitialized,
                item.receiveShadow,
                ToMeshRenderDebugMode(item.geometryDebugMode),
                item.materialOverride);
            SHADOW::SubmitStaticMesh(*item.model, animatedTransform, item.castShadow);
        }

        SHADOW::RenderDirectionalShadowMap();
        MESHRENDERER::RenderAll(camera, environment);
        gQueue.clear();
        PrunePoseCache();
    }

    const ModelRendererDebugStats& GetDebugStats() {
        return gDebugStats;
    }

}
