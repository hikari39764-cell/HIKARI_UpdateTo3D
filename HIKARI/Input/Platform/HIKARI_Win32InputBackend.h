#pragma once

#include "Input/Platform/HIKARI_IInputBackend.h"

namespace HIKARI::INPUT {

class Win32InputBackend final : public IInputBackend {
public:
    void SetHostWindow(void* nativeWindow) override;
    void SetExternalMouseWheel(float delta) override;
    void Reset() override;
    void Poll(InputDeviceState& out) override;
    float ReadControl(
        const InputDeviceState& state,
        InputBindingSource source,
        std::string_view control,
        uint32_t gamepadIndex) const override;
    bool IsAnyControlActive(
        const InputDeviceState& state,
        float axisThreshold) const override;
    void EnumerateNewControls(
        const InputDeviceState& previous,
        const InputDeviceState& current,
        float axisThreshold,
        bool includeMouseMotion,
        std::vector<InputControlActuation>& outCandidates) const override;
    bool SetGamepadVibration(
        uint32_t gamepadIndex,
        float lowFrequency,
        float highFrequency) override;

private:
    void* hostWindow_ = nullptr;
    float externalWheel_ = 0.0f;
    bool hasPreviousMouse_ = false;
    long previousMouseX_ = 0;
    long previousMouseY_ = 0;
};

} // namespace HIKARI::INPUT
