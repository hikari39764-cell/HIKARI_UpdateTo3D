#include "Animation/Runtime/HIKARI_AnimationSystem.h"

#include <utility>
#include <unordered_set>

#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
#include "Core/HIKARI_FrameContext.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
    namespace {
        float ResolveClipDuration(
            const ModelAsset* model,
            const ANIMATION::AnimationClipReference& reference) {
            if (model == nullptr || reference.IsEmpty()) return -1.0f;
            const AnimationClip* clip = ANIMATION::ResolveAnimationClip(
                *model,
                reference);
            return clip != nullptr ? clip->durationSec : -1.0f;
        }
    }

    void AnimationSystem::OnWorldAttached(World& world) {
        publishedObjects_.clear();
        poseService_ = world.Services().Find<
            ANIMATION::AnimationPoseService>();
    }

    void AnimationSystem::OnWorldDetached(World&) {
        if (poseService_ != nullptr) poseService_->Clear();
        poseService_ = nullptr;
        publishedObjects_.clear();
    }

    void AnimationSystem::Update(
        World& world,
        const FrameContext& frame) {
        if (poseService_ == nullptr) return;

        std::unordered_set<uint64_t> currentObjectKeys{};
        std::vector<RuntimeObjectHandle> currentObjects{};

        world.ForEachObjectWith<ModelComponent, AnimatorComponent>(
            [this, &frame, &currentObjectKeys, &currentObjects](
                GameObject& object,
                ModelComponent& model,
                AnimatorComponent& animator) {
                const ModelAsset* modelAsset = model.GetModelAsset();
                if (modelAsset == nullptr) {
                    poseService_->Remove(object.GetRuntimeHandle());
                    return;
                }

                animator.BindClipToModel(*modelAsset);
                const float previousTimeSec = animator.GetTime();
                const bool previousPlaying = animator.IsPlaying();
                const bool previousFinished = animator.IsFinished();
                const bool previousTransitioning =
                    animator.IsTransitioning();
                animator.Advance(
                    frame.gameDt,
                    ResolveClipDuration(
                        modelAsset,
                        animator.GetClipReference()));

                ANIMATION::AnimationPoseSnapshot snapshot{};
                snapshot.primaryClip = animator.GetClipReference();
                snapshot.valid = ANIMATION::SampleAnimationClip(
                    *modelAsset,
                    animator.GetClipReference(),
                    animator.GetTime(),
                    animator.GetLoop(),
                    snapshot.localPose,
                    &snapshot.normalizedTime);
                snapshot.transitioning = animator.IsTransitioning();
                snapshot.blendWeight = animator.GetTransitionWeight();

                if (snapshot.valid && snapshot.transitioning) {
                    ANIMATION::AnimationLocalPose sourcePose{};
                    const bool sourceValid =
                        ANIMATION::SampleAnimationClip(
                            *modelAsset,
                            animator.GetTransitionSourceClip(),
                            animator.GetTransitionSourceTime(),
                            animator.GetTransitionSourceLoop(),
                            sourcePose);
                    if (sourceValid) {
                        ANIMATION::AnimationLocalPose blended{};
                        if (ANIMATION::BlendAnimationPoses(
                                sourcePose,
                                snapshot.localPose,
                                snapshot.blendWeight,
                                blended)) {
                            snapshot.localPose = std::move(blended);
                        }
                    }
                }

                if (!snapshot.valid) {
                    ANIMATION::BuildBindPose(
                        *modelAsset,
                        snapshot.localPose);
                }
                const bool poseChanged = poseService_->Publish(
                    object.GetRuntimeHandle(),
                    std::move(snapshot));
                currentObjectKeys.insert(
                    object.GetRuntimeHandle().ToValue());
                currentObjects.push_back(object.GetRuntimeHandle());
                if (poseChanged ||
                    animator.GetTime() != previousTimeSec ||
                    animator.IsPlaying() != previousPlaying ||
                    animator.IsFinished() != previousFinished ||
                    animator.IsTransitioning() !=
                        previousTransitioning) {
                    object.MarkRenderStateDirty();
                }
            });

        for (const RuntimeObjectHandle object : publishedObjects_) {
            if (!currentObjectKeys.contains(object.ToValue())) {
                poseService_->Remove(object);
            }
        }
        publishedObjects_ = std::move(currentObjects);
    }

} // namespace HIKARI
