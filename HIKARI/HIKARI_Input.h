#pragma once
#include <string>
#include <vector>
#include "HIKARI_Utility.h"
namespace HIKARI {
    namespace HINPUT {

        enum class BackendType {
            Novice,
            Win32
        };

        void PushLayer(const std::string& name);
        void PopLayer();
        void SetLayerActions(const std::string& layer, const std::vector<std::string>& actions);
        void ClearLayers();
        void SwitchLayer(const std::string& name);
        std::string GetCurrentLayer();
        void ToggleLayer(const std::string& name);

        // グローバルレイヤー（常時有効）
        void AddGlobalLayer(const std::string& name);
        void RemoveGlobalLayer(const std::string& name);
        void ClearGlobalLayers();
        std::vector<std::string> GetActiveLayers();
        bool IsLayerActive(const std::string& name);


        // —— 初期化 / フレーム更新 —— //
        void Init(const char* jsonPath = nullptr);
        void SetBackend(BackendType backend);
        BackendType GetBackend();
        void SetHostWindow(void* hwnd);
        void SetExternalMouseWheelDelta(float delta);
        void Update(float dt);

        // —— 照会 —— //
        float GetAxis(const std::string& action);
        bool  IsPressed(const std::string& action); // このフレームで押された
        bool  IsDown(const std::string& action);    // 押し続けている
        bool  IsReleased(const std::string& action);// このフレームで離された
        float HeldTime(const std::string& action);  // 押されていた累計時間
        bool  IsRepeated(const std::string& action);// 連打（初回を含む）

        // —— ダブルタップ / 入力バッファ —— //
        bool  IsDoubleTapped(const std::string& action, float window);
        void  OpenBuffer(const std::string& action, float seconds);
        bool  ConsumeBuffer(const std::string& action);
        bool  Buffered(const std::string& action);

        // —— バインド —— //
        enum class MouseButton { Left = 0, Right = 1, Middle = 2 };

        struct AxisBinding {
            enum class Source {
                Keyboard,
                MouseMoveX,
                MouseMoveY,
                MouseWheel,
            };

            Source source = Source::Keyboard;
            int    key = 0;
            float  value = 0.0f;
            MouseButton requireButton = MouseButton::Left;
            bool   needButton = false;

            static AxisBinding Keyboard(int keyCode, float v) {
                AxisBinding b; b.source = Source::Keyboard; b.key = keyCode; b.value = v; return b;
            }
            static AxisBinding Mouse(Source src, float v, MouseButton button = MouseButton::Left, bool require = false) {
                AxisBinding b; b.source = src; b.value = v; b.requireButton = button; b.needButton = require; return b;
            }
        };
        void  BindButton(const std::string& action, const std::vector<int>& keyCodes);
        void  BindAxis(const std::string& action, const std::vector<AxisBinding>& pairs, bool clamp01 = true);
        void  BindMouseButtons(const std::string& action, const std::vector<MouseButton>& buttons);

        // —— ゲームパッド —— //
        void  BindAxisPadLeft(const std::string& actionX, const std::string& actionY);
        void  SetPadDeadZone(float deadZone);
        void  SetActiveGamepad(int index);
        int   GetActiveGamepad();
        int   GetPadCount();
        void  BindPadButtons(const std::string& action, const std::vector<int>& padButtons); // A/B/X/Y/LB/RB/START/BACK/LS/RS
        Vector2 GetPadLeftStick();
        bool    HasPadLeftStickInput();

        // —— 振動関連 —— //
        void  SetPadVibration(float leftMotor, float rightMotor, float seconds = 0.0f);
        void  StopPadVibration();
        bool  IsPadVibrating();

        // —— 統一された移動方向（正規化済み、優先はゲームパッド）—— //
        Vector2 GetMoveVectorNormalized();

        // —— マウス —— //
        Vector2 GetMousePosition();
        Vector2 GetMouseDelta();
        float   GetMouseWheelDelta();
        bool    IsMouseDown(MouseButton btn);
        bool    IsMousePressed(MouseButton btn);
        bool    IsMouseReleased(MouseButton btn);

        // - いまの操作タイプ- //

        enum class LastInputDevice {
            None,
            Keyboard,
            Gamepad,
            Mouse
        };

        LastInputDevice GetLastInputDevice();
        bool IsUsingGamepad();
        bool IsUsingKeyboard();


    } // namespace HINPUT
} // namespace HIKARI
