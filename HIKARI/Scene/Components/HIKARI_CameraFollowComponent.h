#pragma once

#include <string_view>

#include "HIKARI_IComponent.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    class CameraFollowComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override { return "CameraFollowComponent"; }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const;
        SceneObjectId GetTargetObjectId() const;
        bool GetUseOwnerAsFallbackTarget() const;
        const MATH::Vec3& GetOffset() const;
        const MATH::Vec3& GetLookAtOffset() const;
        float GetFollowSmooth() const;
        float GetLookSmooth() const;

        bool HasRuntimeCameraState() const;
        const MATH::Vec3& GetRuntimeEye() const;
        const MATH::Vec3& GetRuntimeLookAt() const;
        void SetRuntimeCameraState(const MATH::Vec3& eye, const MATH::Vec3& lookAt);
        void ResetRuntimeCameraState();

    private:
        bool enabled_ = true;
        SceneObjectId targetObjectId_{};
        bool useOwnerAsFallbackTarget_ = true;
        MATH::Vec3 offset_{ 0.0f, 5.5f, -7.5f };
        MATH::Vec3 lookAtOffset_{ 0.0f, 1.2f, 0.0f };
        float followSmooth_ = 10.0f;
        float lookSmooth_ = 12.0f;

        bool runtimeInitialized_ = false;
        MATH::Vec3 runtimeEye_{};
        MATH::Vec3 runtimeLookAt_{};
    };

} // namespace HIKARI
