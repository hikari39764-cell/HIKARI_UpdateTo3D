#include "HIKARI/HIKARI.h"

const char kWindowTitle[] = "HIKARI_Ver1.3";

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    HIKARI::SERVICES::BootstrapConfig servicesCfg{};
    if (!HIKARI::SERVICES::Initialize(kWindowTitle, servicesCfg)) {
        return -1;
    }

    HIKARI::HINPUT::SwitchLayer("Debug");

    HIKARI::LAB::ParticleLab particleLab;
    static bool isParticleLab = false;
    particleLab.Init();
    HIKARI::Transform2D T;
    T.position = { 640.0f,360.0f };
    T.pivotPx = { 50.0f,50.0f };

    while (HIKARI::SERVICES::PumpMessages()) {
        HIKARI::SERVICES::BeginFrame(servicesCfg);

        HIKARI::RENDERER::DrawBox(T, 100.0f, 100.0f);

        if (HIKARI::HINPUT::IsPressed("OpenParticleLab")) { isParticleLab = !isParticleLab; }
        if (isParticleLab) { particleLab.Update(kDt); particleLab.Draw(); }

        HIKARI::SERVICES::EndFrame();

        if (HIKARI::HINPUT::IsPressed("CloseProgram")) {
            break;
        }
    }

    HIKARI::SERVICES::FinalizeAll();
    return 0;
}
