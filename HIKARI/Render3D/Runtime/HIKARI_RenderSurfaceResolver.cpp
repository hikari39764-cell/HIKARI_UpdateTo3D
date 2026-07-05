#include "Render3D/Runtime/HIKARI_RenderSurfaceResolver.h"

#include <algorithm>
#include <cmath>

#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::RENDER3D::RUNTIME {

    namespace {
        MATH::Mat4 BuildNodeGlobalRecursive(
            const RenderModelAsset& renderModel,
            size_t nodeIndex,
            std::vector<MATH::Mat4>& globals,
            std::vector<uint8_t>& visited) {

            if (nodeIndex >= renderModel.nodes.size()) {
                return MATH::Mat4::Identity();
            }
            if (visited[nodeIndex]) {
                return globals[nodeIndex];
            }

            const RenderModelNodeRecord& node = renderModel.nodes[nodeIndex];
            MATH::Mat4 parent = MATH::Mat4::Identity();
            if (node.parentIndex >= 0 && node.parentIndex < static_cast<int>(renderModel.nodes.size())) {
                parent = BuildNodeGlobalRecursive(renderModel, static_cast<size_t>(node.parentIndex), globals, visited);
            }

            globals[nodeIndex] = parent * node.localMatrix;
            visited[nodeIndex] = 1u;
            return globals[nodeIndex];
        }

        template<typename TValue>
        TValue LerpAnimationValue(const TValue& a, const TValue& b, float t);

        template<>
        MATH::Vec3 LerpAnimationValue(const MATH::Vec3& a, const MATH::Vec3& b, float t) {
            return {
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t,
            };
        }

        template<>
        MATH::Quat LerpAnimationValue(const MATH::Quat& a, const MATH::Quat& b, float t) {
            MATH::Quat end = b;
            const float dot =
                a.x * b.x +
                a.y * b.y +
                a.z * b.z +
                a.w * b.w;
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
                a.w + (end.w - a.w) * t,
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
                [](const TKey& key, float t) {
                    return key.timeSec < t;
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
            return LerpAnimationValue<TValue>(a.value, b.value, t);
        }

        const AnimationClip* ResolveAnimationClip(
            const ModelAsset* model,
            std::string_view animationClipName) {

            if (model == nullptr || animationClipName.empty()) {
                return nullptr;
            }
            return model->FindAnimationClip(animationClipName);
        }

        float ResolveAnimationSampleTime(
            float animationTimeSec,
            bool animationLoop,
            const AnimationClip& clip) {

            float sampleTime = animationTimeSec;
            if (animationLoop && clip.durationSec > 0.0001f) {
                sampleTime = std::fmod(sampleTime, clip.durationSec);
                if (sampleTime < 0.0f) {
                    sampleTime += clip.durationSec;
                }
            }
            return sampleTime;
        }

        std::vector<Transform3D> BuildAnimatedNodeLocals(
            const ModelAsset* model,
            std::string_view animationClipName,
            float animationTimeSec,
            bool animationLoop) {

            std::vector<Transform3D> locals{};
            if (model == nullptr) {
                return locals;
            }

            locals.reserve(model->nodes.size());
            for (const ModelNode& node : model->nodes) {
                locals.push_back(node.localTransform);
            }

            const AnimationClip* clip = ResolveAnimationClip(model, animationClipName);
            if (clip == nullptr || clip->channels.empty()) {
                return locals;
            }

            const float sampleTime =
                ResolveAnimationSampleTime(animationTimeSec, animationLoop, *clip);
            for (const NodeAnimationChannel& channel : clip->channels) {
                if (channel.targetNode < 0 ||
                    channel.targetNode >= static_cast<int>(locals.size())) {
                    continue;
                }

                Transform3D& local = locals[static_cast<size_t>(channel.targetNode)];
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
            return locals;
        }

        MATH::Mat4 ResolveAnimatedRenderNodeLocalMatrix(
            const RenderModelNodeRecord& renderNode,
            const ModelAsset* model,
            const std::vector<Transform3D>& animatedLocals) {

            if (model == nullptr || renderNode.nodeIndex >= model->nodes.size()) {
                return renderNode.localMatrix;
            }

            const ModelNode& sourceNode = model->nodes[renderNode.nodeIndex];
            if (sourceNode.hasLocalMatrix) {
                return sourceNode.localMatrix;
            }
            if (renderNode.nodeIndex < animatedLocals.size()) {
                return animatedLocals[renderNode.nodeIndex].GetLocalMatrix();
            }
            return sourceNode.localTransform.GetLocalMatrix();
        }

        MATH::Mat4 BuildNodeGlobalRecursive(
            const RenderModelAsset& renderModel,
            const std::vector<MATH::Mat4>& localMatrices,
            size_t nodeIndex,
            std::vector<MATH::Mat4>& globals,
            std::vector<uint8_t>& visited) {

            if (nodeIndex >= renderModel.nodes.size()) {
                return MATH::Mat4::Identity();
            }
            if (visited[nodeIndex]) {
                return globals[nodeIndex];
            }

            const RenderModelNodeRecord& node = renderModel.nodes[nodeIndex];
            MATH::Mat4 parent = MATH::Mat4::Identity();
            if (node.parentIndex >= 0 && node.parentIndex < static_cast<int>(renderModel.nodes.size())) {
                parent = BuildNodeGlobalRecursive(
                    renderModel,
                    localMatrices,
                    static_cast<size_t>(node.parentIndex),
                    globals,
                    visited);
            }

            globals[nodeIndex] = parent *
                (nodeIndex < localMatrices.size()
                    ? localMatrices[nodeIndex]
                    : node.localMatrix);
            visited[nodeIndex] = 1u;
            return globals[nodeIndex];
        }
    }

    std::vector<MATH::Mat4> BuildRenderModelNodeGlobals(const RenderModelAsset& renderModel) {
        std::vector<MATH::Mat4> globals(renderModel.nodes.size(), MATH::Mat4::Identity());
        std::vector<uint8_t> visited(renderModel.nodes.size(), 0u);
        for (size_t nodeIndex = 0; nodeIndex < renderModel.nodes.size(); ++nodeIndex) {
            (void)BuildNodeGlobalRecursive(renderModel, nodeIndex, globals, visited);
        }
        return globals;
    }

    std::vector<MATH::Mat4> BuildRenderModelNodeGlobals(
        const RenderModelAsset& renderModel,
        const ModelAsset* model,
        std::string_view animationClipName,
        float animationTimeSec,
        bool animationLoop) {

        if (model == nullptr ||
            animationClipName.empty() ||
            renderModel.nodes.empty()) {
            return BuildRenderModelNodeGlobals(renderModel);
        }

        const std::vector<Transform3D> animatedLocals =
            BuildAnimatedNodeLocals(
                model,
                animationClipName,
                animationTimeSec,
                animationLoop);
        if (animatedLocals.empty()) {
            return BuildRenderModelNodeGlobals(renderModel);
        }

        std::vector<MATH::Mat4> localMatrices(
            renderModel.nodes.size(),
            MATH::Mat4::Identity());
        for (size_t nodeIndex = 0; nodeIndex < renderModel.nodes.size(); ++nodeIndex) {
            localMatrices[nodeIndex] =
                ResolveAnimatedRenderNodeLocalMatrix(
                    renderModel.nodes[nodeIndex],
                    model,
                    animatedLocals);
        }

        std::vector<MATH::Mat4> globals(renderModel.nodes.size(), MATH::Mat4::Identity());
        std::vector<uint8_t> visited(renderModel.nodes.size(), 0u);
        for (size_t nodeIndex = 0; nodeIndex < renderModel.nodes.size(); ++nodeIndex) {
            (void)BuildNodeGlobalRecursive(
                renderModel,
                localMatrices,
                nodeIndex,
                globals,
                visited);
        }
        return globals;
    }

    bool ResolveRenderSurfaceDrawWorldMatrix(
        const Transform3D& objectWorldTransform,
        const RenderSurfaceRecord& surface,
        const std::vector<MATH::Mat4>& nodeGlobals,
        MATH::Mat4& outDrawWorldMatrix) {

        outDrawWorldMatrix = objectWorldTransform.GetWorldMatrix();
        if (surface.nodeIndex == kInvalidRenderModelIndex) {
            return true;
        }
        if (surface.nodeIndex >= nodeGlobals.size()) {
            return false;
        }

        outDrawWorldMatrix = outDrawWorldMatrix * nodeGlobals[surface.nodeIndex];
        return true;
    }

    Bounds ResolveRenderSurfaceWorldBounds(
        const Bounds& fallbackWorldBounds,
        const RenderSurfaceRecord& surface,
        const MATH::Mat4& drawWorldMatrix,
        bool hasDrawWorldMatrix) {

        if (!hasDrawWorldMatrix || !BOUNDS::IsUsable(surface.localBounds)) {
            return fallbackWorldBounds;
        }

        const Bounds worldBounds = BOUNDS::TransformBounds(surface.localBounds, drawWorldMatrix);
        return BOUNDS::IsUsable(worldBounds) ? worldBounds : fallbackWorldBounds;
    }

    bool HasValidRenderSurfacePrimitive(
        const ModelAsset* model,
        const RenderSurfaceRecord& surface) {

        if (model == nullptr || !surface.HasMeshPrimitive() || surface.meshIndex >= model->meshes.size()) {
            return false;
        }

        const MeshAsset& mesh = model->meshes[surface.meshIndex];
        return surface.primitiveIndex < mesh.primitives.size();
    }

} // namespace HIKARI::RENDER3D::RUNTIME
