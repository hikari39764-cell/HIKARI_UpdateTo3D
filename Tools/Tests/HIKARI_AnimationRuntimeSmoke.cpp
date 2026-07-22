#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
#include "Assets/Formats/HIKARI_HmodelFormat.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"

namespace {
    bool NearlyEqual(float lhs, float rhs) {
        return std::abs(lhs - rhs) <= 1.0e-4f;
    }

    HIKARI::AnimationClip MakeTranslationClip(
        std::string name,
        HIKARI::AnimationInterpolation interpolation) {
        HIKARI::AnimationClip clip{};
        clip.name = std::move(name);
        clip.durationSec = 2.0f;

        HIKARI::NodeAnimationChannel channel{};
        channel.targetNode = 0;
        channel.path = HIKARI::AnimationTargetPath::Translation;
        channel.interpolation = interpolation;

        HIKARI::AnimationKeyframe<HIKARI::MATH::Vec3> first{};
        first.timeSec = 0.0f;
        first.value = { 0.0f, 0.0f, 0.0f };
        first.outTangent = { 2.0f, 0.0f, 0.0f };
        HIKARI::AnimationKeyframe<HIKARI::MATH::Vec3> second{};
        second.timeSec = 2.0f;
        second.value = { 4.0f, 0.0f, 0.0f };
        second.inTangent = { 2.0f, 0.0f, 0.0f };
        channel.vec3Keys = { first, second };
        clip.channels.push_back(std::move(channel));
        return clip;
    }
}

int main() {
    using namespace HIKARI;
    using namespace HIKARI::ANIMATION;

    ModelAsset model{};
    model.SetName("animation-smoke-model");
    model.nodes.resize(1u);
    model.animations.push_back(MakeTranslationClip(
        "Move",
        AnimationInterpolation::Linear));
    model.animations.push_back(MakeTranslationClip(
        "Move",
        AnimationInterpolation::Linear));
    model.animations.push_back(MakeTranslationClip(
        "Curve",
        AnimationInterpolation::CubicSpline));

    const AnimationClipReference move =
        MakeAnimationClipReference(model, 0u);
    const AnimationClipId duplicateId = model.GetAnimationClipId(1u);
    if (!move.clipId.IsValid() || move.clipId == duplicateId) {
        std::cerr << "duplicate clip IDs are not unique\n";
        return 1;
    }

    model.animations.insert(
        model.animations.begin(),
        MakeTranslationClip("Unrelated", AnimationInterpolation::Linear));
    const AnimationClip* resolved = ResolveAnimationClip(model, move);
    if (resolved == nullptr || resolved->name != "Move") {
        std::cerr << "clip reference did not survive unrelated insertion\n";
        return 1;
    }

    AnimationLocalPose pose{};
    float normalizedTime = 0.0f;
    if (!SampleAnimationClip(
            model,
            move,
            1.0f,
            false,
            pose,
            &normalizedTime) ||
        !NearlyEqual(pose.nodes[0].position.x, 2.0f) ||
        !NearlyEqual(normalizedTime, 0.5f)) {
        std::cerr << "linear sampling failed\n";
        return 1;
    }

    if (!SampleAnimationClip(
            model,
            move,
            2.5f,
            true,
            pose,
            nullptr) ||
        !NearlyEqual(pose.nodes[0].position.x, 1.0f)) {
        std::cerr << "looped sampling failed\n";
        return 1;
    }

    const AnimationClipReference curve =
        MakeAnimationClipReference(model, 3u);
    if (!SampleAnimationClip(
            model,
            curve,
            1.0f,
            false,
            pose,
            nullptr) ||
        !NearlyEqual(pose.nodes[0].position.x, 2.0f)) {
        std::cerr << "cubic spline sampling failed\n";
        return 1;
    }

    AnimationLocalPose from{};
    AnimationLocalPose to{};
    from.nodes.resize(1u);
    to.nodes.resize(1u);
    to.nodes[0].position.x = 4.0f;
    AnimationLocalPose blended{};
    if (!BlendAnimationPoses(from, to, 0.25f, blended) ||
        !NearlyEqual(blended.nodes[0].position.x, 1.0f)) {
        std::cerr << "pose blending failed\n";
        return 1;
    }

    AnimationPoseService service{};
    constexpr RuntimeObjectHandle object{ 7u, 3u };
    AnimationPoseSnapshot snapshot{};
    snapshot.primaryClip = move;
    snapshot.localPose = pose;
    snapshot.valid = true;
    if (!service.Publish(object, snapshot) ||
        service.Find(object) == nullptr ||
        service.Find(object)->revision != 1u ||
        service.Publish(object, snapshot)) {
        std::cerr << "pose publication revision contract failed\n";
        return 1;
    }
    snapshot.rootMotion.valid = true;
    snapshot.rootMotion.translation.x = 0.5f;
    if (!service.Publish(object, snapshot) ||
        service.Find(object)->revision != 2u) {
        std::cerr << "root motion did not invalidate the pose snapshot\n";
        return 1;
    }

    ModelAsset otherModel{};
    otherModel.SetName("other-model");
    otherModel.animations.push_back(MakeTranslationClip(
        "Move",
        AnimationInterpolation::Linear));
    if (ResolveAnimationClip(otherModel, move) != nullptr) {
        std::cerr << "clip reference crossed its model asset boundary\n";
        return 1;
    }

    model.meshes.emplace_back();
    const std::filesystem::path roundTripPath =
        std::filesystem::temp_directory_path() /
        "hikari-animation-runtime-v4.hmodel";
    std::string formatMessage{};
    if (!WriteHmodelFile(roundTripPath, model, formatMessage)) {
        std::cerr << "HMODEL v4 write failed: " << formatMessage << '\n';
        return 1;
    }
    ModelAsset roundTripped{};
    if (!ReadHmodelFile(roundTripPath, roundTripped, formatMessage)) {
        std::cerr << "HMODEL v4 read failed: " << formatMessage << '\n';
        return 1;
    }
    std::error_code removeError{};
    std::filesystem::remove(roundTripPath, removeError);
    const AnimationClip* roundTrippedCurve =
        roundTripped.FindAnimationClip("Curve");
    if (roundTrippedCurve == nullptr ||
        roundTrippedCurve->channels.empty() ||
        roundTrippedCurve->channels[0].vec3Keys.size() != 2u ||
        !NearlyEqual(
            roundTrippedCurve->channels[0]
                .vec3Keys[0].outTangent.x,
            2.0f)) {
        std::cerr << "HMODEL v4 animation tangent round trip failed\n";
        return 1;
    }

    std::cout << "Animation runtime sampling, blending, publication, and HMODEL v4 passed\n";
    return 0;
}
