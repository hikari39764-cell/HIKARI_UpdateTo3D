#include "HIKARI/HIKARI_Services.h"
#include "HIKARI/App/HIKARI_EngineApp.h"
#include "HIKARI/Core/HIKARI_TimeService.h"

const char kWindowTitle[] = "HIKARI_Ver1.3";

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	HIKARI::SERVICES::BootstrapConfig servicesCfg{};
	if (!HIKARI::SERVICES::Initialize(kWindowTitle, servicesCfg)) {
		return -1;
	}

	HIKARI::HINPUT::SwitchLayer("Debug");
	HIKARI::MATH::RunMathConventionSelfCheck();
	HIKARI::SpineActor op;
	op.Load("./Assets/Spine/op.atlas", "./Assets/Spine/op.json");
	op.transform.position = { 640.0f, 360.0f };
	op.SetAnimation("op", false);

	HIKARI::EngineApp app;
	if (!app.Initialize()) {
		HIKARI::SERVICES::FinalizeAll();
		return -1;
	}

	while (HIKARI::SERVICES::PumpMessages()) {
		HIKARI::SERVICES::BeginFrame(servicesCfg);
		const HIKARI::FrameContext& frame = HIKARI::TIME::GetFrameContext();

		if (!op.IsAnimationFinished("op"))
		{
			op.Update(frame.gameDt);
			op.Draw();
		} else {
			app.Update(frame.gameDt);
			app.Render();
			app.RenderImGui();
		}


		if (HIKARI::HINPUT::IsPressed("ToggleEditorUI")) {
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
