#include <Novice.h>
#include "HIKARI/HIKARI.h"

const char kWindowTitle[] = "HIKARI_Ver1.3";

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

    // 下層サービスの起動（入力/レンダラー/相机等）
    HIKARI::SERVICES::BootstrapConfig servicesCfg{};
    HIKARI::SERVICES::Initialize(kWindowTitle, servicesCfg);

    // デフォルトのメイン入力レイヤー
    HIKARI::HINPUT::SwitchLayer("Debug");

    // パーティクルラボの初期化（デバッグ用途、サービスレイヤーの上に載せる）
    HIKARI::LAB::ParticleLab particleLab;
    static bool isParticleLab = false;
    particleLab.Init();
    HIKARI::Transform2D T;
    T.position = { 640.0f,360.0f };
    T.pivotPx = { 50.0f,50.0f };
    // ウィンドウの×ボタンが押されるまでループ
    while (Novice::ProcessMessage() == 0) {
        // フレームの開始（サービス層：入力更新/相机更新 等）
        HIKARI::SERVICES::BeginFrame(servicesCfg);

        ///
        /// ↓更新処理ここから（ここに Scene / GameObject / Systems を載せていく）
        ///

        ///
        /// ↑更新処理ここまで
        ///

        HIKARI::RENDERER::DrawBox(T, 100.0f, 100.0f);

        ///
        /// ↓描画処理ここから
        ///

        ///
        /// ↑描画処理ここまで
        ///


        ///
        /// ↓パーティクルラボここから（例：デバッグツールはサービス層の上に載る）
        ///

        if (HIKARI::HINPUT::IsPressed("OpenParticleLab")) { isParticleLab = !isParticleLab; }
        if (isParticleLab) { particleLab.Update(kDt); particleLab.Draw(); }

        ///
        /// ↑パーティクルラボここまで
        ///

        // フレームの終了（サービス層）
        HIKARI::SERVICES::EndFrame();

        // ESCキーが押されたらループを抜ける
        if (HIKARI::HINPUT::IsPressed("CloseProgram")) {
            break;
        }
    }

    // 下層サービスの終了処理
    HIKARI::SERVICES::FinalizeAll();
    return 0;
}
