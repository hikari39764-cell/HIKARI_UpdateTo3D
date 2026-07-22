#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Animation/Assets/HIKARI_AnimationAssetTypes.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI::ANIMATION {

    struct AnimationClipReference {
        AssetId modelAssetId{};
        AnimationClipId clipId{};
        std::string fallbackName{};

        bool IsEmpty() const noexcept {
            return !clipId.IsValid() && fallbackName.empty();
        }

        friend bool operator==(
            const AnimationClipReference&,
            const AnimationClipReference&) = default;
    };

    struct AnimationLocalPose {
        std::vector<Transform3D> nodes{};

        bool IsValidFor(size_t nodeCount) const noexcept {
            return !nodes.empty() && nodes.size() == nodeCount;
        }
    };

    struct AnimationRootMotionDelta {
        MATH::Vec3 translation{};
        MATH::Quat rotation = MATH::Quat::Identity();
        bool valid = false;
    };

    struct AnimationPoseSnapshot {
        AnimationLocalPose localPose{};
        AnimationClipReference primaryClip{};
        float normalizedTime = 0.0f;
        float blendWeight = 1.0f;
        AnimationRootMotionDelta rootMotion{};
        uint64_t revision = 0u;
        bool transitioning = false;
        bool valid = false;
    };

} // namespace HIKARI::ANIMATION
