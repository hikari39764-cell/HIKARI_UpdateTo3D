#include "HIKARI_DebugCameraController3D.h"
#include <algorithm>
#include <cmath>
#include <Windows.h>
#undef min
#undef max
#include "HIKARI_Input.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Editor/HIKARI_EditorViewportInput.h"
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {
        MATH::Vec3 ComputeForward(float yaw, float pitch) {
            const float cp = std::cos(pitch);
            const float sp = std::sin(pitch);
            const float cy = std::cos(yaw);
            const float sy = std::sin(yaw);
            return MATH::Normalize({ sy * cp, sp, cy * cp });
        }
    }

    void DebugCameraController3D::SetEnabled(bool enabled) {
        enabled_ = enabled;
    }

    bool DebugCameraController3D::IsEnabled() const {
        return enabled_;
    }

    void DebugCameraController3D::Reset(const MATH::Vec3& position, float yawRad, float pitchRad) {
        resetPosition_ = position;
        resetYaw_ = yawRad;
        resetPitch_ = ClampPitch(pitchRad);

        position_ = resetPosition_;
        yaw_ = resetYaw_;
        pitch_ = resetPitch_;
    }

    void DebugCameraController3D::Update(float dt, Camera3D& camera) {
        if (!enabled_) {
            ApplyToCamera(camera);
            return;
        }

        bool wantMouse = false;
        bool wantKeyboard = false;
        bool acceptWheel = true;
#if defined(HIKARI_WITH_EDITOR)
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGuiIO& io = ImGui::GetIO();
            wantMouse = io.WantCaptureMouse;
            wantKeyboard = io.WantCaptureKeyboard;
            acceptWheel = !wantMouse;

            if (EDITOR::HasGameViewportInputRect()) {
                const bool viewportMouseActive = EDITOR::IsGameViewportMouseInputActive();
                const bool viewportKeyboardActive = EDITOR::IsGameViewportKeyboardInputActive();
                const bool viewportWheelActive = EDITOR::IsGameViewportWheelInputActive();

                wantMouse = !viewportMouseActive;
                wantKeyboard = !viewportKeyboardActive;
                acceptWheel = viewportWheelActive;
            }
        }
#endif

        // エディタ操作キー(W/E/R/Q)と衝突しないよう、カメラリセットは Home に寄せる。
        if (!wantKeyboard && (::GetAsyncKeyState(VK_HOME) & 0x8000) != 0) {
            position_ = resetPosition_;
            yaw_ = resetYaw_;
            pitch_ = resetPitch_;
        }

        const bool rightMouseDown = HINPUT::IsMouseDown(HINPUT::MouseButton::Right);
        if (!wantMouse && rightMouseDown) {
            const Vector2 delta = HINPUT::GetMouseDelta();
            yaw_ += delta.x * settings_.mouseLookSensitivity;
            pitch_ -= delta.y * settings_.mouseLookSensitivity;
            pitch_ = ClampPitch(pitch_);
        }

        const MATH::Vec3 forward = ComputeForward(yaw_, pitch_);
        const MATH::Vec3 worldUp{ 0.0f, 1.0f, 0.0f };
        const MATH::Vec3 right = MATH::Normalize(MATH::Cross(worldUp, forward));

        // WASD/QE は右クリック中だけ飛行カメラとして扱う。
        if (!wantKeyboard && rightMouseDown) {
            MATH::Vec3 move{};
            if ((::GetAsyncKeyState('W') & 0x8000) != 0) move = move + forward;
            if ((::GetAsyncKeyState('S') & 0x8000) != 0) move = move - forward;
            if ((::GetAsyncKeyState('D') & 0x8000) != 0) move = move + right;
            if ((::GetAsyncKeyState('A') & 0x8000) != 0) move = move - right;
            if ((::GetAsyncKeyState('E') & 0x8000) != 0) move = move + worldUp;
            if ((::GetAsyncKeyState('Q') & 0x8000) != 0) move = move - worldUp;

            const bool fast = ((::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
            float speed = settings_.moveSpeed;
            if (fast) {
                speed *= settings_.fastMultiplier;
            }

            const float moveLen = MATH::Length(move);
            if (moveLen > 1e-5f) {
                position_ = position_ + MATH::Normalize(move) * (speed * dt);
            }
        }

        if (acceptWheel) {
            const float wheel = HINPUT::GetMouseWheelDelta();
            if (std::abs(wheel) > 1e-6f) {
                position_ = position_ + forward * (wheel * settings_.wheelMoveStep);
            }
        }

        ApplyToCamera(camera);
    }

    const MATH::Vec3& DebugCameraController3D::GetPosition() const {
        return position_;
    }

    float DebugCameraController3D::GetYaw() const {
        return yaw_;
    }

    float DebugCameraController3D::GetPitch() const {
        return pitch_;
    }

    void DebugCameraController3D::SetPosition(const MATH::Vec3& position) {
        position_ = position;
    }

    void DebugCameraController3D::SetYaw(float yawRad) {
        yaw_ = yawRad;
    }

    void DebugCameraController3D::SetPitch(float pitchRad) {
        pitch_ = ClampPitch(pitchRad);
    }

    DebugCamera3DSettings& DebugCameraController3D::Settings() {
        return settings_;
    }

    const DebugCamera3DSettings& DebugCameraController3D::Settings() const {
        return settings_;
    }

    void DebugCameraController3D::ApplyToCamera(Camera3D& camera) const {
        const MATH::Vec3 forward = ComputeForward(yaw_, pitch_);
        camera.SetLookAt(position_, position_ + forward);
    }

    float DebugCameraController3D::ClampPitch(float pitch) const {
        return std::clamp(pitch, -settings_.pitchLimitRad, settings_.pitchLimitRad);
    }

} // namespace HIKARI
