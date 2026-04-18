#pragma once
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    class Camera3D {
    public:
        void SetPerspective(float fovYRad, float aspect, float nearZ, float farZ);
        void SetLookAt(const MATH::Vec3& eye, const MATH::Vec3& target, const MATH::Vec3& up = { 0.0f, 1.0f, 0.0f });

        const MATH::Mat4& GetView() const { return view_; }
        const MATH::Mat4& GetProj() const { return proj_; }
        MATH::Mat4 GetViewProj() const { return proj_ * view_; }
        MATH::Vec3 GetPosition() const { return eye_; }

    private:
        MATH::Vec3 eye_{ 0.0f, 0.0f, -5.0f };
        MATH::Vec3 target_{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 up_{ 0.0f, 1.0f, 0.0f };

        MATH::Mat4 view_ = MATH::Mat4::Identity();
        MATH::Mat4 proj_ = MATH::Mat4::Identity();
    };

} // namespace HIKARI
