#include "Scene/HIKARI_CinematicCameraPlayback.h"

#include "Scene/HIKARI_World.h"

namespace HIKARI {

    namespace {
        constexpr int kCinematicCameraPriority = 100;
    }

    CinematicPlaybackHandle CinematicCameraPlayback::Play(
        const SceneCinematicsSettings& settings,
        CinematicSequenceId sequenceId,
        CameraDirector& cameraDirector,
        const CinematicPlaybackOptions& options,
        float startTimeSeconds) {

        ReleaseCameraOverride(cameraDirector);
        if (!player_.Play(
                settings,
                sequenceId,
                startTimeSeconds,
                options)) {
            currentHandle_ = {};
            return {};
        }

        currentHandle_.value = nextHandleValue_++;
        if (nextHandleValue_ == 0) {
            nextHandleValue_ = 1;
        }
        currentHandle_.epoch = epoch_;
        return currentHandle_;
    }

    bool CinematicCameraPlayback::Stop(
        CinematicPlaybackHandle handle,
        CameraDirector& cameraDirector) {

        if (!IsCurrentHandle(handle)) {
            return false;
        }
        ReleaseCameraOverride(cameraDirector);
        player_.Clear();
        currentHandle_ = {};
        return true;
    }

    bool CinematicCameraPlayback::Pause(CinematicPlaybackHandle handle) {
        if (!IsCurrentHandle(handle) || !player_.IsPlaying()) {
            return false;
        }
        player_.Pause();
        return true;
    }

    bool CinematicCameraPlayback::Resume(CinematicPlaybackHandle handle) {
        return IsCurrentHandle(handle) && player_.Resume();
    }

    bool CinematicCameraPlayback::Seek(
        const SceneCinematicsSettings& settings,
        CinematicPlaybackHandle handle,
        float timeSeconds) {

        return IsCurrentHandle(handle) &&
            player_.Seek(settings, timeSeconds);
    }

    void CinematicCameraPlayback::Update(
        const SceneCinematicsSettings& settings,
        CameraDirector& cameraDirector,
        const World& world,
        float aspect,
        float deltaTime) {

        if (!currentHandle_.IsValid()) {
            ReleaseCameraOverride(cameraDirector);
            return;
        }

        const CinematicSequenceEvaluation evaluation =
            player_.Tick(settings, deltaTime);
        Camera3D resolvedCamera{};
        const bool cameraValid = evaluation.IsValid() &&
            cameraDirector.TryResolveCameraObject(
                world,
                evaluation.cameraObjectId,
                aspect,
                resolvedCamera);
        if (!cameraValid) {
            ReleaseCameraOverride(cameraDirector);
            return;
        }

        if (cameraOverrideToken_.IsValid() &&
            overriddenCameraObjectId_ == evaluation.cameraObjectId) {
            return;
        }

        ReleaseCameraOverride(cameraDirector);
        CameraActivationRequest request{};
        request.cameraObjectId = evaluation.cameraObjectId;
        request.blend.mode = CameraBlendMode::Cut;
        request.blend.durationSeconds = 0.0f;
        request.priority = kCinematicCameraPriority;
        request.affectsControlBasis = false;
        cameraOverrideToken_ = cameraDirector.PushOverride(request);
        if (cameraOverrideToken_.IsValid()) {
            overriddenCameraObjectId_ = evaluation.cameraObjectId;
        }
    }

    void CinematicCameraPlayback::Reset(CameraDirector& cameraDirector) {
        ReleaseCameraOverride(cameraDirector);
        player_.Clear();
        currentHandle_ = {};
        nextHandleValue_ = 1;
        AdvanceHandleEpoch();
    }

    bool CinematicCameraPlayback::IsCurrentHandle(
        CinematicPlaybackHandle handle) const noexcept {

        return handle.IsValid() &&
            currentHandle_.value == handle.value &&
            currentHandle_.epoch == handle.epoch;
    }

    bool CinematicCameraPlayback::IsPlaying() const noexcept {
        return currentHandle_.IsValid() && player_.IsPlaying();
    }

    CinematicPlaybackHandle CinematicCameraPlayback::GetCurrentHandle() const noexcept {
        return currentHandle_;
    }

    const CinematicSequencePlayer& CinematicCameraPlayback::GetPlayer() const noexcept {
        return player_;
    }

    void CinematicCameraPlayback::ReleaseCameraOverride(
        CameraDirector& cameraDirector) {

        if (cameraOverrideToken_.IsValid()) {
            (void)cameraDirector.ReleaseOverride(cameraOverrideToken_);
        }
        cameraOverrideToken_ = {};
        overriddenCameraObjectId_ = {};
    }

    void CinematicCameraPlayback::AdvanceHandleEpoch() {
        ++epoch_;
        if (epoch_ == 0) {
            epoch_ = 1;
        }
    }

} // namespace HIKARI
