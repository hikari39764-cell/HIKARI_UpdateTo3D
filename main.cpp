#include "HIKARI/HIKARI_Services.h"
#include "HIKARI/HIKARI_3D.h"

const char kWindowTitle[] = "HIKARI_Ver1.3";

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    HIKARI::SERVICES::BootstrapConfig servicesCfg{};
    if (!HIKARI::SERVICES::Initialize(kWindowTitle, servicesCfg)) {
        return -1;
    }

    HIKARI::HINPUT::SwitchLayer("Debug");

    HIKARI::MATH::RunMathConventionSelfCheck();

    HIKARI::Camera3D camera;
    camera.SetPerspective(60.0f * 3.1415926535f / 180.0f, static_cast<float>(kScreenW) / static_cast<float>(kScreenH), 0.1f, 100.0f);
    camera.SetLookAt({ 0.0f, 1.5f, -5.0f }, { 0.0f, 0.0f, 0.0f });

    HIKARI::RENDERER3D::WireCube cube{};
    cube.size = 2.0f;
    cube.rgba = 0x66CCFFFF;

    float angle = 0.0f;

    while (HIKARI::SERVICES::PumpMessages()) {
        HIKARI::SERVICES::BeginFrame(servicesCfg);

        angle += kDt;
        cube.transform.rotation = HIKARI::MATH::Quat::FromEulerXYZ(angle * 0.4f, angle, 0.0f);

        HIKARI::RENDERER3D::Reset();
        HIKARI::RENDERER3D::SubmitWireCube(cube);
        HIKARI::RENDERER3D::RenderAll(camera, static_cast<float>(kScreenW), static_cast<float>(kScreenH));

        HIKARI::RENDERER::SetCurrentLayer(HIKARI::RENDERER::RenderLayer::UI);
        HIKARI::Transform2D label{};
        label.position = { 16.0f, 16.0f };
        HIKARI::RENDERER::DrawBox(label, 220.0f, 44.0f, HIKARI::RENDERER::FillMode::Wireframe, HIKARI::RENDERER::CameraMode::Ignore, 0xFFFFFFFF);
        HIKARI::RENDERER::SetCurrentLayer(HIKARI::RENDERER::RenderLayer::Entity);

        HIKARI::SERVICES::EndFrame();

        if (HIKARI::HINPUT::IsPressed("CloseProgram")) {
            break;
        }
    }

    HIKARI::SERVICES::FinalizeAll();
    return 0;
}
