#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "Gameplay/Motion/HIKARI_MotionIntentService.h"
#include "Input/Platform/HIKARI_IInputBackend.h"
#include "Input/Runtime/HIKARI_InputService.h"

namespace {
    using namespace HIKARI;
    using namespace HIKARI::INPUT;

    class TestInputBackend final : public IInputBackend {
    public:
        void SetSpaceDown(bool down) noexcept { spaceDown_ = down; }

        void SetHostWindow(void*) override {}
        void SetMouseCaptureMode(MouseCaptureMode mode) override {
            captureMode_ = mode;
        }
        MouseCaptureMode GetMouseCaptureMode() const noexcept override {
            return captureMode_;
        }
        void SetExternalMouseWheel(float) override {}
        void Reset() override { spaceDown_ = false; }
        void Poll(InputDeviceState& out) override {
            out = {};
            out.keyboard[32u] = spaceDown_ ? 1u : 0u;
            if (spaceDown_) {
                out.lastActiveDevice = InputDeviceKind::KeyboardMouse;
            }
        }
        float ReadControl(
            const InputDeviceState& state,
            InputBindingSource source,
            std::string_view control,
            uint32_t) const override {
            return source == InputBindingSource::Keyboard &&
                    control == "Space"
                ? static_cast<float>(state.keyboard[32u])
                : 0.0f;
        }
        bool IsAnyControlActive(
            const InputDeviceState& state,
            float) const override {
            return state.keyboard[32u] != 0u;
        }
        void EnumerateNewControls(
            const InputDeviceState&,
            const InputDeviceState&,
            float,
            bool,
            std::vector<InputControlActuation>& outCandidates) const override {
            outCandidates.clear();
        }
        bool SetGamepadVibration(
            uint32_t,
            float,
            float) override {
            return true;
        }

    private:
        bool spaceDown_ = false;
        MouseCaptureMode captureMode_ = MouseCaptureMode::Free;
    };
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "repository root argument is required\n";
        return 1;
    }

    INPUT::InputService input{};
    auto backend = std::make_unique<TestInputBackend>();
    TestInputBackend* testBackend = backend.get();
    input.SetBackend(std::move(backend));
    if (!input.Initialize(std::filesystem::path(argv[1]))) {
        std::cerr << "input initialization failed: "
            << input.GetLastError() << '\n';
        return 1;
    }
    input.Contexts().SetActive("Editor", false);
    input.Contexts().SetActive("Gameplay", true);

    input.Update(1.0f / 60.0f);
    if (input.GetSnapshot().IsDown("Gameplay.Jump") ||
        input.GetSnapshot().IsPressed("Gameplay.Jump")) {
        std::cerr << "jump started active before Space was pressed\n";
        return 1;
    }

    testBackend->SetSpaceDown(true);
    input.Update(1.0f / 60.0f);
    const InputSnapshot& pressed = input.GetSnapshot();
    if (!pressed.IsDown("Gameplay.Jump") ||
        !pressed.IsPressed("Gameplay.Jump")) {
        std::cerr << "Space did not produce the Gameplay.Jump press edge\n";
        return 1;
    }

    GAMEPLAY::MotionIntentService intents{};
    constexpr RuntimeObjectHandle object{ 1u, 1u };
    GAMEPLAY::MotionIntent pressedIntent{};
    pressedIntent.jumpPressed = pressed.IsPressed("Gameplay.Jump");
    pressedIntent.jumpHeld = pressed.IsDown("Gameplay.Jump");
    intents.SubmitIntent(
        object,
        GAMEPLAY::kPlayerInputMotionSource,
        100,
        pressedIntent,
        2u);

    input.Update(1.0f / 60.0f);
    GAMEPLAY::MotionIntent heldIntent{};
    heldIntent.jumpPressed = input.GetSnapshot().IsPressed("Gameplay.Jump");
    heldIntent.jumpHeld = input.GetSnapshot().IsDown("Gameplay.Jump");
    intents.SubmitIntent(
        object,
        GAMEPLAY::kPlayerInputMotionSource,
        100,
        heldIntent,
        3u);

    GAMEPLAY::MotionIntent resolved{};
    if (!intents.ResolveIntent(object, 3u, resolved) ||
        !resolved.jumpPressed || !resolved.jumpHeld) {
        std::cerr << "jump press was not latched across the fixed-step gap\n";
        return 1;
    }

    std::cout << "Space -> Gameplay.Jump -> MotionIntent latch passed\n";
    return 0;
}
