#include "HIKARI_CameraFollowSystem.h"

#include <algorithm>
#include <cmath>

#include "Core/HIKARI_FrameContext.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    namespace {
        MATH::Vec3 Lerp(const MATH::Vec3& from, const MATH::Vec3& to, float t) {
            return from + (to - from) * t;
        }

        float SmoothStepFactor(float smooth, float dt) {
            if (smooth <= 0.0f || dt <= 0.0f) {
                return 1.0f;
            }
            return (std::clamp)(1.0f - std::exp(-smooth * dt), 0.0f, 1.0f);
        }

        GameObject* FindObjectByDocumentId(World& world, SceneObjectId id) {
            if (id.value == 0u) {
                return nullptr;
            }

            for (const auto& object : world.GetObjects()) {
                if (object && object->GetDocumentId() == id) {
                    return object.get();
                }
            }
            return nullptr;
        }

        GameObject* ResolveTargetObject(World& world, GameObject& owner, const CameraFollowComponent& component) {
            if (GameObject* target = FindObjectByDocumentId(world, component.GetTargetObjectId())) {
                return target;
            }
            return component.GetUseOwnerAsFallbackTarget() ? &owner : nullptr;
        }
    }

    CameraFollowSystem::CameraFollowSystem(Camera3D& camera)
        : camera_(&camera) {}

    void CameraFollowSystem::Update(World& world, const FrameContext& frame) {
        if (camera_ == nullptr) {
            return;
        }

        bool applied = false;
        world.ForEachObjectWith<CameraFollowComponent>(
            [&](GameObject& owner, CameraFollowComponent& component) {
                if (applied || !component.IsEnabled()) {
                    if (!component.IsEnabled()) {
                        component.ResetRuntimeCameraState();
                    }
                    return;
                }

                GameObject* target = ResolveTargetObject(world, owner, component);
                if (target == nullptr) {
                    component.ResetRuntimeCameraState();
                    return;
                }

                const MATH::Vec3 targetPosition = target->Transform().position;
                MATH::Vec3 desiredEye = targetPosition + component.GetOffset();
                MATH::Vec3 desiredLookAt = targetPosition + component.GetLookAtOffset();
                if (MATH::Length(desiredLookAt - desiredEye) <= 1e-4f) {
                    desiredLookAt.z += 1.0f;
                }

                MATH::Vec3 eye = desiredEye;
                MATH::Vec3 lookAt = desiredLookAt;
                if (component.HasRuntimeCameraState()) {
                    const float dt = (std::max)(frame.gameDt, 0.0f);
                    eye = Lerp(component.GetRuntimeEye(), desiredEye, SmoothStepFactor(component.GetFollowSmooth(), dt));
                    lookAt = Lerp(component.GetRuntimeLookAt(), desiredLookAt, SmoothStepFactor(component.GetLookSmooth(), dt));
                }

                component.SetRuntimeCameraState(eye, lookAt);
                camera_->SetLookAt(eye, lookAt);
                applied = true;
            });
    }

} // namespace HIKARI
