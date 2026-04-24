#include "Render3D/Render/HIKARI_ModelRenderer.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/HIKARI_ModelAsset.h"

namespace HIKARI::MODELRENDERER {

    namespace {
        std::vector<ModelRenderItem> gQueue;

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
