#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "Animation/StateMachine/HIKARI_AnimationStateMachineInstance.h"

namespace {
    HIKARI::ANIMATION::AnimationClipReference MakeClip(
        std::string name,
        size_t ordinal) {
        HIKARI::ANIMATION::AnimationClipReference reference{};
        reference.modelAssetId.value = "model-guid";
        reference.clipId = HIKARI::MakeAnimationClipId(name, ordinal);
        reference.fallbackName = std::move(name);
        return reference;
    }

    bool HasValidationError(
        const HIKARI::ANIMATION::AnimationStateMachineDefinition& definition) {
        for (const auto& issue :
                HIKARI::ANIMATION::ValidateAnimationStateMachine(definition)) {
            if (issue.error) return true;
        }
        return false;
    }
}

int main() {
    using namespace HIKARI;
    using namespace HIKARI::ANIMATION;

    AnimationStateMachineDefinition definition{};
    definition.name = "Character Locomotion";
    definition.previewModelAssetId.value = "model-guid";

    AnimationParameterDefinition speed{};
    speed.id = { 1u };
    speed.name = "Speed";
    speed.type = AnimationParameterType::Float;
    speed.defaultValue = 0.0f;
    definition.parameters.push_back(speed);

    AnimationParameterDefinition jump{};
    jump.id = { 2u };
    jump.name = "Jump";
    jump.type = AnimationParameterType::Trigger;
    jump.defaultValue = false;
    definition.parameters.push_back(jump);

    AnimationState idle{};
    idle.id = { 1u };
    idle.name = "Idle";
    idle.motion = AnimationClipMotion{ MakeClip("Idle", 0u) };
    definition.states.push_back(idle);

    AnimationState run{};
    run.id = { 2u };
    run.name = "Run";
    AnimationBlendTree1DMotion locomotion{};
    locomotion.parameterId = speed.id;
    locomotion.samples.push_back({ MakeClip("Walk", 1u), 0.0f, 1.0f });
    locomotion.samples.push_back({ MakeClip("Run", 2u), 2.0f, 1.2f });
    run.motion = locomotion;
    definition.states.push_back(run);

    AnimationState jumping{};
    jumping.id = { 3u };
    jumping.name = "Jump";
    jumping.motion = AnimationClipMotion{ MakeClip("Jump", 3u) };
    jumping.loop = false;
    definition.states.push_back(jumping);
    definition.entryStateId = idle.id;

    AnimationStateTransition toRun{};
    toRun.id = { 1u };
    toRun.sourceStateId = idle.id;
    toRun.targetStateId = run.id;
    toRun.priority = 10;
    toRun.conditions.push_back({
        speed.id,
        AnimationConditionOperator::Greater,
        0.2f
    });
    definition.transitions.push_back(toRun);

    AnimationStateTransition anyToJump{};
    anyToJump.id = { 2u };
    anyToJump.targetStateId = jumping.id;
    anyToJump.priority = 100;
    anyToJump.conditions.push_back({
        jump.id,
        AnimationConditionOperator::Triggered,
        false
    });
    definition.transitions.push_back(anyToJump);

    AnimationStateTransition jumpToIdle{};
    jumpToIdle.id = { 3u };
    jumpToIdle.sourceStateId = jumping.id;
    jumpToIdle.targetStateId = idle.id;
    jumpToIdle.priority = 5;
    jumpToIdle.requireExitTime = true;
    jumpToIdle.exitTimeNormalized = 0.9f;
    definition.transitions.push_back(jumpToIdle);

    NormalizeAnimationStateMachine(definition);
    if (HasValidationError(definition)) {
        std::cerr << "valid state machine reported an error\n";
        return 1;
    }

    const AnimationParameterValue blendValue = 1.0f;
    const AnimationStateMotionEvaluation blendEvaluation =
        EvaluateAnimationStateMotion(run.motion, &blendValue);
    if (!blendEvaluation.valid ||
        blendEvaluation.sample.primaryClip.fallbackName != "Walk" ||
        blendEvaluation.sample.secondaryClip.fallbackName != "Run" ||
        std::abs(blendEvaluation.sample.secondaryWeight - 0.5f) > 0.001f ||
        std::abs(blendEvaluation.speedScale - 1.1f) > 0.001f) {
        std::cerr << "1D blend tree evaluation failed\n";
        return 1;
    }

    auto asset = std::make_shared<AnimationStateMachineAsset>();
    asset->guid.value = "state-machine-guid";
    asset->displayName = definition.name;
    asset->definition = definition;

    AnimationStateMachineInstance instance{};
    if (!instance.Bind(asset) ||
        instance.GetCurrentState() == nullptr ||
        instance.GetCurrentState()->id != idle.id) {
        std::cerr << "entry state binding failed\n";
        return 1;
    }
    if (!instance.Start() ||
        instance.ConsumePlaybackRequest() !=
            AnimationStateMachinePlaybackRequest::Start) {
        std::cerr << "explicit playback start failed\n";
        return 1;
    }
    if (!instance.SetFloat("Speed", 1.0f) ||
        !instance.FireTrigger("Jump")) {
        std::cerr << "parameter writes failed\n";
        return 1;
    }

    const AnimationStateTransition* selected = instance.Evaluate(0.0f, false);
    if (selected == nullptr || selected->id != anyToJump.id) {
        std::cerr << "transition priority or Any State evaluation failed\n";
        return 1;
    }
    if (!instance.CommitTransition(selected->targetStateId) ||
        instance.GetCurrentState()->id != jumping.id) {
        std::cerr << "transition commit failed\n";
        return 1;
    }
    instance.ClearTriggers();
    const auto* triggerValue = instance.FindValue(jump.id);
    if (triggerValue == nullptr || std::get<bool>(*triggerValue)) {
        std::cerr << "trigger lifetime contract failed\n";
        return 1;
    }
    if (instance.Evaluate(0.5f, false) != nullptr) {
        std::cerr << "exit time fired too early\n";
        return 1;
    }
    selected = instance.Evaluate(0.95f, false);
    if (selected == nullptr || selected->id != jumpToIdle.id) {
        std::cerr << "exit time transition failed\n";
        return 1;
    }

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        "hikari-animation-state-machine-smoke.hanimsm";
    std::string message{};
    if (!SaveAnimationStateMachineAsset(path, *asset, &message)) {
        std::cerr << "asset save failed: " << message << '\n';
        return 1;
    }
    AnimationStateMachineAsset loaded{};
    if (!LoadAnimationStateMachineAsset(
            path,
            asset->guid,
            loaded,
            &message)) {
        std::cerr << "asset load failed: " << message << '\n';
        return 1;
    }
    std::error_code removeError{};
    std::filesystem::remove(path, removeError);
    const AnimationState* loadedRun = FindAnimationState(
        loaded.definition,
        run.id);
    const auto* loadedBlend = loadedRun != nullptr
        ? std::get_if<AnimationBlendTree1DMotion>(&loadedRun->motion)
        : nullptr;
    if (loadedBlend == nullptr || loadedBlend->samples.size() != 2u ||
        loadedBlend->samples[1].clip.fallbackName != "Run" ||
        loaded.definition.transitions.size() != 3u) {
        std::cerr << "asset round trip lost motion or transition data\n";
        return 1;
    }

    std::cout << "Animation state machine runtime and asset round trip passed\n";
    return 0;
}
