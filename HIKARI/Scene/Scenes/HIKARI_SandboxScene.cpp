#include "HIKARI_SandboxScene.h"

#include <Windows.h>
#include <numbers>

#include "HIKARI_Services.h"

#include "Effekseer.h"
#include "EffekseerRendererDX12.h"

namespace HIKARI {

    namespace {

        Effekseer::ManagerRef gEfkManager;
        Effekseer::EffectRef gEfkEffect;
        Effekseer::Backend::GraphicsDeviceRef gEfkGraphicsDevice;
        EffekseerRenderer::RendererRef gEfkRenderer;
        Effekseer::RefPtr<EffekseerRenderer::SingleFrameMemoryPool> gEfkMemoryPool;
        Effekseer::RefPtr<EffekseerRenderer::CommandList> gEfkCommandList;

        bool gEfkInitialized = false;
        float gEfkTime = 0.0f;

        void ShutdownEffekseer()
        {
            if (!gEfkInitialized) {
                return;
            }

            if (gEfkManager != nullptr) {
                gEfkManager->StopAllEffects();
            }

            if (gEfkRenderer != nullptr) {
                gEfkRenderer->SetCommandList(nullptr);
            }

            gEfkEffect = nullptr;
            gEfkCommandList = nullptr;
            gEfkMemoryPool = nullptr;
            gEfkRenderer = nullptr;
            gEfkGraphicsDevice = nullptr;
            gEfkManager = nullptr;

            gEfkTime = 0.0f;
            gEfkInitialized = false;
        }

        bool InitializeEffekseer()
        {
            if (gEfkInitialized) {
                return true;
            }

            auto& ctx = HIKARI::SERVICES::gCtx;
            if (!ctx.device || !ctx.queue || !ctx.cmdList) {
                return false;
            }

            gEfkManager = Effekseer::Manager::Create(8000);
            if (gEfkManager == nullptr) {
                return false;
            }

            gEfkGraphicsDevice =
                EffekseerRendererDX12::CreateGraphicsDevice(
                    ctx.device,
                    ctx.queue,
                    3);

            if (gEfkGraphicsDevice == nullptr) {
                ShutdownEffekseer();
                return false;
            }

            DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

            gEfkRenderer =
                EffekseerRendererDX12::Create(
                    gEfkGraphicsDevice,
                    &colorFormat,
                    1,
                    DXGI_FORMAT_D32_FLOAT,
                    false,
                    8000);

            if (gEfkRenderer == nullptr) {
                ShutdownEffekseer();
                return false;
            }

            gEfkMemoryPool =
                EffekseerRenderer::CreateSingleFrameMemoryPool(
                    gEfkRenderer->GetGraphicsDevice());

            if (gEfkMemoryPool == nullptr) {
                ShutdownEffekseer();
                return false;
            }

            gEfkCommandList =
                EffekseerRenderer::CreateCommandList(
                    gEfkRenderer->GetGraphicsDevice(),
                    gEfkMemoryPool);

            if (gEfkCommandList == nullptr) {
                ShutdownEffekseer();
                return false;
            }

            gEfkManager->SetSpriteRenderer(gEfkRenderer->CreateSpriteRenderer());
            gEfkManager->SetRibbonRenderer(gEfkRenderer->CreateRibbonRenderer());
            gEfkManager->SetRingRenderer(gEfkRenderer->CreateRingRenderer());
            gEfkManager->SetTrackRenderer(gEfkRenderer->CreateTrackRenderer());
            gEfkManager->SetModelRenderer(gEfkRenderer->CreateModelRenderer());

            gEfkManager->SetTextureLoader(gEfkRenderer->CreateTextureLoader());
            gEfkManager->SetModelLoader(gEfkRenderer->CreateModelLoader());
            gEfkManager->SetMaterialLoader(gEfkRenderer->CreateMaterialLoader());
            gEfkManager->SetCurveLoader(Effekseer::MakeRefPtr<Effekseer::CurveLoader>());

            gEfkManager->SetCoordinateSystem(Effekseer::CoordinateSystem::RH);

            gEfkEffect = Effekseer::Effect::Create(
                gEfkManager,
                u"Data/vfx/Laser01.efkefc");

            if (gEfkEffect == nullptr) {
                ShutdownEffekseer();
                return false;
            }

            gEfkInitialized = true;
            gEfkManager->Play(gEfkEffect, 0.0f, 0.0f, 0.0f);
            return true;
        }

        void UpdateEffekseer(float dt)
        {
            if (!gEfkInitialized) {
                return;
            }

            gEfkTime += dt;

            Effekseer::Manager::UpdateParameter updateParameter;
            gEfkManager->Update(updateParameter);
        }

        void DrawEffekseer(const HIKARI::Camera3D& camera)
        {
            if (!gEfkInitialized) {
                return;
            }

            auto& ctx = HIKARI::SERVICES::gCtx;

            gEfkMemoryPool->NewFrame();

            EffekseerRendererDX12::BeginCommandList(gEfkCommandList, ctx.cmdList);
            gEfkRenderer->SetCommandList(gEfkCommandList);

            Effekseer::Matrix44 efkProj{};
            Effekseer::Matrix44 efkView{};

            const auto& proj = camera.GetProj();
            const auto& view = camera.GetView();

            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    efkProj.Values[r][c] = proj.m[r][c];
                    efkView.Values[r][c] = view.m[r][c];
                }
            }

            Effekseer::Manager::LayerParameter layerParameter;
            auto camPos = camera.GetPosition();
            layerParameter.ViewerPosition = Effekseer::Vector3D(camPos.x, camPos.y, camPos.z);
            gEfkManager->SetLayerParameter(0, layerParameter);

            gEfkRenderer->SetTime(gEfkTime);
            gEfkRenderer->SetProjectionMatrix(efkProj);
            gEfkRenderer->SetCameraMatrix(efkView);

            gEfkRenderer->BeginRendering();

            Effekseer::Manager::DrawParameter drawParameter;
            drawParameter.ZNear = 0.1f;
            drawParameter.ZFar = 100.0f;
            drawParameter.ViewProjectionMatrix = gEfkRenderer->GetCameraProjectionMatrix();
            gEfkManager->Draw(drawParameter);

            gEfkRenderer->EndRendering();

            gEfkRenderer->SetCommandList(nullptr);
            EffekseerRendererDX12::EndCommandList(gEfkCommandList);
        }

    } // namespace

    SandboxScene::SandboxScene(SceneCatalog& sceneCatalog, std::string sceneId)
        : DocumentSceneBase(sceneCatalog, std::move(sceneId)) {
    }

    SandboxScene::~SandboxScene()
    {
        ShutdownEffekseer();
    }

    void SandboxScene::OnEnter()
    {
        DocumentSceneBase::OnEnter();
        InitializeEffekseer();
    }

    void SandboxScene::Update(float dt)
    {
        DocumentSceneBase::Update(dt);

        if (!gEfkInitialized) {
            return;
        }

        if ((GetAsyncKeyState(VK_SPACE) & 0x0001) != 0) {
            gEfkManager->Play(gEfkEffect, 0.0f, 0.0f, 0.0f);
        }

        UpdateEffekseer(dt);
    }

    void SandboxScene::Render()
    {
        DocumentSceneBase::Render();

        if (!gEfkInitialized) {
            return;
        }

        DrawEffekseer(GetCamera());
    }

} // namespace HIKARI