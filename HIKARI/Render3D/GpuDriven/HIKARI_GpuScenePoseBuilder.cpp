#include "Render3D/GpuDriven/HIKARI_GpuScenePoseBuilder.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        template<typename TValue>
        TValue LerpValue(const TValue& a, const TValue& b, float t);

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
        TValue SampleAnimationKeys(
            const std::vector<TKey>& keys,
            float timeSec,
            AnimationInterpolation interpolation,
            const TValue& fallback) {

            if (keys.empty()) {
                return fallback;
            }
            if (keys.size() == 1 || timeSec <= keys.front().timeSec) {
                return keys.front().value;
            }
            if (timeSec >= keys.back().timeSec) {
                return keys.back().value;
            }

            const auto it = std::lower_bound(
                keys.begin(),
                keys.end(),
                timeSec,
                [](const TKey& key, float value) {
                    return key.timeSec < value;
                });
            if (it == keys.begin()) {
                return it->value;
            }
            const TKey& a = *(it - 1);
            const TKey& b = *it;
            if (interpolation == AnimationInterpolation::Step) {
                return a.value;
            }

            const float span = (std::max)(1e-5f, b.timeSec - a.timeSec);
            const float t = (timeSec - a.timeSec) / span;
            return LerpValue<TValue>(a.value, b.value, t);
        }

        float ResolveAnimationSampleTime(
            const GpuSceneSurfaceRecord& record,
            const AnimationClip& clip) {

            float sampleTime = record.animationTimeSec;
            if (record.animationLoop && clip.durationSec > 0.0001f) {
                sampleTime = std::fmod(sampleTime, clip.durationSec);
                if (sampleTime < 0.0f) {
                    sampleTime += clip.durationSec;
                }
            }
            return sampleTime;
        }

        void BuildAnimatedNodeLocals(
            const GpuSceneSurfaceRecord& record,
            std::vector<Transform3D>& outLocals) {

            outLocals.clear();
            if (record.model == nullptr) {
                return;
            }

            outLocals.reserve(record.model->nodes.size());
            for (const ModelNode& node : record.model->nodes) {
                outLocals.push_back(node.localTransform);
            }

            if (record.animationClipName.empty()) {
                return;
            }
            const AnimationClip* clip =
                record.model->FindAnimationClip(record.animationClipName);
            if (clip == nullptr || clip->channels.empty()) {
                return;
            }

            const float sampleTime = ResolveAnimationSampleTime(record, *clip);
            for (const NodeAnimationChannel& channel : clip->channels) {
                if (channel.targetNode < 0 ||
                    channel.targetNode >= static_cast<int>(outLocals.size())) {
                    continue;
                }

                Transform3D& local = outLocals[static_cast<size_t>(channel.targetNode)];
                if (channel.path == AnimationTargetPath::Translation) {
                    local.position =
                        SampleAnimationKeys<AnimationKeyframe<MATH::Vec3>, MATH::Vec3>(
                            channel.vec3Keys,
                            sampleTime,
                            channel.interpolation,
                            local.position);
                } else if (channel.path == AnimationTargetPath::Scale) {
                    local.scale =
                        SampleAnimationKeys<AnimationKeyframe<MATH::Vec3>, MATH::Vec3>(
                            channel.vec3Keys,
                            sampleTime,
                            channel.interpolation,
                            local.scale);
                } else if (channel.path == AnimationTargetPath::Rotation) {
                    local.rotation =
                        SampleAnimationKeys<AnimationKeyframe<MATH::Quat>, MATH::Quat>(
                            channel.quatKeys,
                            sampleTime,
                            channel.interpolation,
                            local.rotation);
                }
            }
        }

        MATH::Mat4 ResolveNodeLocalMatrix(
            const ModelNode& node,
            const std::vector<Transform3D>& animatedLocals,
            size_t nodeIndex) {

            if (node.hasLocalMatrix) {
                return node.localMatrix;
            }
            if (nodeIndex < animatedLocals.size()) {
                return animatedLocals[nodeIndex].GetLocalMatrix();
            }
            return node.localTransform.GetLocalMatrix();
        }

        void EvaluateNodeMatrixRecursive(
            const ModelAsset& model,
            const std::vector<Transform3D>& animatedLocals,
            int nodeIndex,
            const MATH::Mat4& parentWorld,
            std::vector<MATH::Mat4>& outGlobals,
            std::vector<uint8_t>& visited) {

            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size())) {
                return;
            }
            const size_t index = static_cast<size_t>(nodeIndex);
            if (visited[index]) {
                return;
            }

            const ModelNode& node = model.nodes[index];
            outGlobals[index] =
                parentWorld * ResolveNodeLocalMatrix(node, animatedLocals, index);
            visited[index] = 1u;

            for (int childIndex : node.children) {
                EvaluateNodeMatrixRecursive(
                    model,
                    animatedLocals,
                    childIndex,
                    outGlobals[index],
                    outGlobals,
                    visited);
            }
        }

        void BuildNodeGlobalMatricesWithRoot(
            const ModelAsset& model,
            const std::vector<Transform3D>& animatedLocals,
            const MATH::Mat4& rootWorld,
            std::vector<MATH::Mat4>& outGlobals,
            std::vector<uint8_t>& visited) {

            if (model.nodes.empty()) {
                outGlobals.clear();
                visited.clear();
                return;
            }

            outGlobals.assign(model.nodes.size(), rootWorld);
            visited.assign(model.nodes.size(), 0u);

            for (size_t i = 0; i < model.nodes.size(); ++i) {
                if (model.nodes[i].parent == -1) {
                    EvaluateNodeMatrixRecursive(
                        model,
                        animatedLocals,
                        static_cast<int>(i),
                        rootWorld,
                        outGlobals,
                        visited);
                }
            }
            for (size_t i = 0; i < model.nodes.size(); ++i) {
                if (visited[i]) {
                    continue;
                }
                const int parent = model.nodes[i].parent;
                const MATH::Mat4 parentWorld =
                    parent >= 0 && parent < static_cast<int>(outGlobals.size())
                        ? outGlobals[static_cast<size_t>(parent)]
                        : rootWorld;
                EvaluateNodeMatrixRecursive(
                    model,
                    animatedLocals,
                    static_cast<int>(i),
                    parentWorld,
                    outGlobals,
                    visited);
            }
        }

        bool BuildJointPalette(
            const ModelAsset& model,
            int skinIndex,
            const std::vector<MATH::Mat4>& localNodeGlobals,
            std::vector<MATH::Mat4>& outPalette) {

            outPalette.clear();
            const SkeletonAsset* skin = model.FindSkin(skinIndex);
            if (skin == nullptr) {
                return false;
            }

            outPalette.resize(skin->joints.size(), MATH::Mat4::Identity());
            for (size_t jointIndex = 0; jointIndex < skin->joints.size(); ++jointIndex) {
                const SkeletonJoint& joint = skin->joints[jointIndex];
                if (joint.nodeIndex < 0 ||
                    joint.nodeIndex >= static_cast<int>(localNodeGlobals.size())) {
                    continue;
                }
                outPalette[jointIndex] =
                    localNodeGlobals[static_cast<size_t>(joint.nodeIndex)] *
                    joint.inverseBindMatrix;
            }
            return true;
        }
    }

    GpuScenePoseBuilder::PoseEntry* GpuScenePoseBuilder::ResolvePoseEntry(
        const GpuSceneSurfaceRecord& record) {

        if (record.model == nullptr) {
            return nullptr;
        }

        for (PoseEntry& entry : entries_) {
            if (entry.objectId == record.objectId &&
                entry.objectVersion == record.objectVersion &&
                entry.model == record.model) {
                return &entry;
            }
        }

        PoseEntry entry{};
        entry.objectId = record.objectId;
        entry.objectVersion = record.objectVersion;
        entry.model = record.model;
        BuildAnimatedNodeLocals(record, entry.animatedLocals);
        BuildNodeGlobalMatricesWithRoot(
            *record.model,
            entry.animatedLocals,
            MATH::Mat4::Identity(),
            entry.localNodeGlobals,
            entry.visited);

        entries_.push_back(std::move(entry));
        return &entries_.back();
    }

    const std::vector<MATH::Mat4>* GpuScenePoseBuilder::ResolveJointPalette(
        const GpuSceneSurfaceRecord& record) {

        if (record.model == nullptr || record.surface == nullptr) {
            return nullptr;
        }
        const int skinIndex = record.surface->skinIndex;
        if (skinIndex < 0) {
            return nullptr;
        }

        PoseEntry* entry = ResolvePoseEntry(record);
        if (entry == nullptr) {
            return nullptr;
        }

        auto found = entry->palettesBySkin.find(skinIndex);
        if (found != entry->palettesBySkin.end()) {
            return &found->second;
        }

        std::vector<MATH::Mat4>& palette = entry->palettesBySkin[skinIndex];
        if (!BuildJointPalette(
            *record.model,
            skinIndex,
            entry->localNodeGlobals,
            palette)) {
            entry->palettesBySkin.erase(skinIndex);
            return nullptr;
        }
        return &palette;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
