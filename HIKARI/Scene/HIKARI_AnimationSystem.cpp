#include "HIKARI_AnimationSystem.h"

#include "Core/HIKARI_FrameContext.h"
#include "Render3D/HIKARI_ModelAsset.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    namespace {
        float ResolveClipDuration(const ModelAsset* model, const std::string& clipName) {
            if (model == nullptr || clipName.empty()) {
                return -1.0f;
            }

            const AnimationClip* clip = model->FindAnimationClip(clipName);
            return clip != nullptr ? clip->durationSec : -1.0f;
        }
    }

    void AnimationSystem::Update(World& world, const FrameContext& frame) {
        world.ForEachObjectWith<ModelComponent, AnimatorComponent>([&frame](GameObject& object, ModelComponent& model, AnimatorComponent& animator) {
            const float previousTimeSec = animator.GetTime();
            const bool previousPlaying = animator.IsPlaying();
            const bool previousFinished = animator.IsFinished();
            const float clipDurationSec = ResolveClipDuration(model.GetModelAsset(), animator.GetClip());
            animator.Advance(frame.gameDt, clipDurationSec);
            if (animator.GetTime() != previousTimeSec ||
                animator.IsPlaying() != previousPlaying ||
                animator.IsFinished() != previousFinished) {
                object.MarkRenderStateDirty();
            }
        });
    }

} // namespace HIKARI
