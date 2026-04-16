#include "HIKARI/HIKARI_Services.h"
#include "HIKARI/App/HIKARI_EngineApp.h"
#include <Windows.h>

const char kWindowTitle[] = "HIKARI_Ver1.3";

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    HIKARI::SERVICES::BootstrapConfig servicesCfg{};
    if (!HIKARI::SERVICES::Initialize(kWindowTitle, servicesCfg)) {
        return -1;
    }

    HIKARI::HINPUT::SwitchLayer("Debug");
    HIKARI::MATH::RunMathConventionSelfCheck();
    HIKARI::EngineApp app;
    if (!app.Initialize()) {
        HIKARI::SERVICES::FinalizeAll();
        return -1;
    }

    while (HIKARI::SERVICES::PumpMessages()) {
        HIKARI::SERVICES::BeginFrame(servicesCfg);

        app.Update(kDt);
        app.Render();
        app.RenderImGui();

        if ((GetAsyncKeyState(VK_F1) & 0x0001) != 0) {
            HIKARI::SERVICES::SetEditorUIEnabled(!HIKARI::SERVICES::IsEditorUIEnabled());
        }

        HIKARI::SERVICES::EndFrame();

        if (HIKARI::HINPUT::IsPressed("CloseProgram")) {
            break;
        }
    }

    app.Shutdown();
    HIKARI::SERVICES::FinalizeAll();
    return 0;
}
