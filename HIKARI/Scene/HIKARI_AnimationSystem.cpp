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
        world.ForEachObjectWith<ModelComponent, AnimatorComponent>([&frame](GameObject&, ModelComponent& model, AnimatorComponent& animator) {
            const float clipDurationSec = ResolveClipDuration(model.GetModelAsset(), animator.GetClip());
            animator.Advance(frame.gameDt, clipDurationSec);
        });
    }

} // namespace HIKARI
