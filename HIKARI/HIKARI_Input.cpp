#include "HIKARI_Input.h"
#include <cstring>
#include <unordered_map>
#include <cmath>
#include <algorithm>

// 未定義
#include <Windows.h>
#undef min
#undef max

// XInput
#include <Xinput.h>
#pragma comment(lib, "xinput9_1_0.lib")

// nlohmann/json
#include <json.hpp>
#include <fstream>
#include <sstream>
#include <dinput.h>
using nlohmann::json;

namespace HIKARI {
	namespace HINPUT {

		// ===== 内部 =====
		enum class ActionType { Button, Axis };

		static LastInputDevice gLastInputDevice = LastInputDevice::None;

		struct ActionConfig {
			ActionType type = ActionType::Button;
			std::vector<int> buttonKeys;       // キーボード: DIK_*
			std::vector<AxisBinding> axisKeys; // キーボード軸: (key, value)
			std::vector<int> padButtons;       // XInput ボタンFLAG集合
			std::vector<MouseButton> mouseButtons; // マウスボタン
			bool  enableRepeat = false;
			float repeatDelay = 0.25f;
			float repeatRate = 10.0f; // 回/秒
			bool  clampAxis01 = true;  // 軸を -1〜1 にクランプするか（マウス移動では false 推奨）
		};

		struct ActionState {
			bool  pressed = false;
			bool  down = false;
			bool  released = false;
			float heldTime = 0.0f;

			int   repeatCount = 0;
			float repeatTimer = 0.0f;

			float lastPressTime = -1e9f;    // 直近の押下時刻
			float prevPressTime = -1e9f;    // その前の押下時刻
			float lastReleaseTime = -1e9f;  // 直近の離した時刻

			float bufferRemain = 0.0f;
		};

		struct LayerStack {
			std::vector<std::string> stack;   // 主レイヤーのスタック
			std::vector<std::string> globals; // 常時有効
			std::unordered_map<std::string, std::vector<std::string>> allowed;

			void Push(const std::string& n) { stack.push_back(n); }
			void Pop() { if (!stack.empty()) stack.pop_back(); }
			void Clear() { stack.clear(); globals.clear(); }
			void AddGlobal(const std::string& n) {
				for (auto& g : globals) { if (g == n) return; }
				globals.push_back(n);
			}
			void RemoveGlobal(const std::string& n) {
				globals.erase(std::remove(globals.begin(), globals.end(), n), globals.end());
			}
			std::vector<std::string> Active() const {
				std::vector<std::string> r;
				if (!stack.empty()) r.push_back(stack.back());
				r.insert(r.end(), globals.begin(), globals.end());
				return r;
			}
			bool IsLayerActive(const std::string& name) const {
				if (!stack.empty() && stack.back() == name) return true;
				for (auto& g : globals) if (g == name) return true;
				return false;
			}
			bool IsAllowed(const std::string& act) const {
				auto active = Active();
				if (active.empty()) return true;
				for (auto& layer : active) {
					auto it = allowed.find(layer);
					if (it == allowed.end()) return true; // 未登録 = 全許可
					const auto& list = it->second;
					if (std::find(list.begin(), list.end(), act) != list.end()) return true;
				}
				return false;
			}
		} gLayers;

		// ===== グローバル状態 =====
		static std::unordered_map<std::string, ActionConfig> gConfigs;
		static std::unordered_map<std::string, ActionState>  gStates;
		static std::unordered_map<std::string, float>        gAxisCache;

		// サンプリングテーブル
		static std::unordered_map<std::string, bool> gKbDownSample;
		static std::unordered_map<std::string, bool> gPadDownSample;
		static std::unordered_map<std::string, bool> gMouseDownSample;

		static unsigned char gKeys[256] = { 0 };
		static unsigned char gPrev[256] = { 0 };
		static float gTime = 0.0f;
		static BackendType gBackend = BackendType::Win32;
		static HWND gHostWindow = nullptr;
		static float gExternalWheelDelta = 0.0f;

		// ===== マウス =====
		static Vector2 gMousePos{ 0.0f, 0.0f };
		static Vector2 gMousePrev{ 0.0f, 0.0f };
		static Vector2 gMouseDelta{ 0.0f, 0.0f };
		static float   gWheelDelta = 0.0f;
		static bool    gMouseNow[3] = { false,false,false };
		static bool    gMousePrevBtns[3] = { false,false,false };

		// ===== キー名 → DIK =====
		static bool StrEq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }
		static int  KeyCodeFromString(const char* name) {
			if (!name || !name[0]) return 0;
			if (name[1] == '\0') {
				unsigned char c = static_cast<unsigned char>(name[0]);
				if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
					static const int mapAlpha[26] = {
						DIK_A,DIK_B,DIK_C,DIK_D,DIK_E,DIK_F,DIK_G,DIK_H,DIK_I,DIK_J,
						DIK_K,DIK_L,DIK_M,DIK_N,DIK_O,DIK_P,DIK_Q,DIK_R,DIK_S,DIK_T,
						DIK_U,DIK_V,DIK_W,DIK_X,DIK_Y,DIK_Z
					};
					return mapAlpha[(c >= 'a') ? (c - 'a') : (c - 'A')];
				}
				if (c >= '0' && c <= '9') {
					static const int mapNum[10] = { DIK_0,DIK_1,DIK_2,DIK_3,DIK_4,DIK_5,DIK_6,DIK_7,DIK_8,DIK_9 };
					return mapNum[c - '0'];
				}
				if (c == '-') return DIK_MINUS;
				if (c == '=') return DIK_EQUALS;
				if (c == '[') return DIK_LBRACKET;
				if (c == ']') return DIK_RBRACKET;
				if (c == ';') return DIK_SEMICOLON;
				if (c == '\'') return DIK_APOSTROPHE;
				if (c == ',') return DIK_COMMA;
				if (c == '.') return DIK_PERIOD;
				if (c == '/') return DIK_SLASH;
				if (c == '\\') return DIK_BACKSLASH;
			}
			if (StrEq(name, "Space")) return DIK_SPACE;
			if (StrEq(name, "Enter")) return DIK_RETURN;
			if (StrEq(name, "Esc") || StrEq(name, "Escape")) return DIK_ESCAPE;
			if (StrEq(name, "Tab")) return DIK_TAB;
			if (StrEq(name, "Backspace")) return DIK_BACK;
			if (StrEq(name, "Left")) return DIK_LEFT;
			if (StrEq(name, "Right")) return DIK_RIGHT;
			if (StrEq(name, "Up")) return DIK_UP;
			if (StrEq(name, "Down")) return DIK_DOWN;
			if (StrEq(name, "Shift")) return DIK_LSHIFT;
			if (StrEq(name, "Ctrl")) return DIK_LCONTROL;
			if (StrEq(name, "Alt")) return DIK_LMENU;
			if (StrEq(name, "LeftShift")) return DIK_LSHIFT;
			if (StrEq(name, "RightShift")) return DIK_RSHIFT;
			if (StrEq(name, "LeftCtrl")) return DIK_LCONTROL;
			if (StrEq(name, "RightCtrl")) return DIK_RCONTROL;
			if (StrEq(name, "LeftAlt")) return DIK_LMENU;
			if (StrEq(name, "RightAlt")) return DIK_RMENU;
			if (StrEq(name, "Minus")) return DIK_MINUS;
			if (StrEq(name, "Equals")) return DIK_EQUALS;
			if (StrEq(name, "LBracket")) return DIK_LBRACKET;
			if (StrEq(name, "RBracket")) return DIK_RBRACKET;
			if (StrEq(name, "Semicolon")) return DIK_SEMICOLON;
			if (StrEq(name, "Quote")) return DIK_APOSTROPHE;
			if (StrEq(name, "Comma")) return DIK_COMMA;
			if (StrEq(name, "Period")) return DIK_PERIOD;
			if (StrEq(name, "Slash")) return DIK_SLASH;
			if (StrEq(name, "Backslash")) return DIK_BACKSLASH;
			if (StrEq(name, "NumPad0")) return DIK_NUMPAD0;
			if (StrEq(name, "NumPad1")) return DIK_NUMPAD1;
			if (StrEq(name, "NumPad2")) return DIK_NUMPAD2;
			if (StrEq(name, "NumPad3")) return DIK_NUMPAD3;
			if (StrEq(name, "NumPad4")) return DIK_NUMPAD4;
			if (StrEq(name, "NumPad5")) return DIK_NUMPAD5;
			if (StrEq(name, "NumPad6")) return DIK_NUMPAD6;
			if (StrEq(name, "NumPad7")) return DIK_NUMPAD7;
			if (StrEq(name, "NumPad8")) return DIK_NUMPAD8;
			if (StrEq(name, "NumPad9")) return DIK_NUMPAD9;
			if (StrEq(name, "NumPadAdd")) return DIK_ADD;
			if (StrEq(name, "NumPadSub")) return DIK_SUBTRACT;
			if (StrEq(name, "NumPadMul")) return DIK_MULTIPLY;
			if (StrEq(name, "NumPadDiv")) return DIK_DIVIDE;
			if (StrEq(name, "NumPadEnter")) return DIK_NUMPADENTER;
			if (StrEq(name, "F1")) return DIK_F1;
			if (StrEq(name, "F2")) return DIK_F2;
			if (StrEq(name, "F3")) return DIK_F3;
			if (StrEq(name, "F4")) return DIK_F4;
			if (StrEq(name, "F5")) return DIK_F5;
			if (StrEq(name, "F6")) return DIK_F6;
			if (StrEq(name, "F7")) return DIK_F7;
			if (StrEq(name, "F8")) return DIK_F8;
			if (StrEq(name, "F9")) return DIK_F9;
			if (StrEq(name, "F10")) return DIK_F10;
			if (StrEq(name, "F11")) return DIK_F11;
			if (StrEq(name, "F12")) return DIK_F12;
			return 0;
		}


		// ===== ゲームパッドボタン FLAG =====
		enum PadButtonFlag {
			PAD_A = XINPUT_GAMEPAD_A,
			PAD_B = XINPUT_GAMEPAD_B,
			PAD_X = XINPUT_GAMEPAD_X,
			PAD_Y = XINPUT_GAMEPAD_Y,
			PAD_LB = XINPUT_GAMEPAD_LEFT_SHOULDER,
			PAD_RB = XINPUT_GAMEPAD_RIGHT_SHOULDER,
			PAD_BACK = XINPUT_GAMEPAD_BACK,
			PAD_START = XINPUT_GAMEPAD_START,
			PAD_LS = XINPUT_GAMEPAD_LEFT_THUMB,
			PAD_RS = XINPUT_GAMEPAD_RIGHT_THUMB,
			PAD_LT = 0x00010000,
			PAD_RT = 0x00020000
		};

		// ===== ゲームパッド実行時 =====
		static int   gActivePad = -1;        // -1 は自動
		static int   gPadCount = 0;
		static float gPadDeadZone = 0.15f;   // 半径デッドゾーン
		static float gPadLX = 0.0f, gPadLY = 0.0f; // 正規化 [-1,1]
		static std::string gPadAxisX = "MoveX";
		static std::string gPadAxisY = "MoveY";

		static float gPadLT = 0.0f;
		static float gPadRT = 0.0f;
		static std::string gPadAxisLT;
		static std::string gPadAxisRT;

		// === 振動関連 ===
		static int   gLastUsedPadIndex = 0;  // 最後に使用したパッド
		static int   gVibIndex = -1;         // 振動をかけているインデックス
		static float gVibRemain = 0.0f;      // 残り時間（秒）
		static bool  gVibActive = false;

		// ===== ユーティリティ =====
		static bool  KeyNow(int k) { return gKeys[k] != 0; }
		static int DikToVirtualKey(int dik) {
			if (dik < 0 || dik > 0xFF) return 0;
			const UINT vk = ::MapVirtualKeyA(static_cast<UINT>(dik), MAPVK_VSC_TO_VK);
			return static_cast<int>(vk);
		}

		static void PollKeyboardWin32(unsigned char* keys) {
			for (int i = 0; i < 256; ++i) {
				const int vk = DikToVirtualKey(i);
				keys[i] = (vk != 0 && (::GetAsyncKeyState(vk) & 0x8000) != 0) ? 0x80 : 0x00;
			}
		}

		static void PollMouseWin32() {
			gMousePrev = gMousePos;

			POINT pt{};
			if (::GetCursorPos(&pt)) {
				if (gHostWindow) {
					::ScreenToClient(gHostWindow, &pt);
				}
				gMousePos = { static_cast<float>(pt.x), static_cast<float>(pt.y) };
			}

			gMouseDelta = { gMousePos.x - gMousePrev.x, gMousePos.y - gMousePrev.y };
			gWheelDelta = gExternalWheelDelta;
			gExternalWheelDelta = 0.0f;

			gMousePrevBtns[0] = gMouseNow[0];
			gMousePrevBtns[1] = gMouseNow[1];
			gMousePrevBtns[2] = gMouseNow[2];
			gMouseNow[0] = (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
			gMouseNow[1] = (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
			gMouseNow[2] = (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
		}

		static bool  MouseBtnNow(MouseButton btn) {
			int idx = static_cast<int>(btn);
			if (idx < 0 || idx >= 3) return false;
			return gMouseNow[idx];
		}

		// ===== デフォルトバインド =====
		static void BindButtonsIL(const char* name, std::initializer_list<int> ks) {
			std::vector<int> v; v.reserve(ks.size()); for (auto k : ks) v.push_back(k);
			BindButton(name, v);
		}
		static void BindAxisIL(const char* name, std::initializer_list<AxisBinding> list) {
			std::vector<AxisBinding> v; v.reserve(list.size()); for (auto ab : list) v.push_back(ab);
			BindAxis(name, v);
		}
		static void LoadDefaults() {
			BindAxisIL("MoveX", {
				AxisBinding::Keyboard(KeyCodeFromString("A"),   -1.0f),
				AxisBinding::Keyboard(KeyCodeFromString("Left"),-1.0f),
				AxisBinding::Keyboard(KeyCodeFromString("D"),   +1.0f),
				AxisBinding::Keyboard(KeyCodeFromString("Right"),+1.0f),
				});
			BindAxisIL("MoveY", {
				AxisBinding::Keyboard(KeyCodeFromString("W"),   +1.0f), // 上方向を +
				AxisBinding::Keyboard(KeyCodeFromString("Up"),  +1.0f),
				AxisBinding::Keyboard(KeyCodeFromString("S"),   -1.0f),
				AxisBinding::Keyboard(KeyCodeFromString("Down"),-1.0f),
				});


			BindButtonsIL("Jump", { KeyCodeFromString("Space") });
			BindPadButtons("Jump", { PAD_A });

			// ----  ESC で終了用 ----
			BindButtonsIL("CloseProgram", { KeyCodeFromString("Esc") });

			BindButtonsIL("ToggleEditorUI", { KeyCodeFromString("F1") });
			BindButtonsIL("PlayTestVfx", { KeyCodeFromString("Space") });
			BindButtonsIL("PlaySceneScan", { KeyCodeFromString("V") });

			// ---- Debug Camera ----
			BindMouseButtons("CameraDrag", { MouseButton::Middle });
			BindMouseButtons("CameraLook", { MouseButton::Right });
			BindAxis("CameraDragX", { AxisBinding::Mouse(AxisBinding::Source::MouseMoveX, -1.0f, MouseButton::Middle, true) }, false);
			BindAxis("CameraDragY", { AxisBinding::Mouse(AxisBinding::Source::MouseMoveY, -1.0f, MouseButton::Middle, true) }, false);
			BindAxis("CameraZoom", { AxisBinding::Mouse(AxisBinding::Source::MouseWheel, 0.1f) }, false);

			// 状態初期化
			for (auto& kv : gConfigs) {
				gStates[kv.first] = ActionState{};
			}

			// Gameplay レイヤーで使えるアクションに追加
			SetLayerActions("Debug", {
				"MoveX","MoveY","Jump",
				"CloseProgram",
				"ToggleEditorUI",
				"PlayTestVfx",
				"PlaySceneScan"
				});
			SetLayerActions("Gameplay", {
				"MoveX","MoveY","Jump",
				"CloseProgram",
				"ToggleEditorUI",
				"PlayTestVfx",
				"PlaySceneScan"
				});
			SetLayerActions("DebugCamera", {
			 "CameraDrag",
			 "CameraLook",
			 "CameraDragX",
			 "CameraDragY",
			 "CameraZoom"
				});
			AddGlobalLayer("DebugCamera");
		}


		// ===== JSON 名称 → ゲームパッドボタン =====
		static int PadNameToFlag(const std::string& sIn) {
			std::string s = sIn;
			for (auto& c : s) c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
			if (s == "A")     return PAD_A;
			if (s == "B")     return PAD_B;
			if (s == "X")     return PAD_X;
			if (s == "Y")     return PAD_Y;
			if (s == "LB")    return PAD_LB;
			if (s == "RB")    return PAD_RB;
			if (s == "BACK")  return PAD_BACK;
			if (s == "START") return PAD_START;
			if (s == "LS")    return PAD_LS;
			if (s == "RS")    return PAD_RS;
			if (s == "LT" || s == "LTRIGGER" || s == "LEFTTRIGGER" || s == "L2") return PAD_LT;
			if (s == "RT" || s == "RTRIGGER" || s == "RIGHTTRIGGER" || s == "R2") return PAD_RT;
			return 0;
		}

		static bool MouseButtonFromString(const std::string& name, MouseButton& out) {
			std::string s = name;
			for (auto& c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
			if (s == "left" || s == "l" || s == "0") { out = MouseButton::Left; return true; }
			if (s == "right" || s == "r" || s == "1") { out = MouseButton::Right; return true; }
			if (s == "middle" || s == "m" || s == "2" || s == "wheel") { out = MouseButton::Middle; return true; }
			return false;
		}

		// ===== JSON から設定を読み込む =====
		static void TryLoadJson(const char* path) {
			if (!path) return;

			std::ifstream ifs(path, std::ios::in | std::ios::binary);
			if (!ifs) return;
			std::stringstream ss; ss << ifs.rdbuf();
			json j = json::parse(ss.str(), nullptr, false);
			if (j.is_discarded()) return;

			// レイヤー
			if (j.contains("layers") && j["layers"].is_object()) {
				for (auto& [layerName, arr] : j["layers"].items()) {
					if (!arr.is_array()) continue;
					std::vector<std::string> acts;
					for (auto& it : arr) if (it.is_string()) acts.push_back(it.get<std::string>());
					SetLayerActions(layerName, acts);
				}
			} else {
				SetLayerActions("Gameplay", { "MoveX","MoveY","Jump" });
			}

			if (j.contains("globalLayers") && j["globalLayers"].is_array()) {
				for (auto& l : j["globalLayers"]) {
					if (l.is_string()) AddGlobalLayer(l.get<std::string>());
				}
			}

			// パッド
			if (j.contains("pad") && j["pad"].is_object()) {
				auto& p = j["pad"];
				if (p.contains("deadZone") && p["deadZone"].is_number()) {
					SetPadDeadZone(static_cast<float>(p["deadZone"].get<double>()));
				}
				if (p.contains("activeIndex") && p["activeIndex"].is_number_integer()) {
					SetActiveGamepad(p["activeIndex"].get<int>());
				}
			}

			// ボタン
			if (j.contains("buttons") && j["buttons"].is_object()) {
				for (auto& [action, obj] : j["buttons"].items()) {
					if (!obj.is_object()) continue;

					// キーボード
					std::vector<int> keyCodes;
					if (obj.contains("keyboard") && obj["keyboard"].is_array()) {
						for (auto& k : obj["keyboard"]) {
							if (!k.is_string()) continue;
							int dik = KeyCodeFromString(k.get<std::string>().c_str());
							if (dik != 0) keyCodes.push_back(dik);
						}
					}
					if (!keyCodes.empty()) BindButton(action, keyCodes);

					std::vector<MouseButton> mouseButtons;
					if (obj.contains("mouse") && obj["mouse"].is_array()) {
						for (auto& mb : obj["mouse"]) {
							if (!mb.is_string()) continue;
							MouseButton btn{};
							if (MouseButtonFromString(mb.get<std::string>(), btn)) {
								mouseButtons.push_back(btn);
							}
						}
					}
					if (!mouseButtons.empty()) BindMouseButtons(action, mouseButtons);

					// ゲームパッド
					std::vector<int> padFlags;
					if (obj.contains("pad") && obj["pad"].is_array()) {
						for (auto& pbtn : obj["pad"]) {
							if (!pbtn.is_string()) continue;
							int flag = PadNameToFlag(pbtn.get<std::string>());
							if (flag != 0) padFlags.push_back(flag);
						}
					}
					if (!padFlags.empty()) BindPadButtons(action, padFlags);
				}
			}

			// 軸
			if (j.contains("axes") && j["axes"].is_object()) {
				for (auto& [action, obj] : j["axes"].items()) {
					if (!obj.is_object()) continue;

					// キーボード軸 / マウス軸
					std::vector<AxisBinding> pairs;
					bool clampAxis = true;
					if (obj.contains("keyboard") && obj["keyboard"].is_array()) {
						for (auto& kv : obj["keyboard"]) {
							if (!kv.is_array() || kv.size() != 2) continue;
							std::string keyName = kv[0].is_string() ? kv[0].get<std::string>() : "";
							double val = kv[1].is_number() ? kv[1].get<double>() : 0.0;
							int dik = KeyCodeFromString(keyName.c_str());
							if (dik != 0) pairs.push_back(AxisBinding::Keyboard(dik, static_cast<float>(val)));
						}
					}
					if (obj.contains("mouse") && obj["mouse"].is_array()) {
						for (auto& kv : obj["mouse"]) {
							if (!kv.is_array() || kv.size() < 2) continue;
							std::string kind = kv[0].is_string() ? kv[0].get<std::string>() : "";
							double val = kv[1].is_number() ? kv[1].get<double>() : 0.0;
							MouseButton btn = MouseButton::Left;
							bool require = false;
							if (kv.size() >= 3 && kv[2].is_string()) {
								if (MouseButtonFromString(kv[2].get<std::string>(), btn)) {
									require = true;
								}
							}

							AxisBinding::Source src = AxisBinding::Source::MouseMoveX;
							if (kind == "MoveX" || kind == "MouseX") src = AxisBinding::Source::MouseMoveX;
							else if (kind == "MoveY" || kind == "MouseY") src = AxisBinding::Source::MouseMoveY;
							else if (kind == "Wheel" || kind == "Scroll") src = AxisBinding::Source::MouseWheel;
							else continue;

							pairs.push_back(AxisBinding::Mouse(src, static_cast<float>(val), btn, require));
							clampAxis = false; // マウス移動は非クランプ前提
						}
					}
					if (obj.contains("clamp") && obj["clamp"].is_boolean()) {
						clampAxis = obj["clamp"].get<bool>();
					}
					if (!pairs.empty()) BindAxis(action, pairs, clampAxis);

					// パッド軸マッピング
					if (obj.contains("pad") && obj["pad"].is_string()) {
						std::string which = obj["pad"].get<std::string>();
						if (which == "LeftX")      gPadAxisX = action;
						else if (which == "LeftY") gPadAxisY = action;
					}
				}
			}
		}

		// ===== レイヤー =====
		void PushLayer(const std::string& name) { gLayers.Push(name); }
		void PopLayer() { gLayers.Pop(); }
		void SetLayerActions(const std::string& layer, const std::vector<std::string>& actions) { gLayers.allowed[layer] = actions; }
		// すべてのレイヤーをクリア（＝全アクション許可状態）
		void ClearLayers() {
			gLayers.Clear();
		}

		// 1つのレイヤーに「切り替え」：スタックを全部消してから指定レイヤーを積む
		void SwitchLayer(const std::string& name) {
			gLayers.stack.clear();
			gLayers.stack.push_back(name);
		}

		// 今一番上に乗っているレイヤー名を取得（なければ空文字）
		std::string GetCurrentLayer() {
			if (gLayers.stack.empty()) {
				return "";
			}
			return gLayers.stack.back();
		}
		// 同じ名前なら外す / 違うなら積む というトグル用
		void ToggleLayer(const std::string& name) {
			if (!gLayers.stack.empty() && gLayers.stack.back() == name) {
				gLayers.stack.pop_back();
			} else {
				gLayers.stack.push_back(name);
			}
		}
		void AddGlobalLayer(const std::string& name) { gLayers.AddGlobal(name); }
		void RemoveGlobalLayer(const std::string& name) { gLayers.RemoveGlobal(name); }
		void ClearGlobalLayers() { gLayers.globals.clear(); }
		std::vector<std::string> GetActiveLayers() { return gLayers.Active(); }
		bool IsLayerActive(const std::string& name) { return gLayers.IsLayerActive(name); }


		// ===== 初期化 / 更新 =====
		void Init(const char* jsonPath) {
			std::memset(gKeys, 0, sizeof(gKeys));
			std::memset(gPrev, 0, sizeof(gPrev));
			gConfigs.clear(); gStates.clear(); gAxisCache.clear();
			gKbDownSample.clear(); gPadDownSample.clear(); gMouseDownSample.clear();
			gLayers.Clear(); gLayers.allowed.clear();
			gTime = 0.0f;

			gMousePos = { 0.0f,0.0f }; gMousePrev = gMousePos; gMouseDelta = { 0.0f,0.0f }; gWheelDelta = 0.0f;
			for (int i = 0; i < 3; ++i) { gMouseNow[i] = false; gMousePrevBtns[i] = false; }

			// 既定では左スティックを MoveX / MoveY に割り当て
			gPadAxisX = "MoveX";
			gPadAxisY = "MoveY";
			gPadDeadZone = 0.15f;
			gActivePad = -1;

			gPadRT = 0.0f;
			gPadLT = 0.0f;
			gPadAxisLT.clear();
			gPadAxisRT.clear();

			gLastInputDevice = LastInputDevice::None;
			gLastUsedPadIndex = 0;
			gVibIndex = -1;
			gVibRemain = 0.0f;
			gVibActive = false;
			gBackend = BackendType::Win32;

			LoadDefaults();
			if (jsonPath) {
				TryLoadJson(jsonPath);
			}
		}

		void SetBackend(BackendType backend) {
			gBackend = backend;
		}

		BackendType GetBackend() {
			return gBackend;
		}

		void SetHostWindow(void* hwnd) {
			gHostWindow = reinterpret_cast<HWND>(hwnd);
		}

		void SetExternalMouseWheelDelta(float delta) {
			gExternalWheelDelta += delta;
		}

		// XInput の状態をポーリング
		static void PollXInput() {
			gPadCount = 0;
			gPadLX = gPadLY = 0.0f;

			// 接続数のカウント
			for (DWORD i = 0; i < 4; ++i) {
				XINPUT_STATE state{};
				if (XInputGetState(i, &state) == ERROR_SUCCESS) ++gPadCount;
			}
			if (gPadCount == 0) return;

			int useIndex = 0;
			if (gActivePad >= 0) {
				useIndex = std::min(std::max(gActivePad, 0), 3);
			} else {
				// 自動選択：最初に入力があるもの、なければ 0
				bool found = false;
				for (DWORD i = 0; i < 4; ++i) {
					XINPUT_STATE s{};
					if (XInputGetState(i, &s) == ERROR_SUCCESS) {
						if (s.Gamepad.wButtons != 0 ||
							std::abs(s.Gamepad.sThumbLX) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
							std::abs(s.Gamepad.sThumbLY) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
							s.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
							s.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
							useIndex = static_cast<int>(i);
							gLastInputDevice = LastInputDevice::Gamepad;
							found = true;
							break;
						}
					}
				}
				if (!found) useIndex = 0;
			}

			XINPUT_STATE st{};
			if (XInputGetState(static_cast<DWORD>(useIndex), &st) == ERROR_SUCCESS) {

				// このパッドを「最後に使ったパッド」として覚える
				gLastUsedPadIndex = useIndex;

				if (st.Gamepad.wButtons != 0 ||
					std::abs(st.Gamepad.sThumbLX) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
					std::abs(st.Gamepad.sThumbLY) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
					st.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
					st.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
					gLastInputDevice = LastInputDevice::Gamepad;
				}

				// 左スティックを [-1,1] に正規化（Y は上が +）
				const float nx = (st.Gamepad.sThumbLX >= 0)
					? static_cast<float>(st.Gamepad.sThumbLX) / 32767.0f
					: static_cast<float>(st.Gamepad.sThumbLX) / 32768.0f;
				const float ny = (st.Gamepad.sThumbLY >= 0)
					? static_cast<float>(st.Gamepad.sThumbLY) / 32767.0f
					: static_cast<float>(st.Gamepad.sThumbLY) / 32768.0f;
				gPadLT = static_cast<float>(st.Gamepad.bLeftTrigger) / 255.0f;
				gPadRT = static_cast<float>(st.Gamepad.bRightTrigger) / 255.0f;

				gPadLX = nx;
				gPadLY = ny;

				// 半径デッドゾーン
				float r = std::sqrt(gPadLX * gPadLX + gPadLY * gPadLY);
				if (r < gPadDeadZone) {
					gPadLX = gPadLY = 0.0f;
				} else if (r > 1.0f) {
					gPadLX /= r;
					gPadLY /= r;
				}

				// —— ゲームパッドボタンの「サンプリングのみ」 —— //
				for (auto& kv : gConfigs) {
					ActionConfig& cfg = kv.second;
					if (cfg.type != ActionType::Button || cfg.padButtons.empty()) continue;

					bool now = false;
					for (int flag : cfg.padButtons) {
						if (flag == PAD_LT) {
							// 左トリガーをボタン扱い
							if (st.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
								now = true;
								break;
							}
						} else if (flag == PAD_RT) {
							// 右トリガーをボタン扱い
							if (st.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
								now = true;
								break;
							}
						} else {
							// 通常のボタン（A/B/X/Y/LB/RB/...）
							if ((st.Gamepad.wButtons & static_cast<WORD>(flag)) != 0) {
								now = true;
								break;
							}
						}
					}
					gPadDownSample[kv.first] = now;
				}

			}
		}

		void Update(float dt) {
			gTime += dt;

			PollMouseWin32();
			PollKeyboardWin32(gKeys);

			gAxisCache.clear();
			gKbDownSample.clear();
			gPadDownSample.clear();
			gMouseDownSample.clear();

			// —— キーボード「サンプリングのみ」ボタン + 軸の加算 —— //
			for (auto& kv : gConfigs) {
				const std::string& name = kv.first;
				ActionConfig& cfg = kv.second;

				if (!gLayers.IsAllowed(name)) {
					if (cfg.type == ActionType::Axis) gAxisCache[name] = 0.0f;
					gKbDownSample[name] = false;
					gMouseDownSample[name] = false;
					continue;
				}

				if (cfg.type == ActionType::Button) {
					bool now = false;
					for (auto k : cfg.buttonKeys) {
						if (KeyNow(k)) {
							now = true;
							gLastInputDevice = LastInputDevice::Keyboard;
							break;
						}
					}
					bool mouseNow = false;
					for (auto mb : cfg.mouseButtons) {
						if (MouseBtnNow(mb)) {
							mouseNow = true;
							gLastInputDevice = LastInputDevice::Mouse;
							break;
						}
					}
					gKbDownSample[name] = now; // サンプリングのみ
					gMouseDownSample[name] = mouseNow;
				} else {
					float v = 0.0f;
					for (auto& ab : cfg.axisKeys) {
						switch (ab.source) {
						case AxisBinding::Source::Keyboard:
							if (KeyNow(ab.key)) {
								v += ab.value;
								gLastInputDevice = LastInputDevice::Keyboard;
							}
							break;
						case AxisBinding::Source::MouseMoveX:
							if (!ab.needButton || MouseBtnNow(ab.requireButton)) v += gMouseDelta.x * ab.value;
							break;
						case AxisBinding::Source::MouseMoveY:
							if (!ab.needButton || MouseBtnNow(ab.requireButton)) v += gMouseDelta.y * ab.value;
							break;
						case AxisBinding::Source::MouseWheel:
							if (!ab.needButton || MouseBtnNow(ab.requireButton)) v += gWheelDelta * ab.value;
							break;
						}
					}
					if (cfg.clampAxis01) {
						if (v < -1.0f) v = -1.0f; else if (v > 1.0f) v = 1.0f;
					}
					gAxisCache[name] = v; // 軸キャッシュ
				}
			}

			// ゲームパッドのサンプリング（ボタン→gPadDownSample、左スティック→gPadLX/gPadLY）
			PollXInput();

			// 左スティックをアクションに書き込み（パッド優先：非ゼロなら上書き）
			if (!gPadAxisX.empty()) {
				if (std::fabs(gPadLX) > 0.0001f) gAxisCache[gPadAxisX] = gPadLX;
			}
			if (!gPadAxisY.empty()) {
				if (std::fabs(gPadLY) > 0.0001f) gAxisCache[gPadAxisY] = gPadLY;
			}

			// —— 合成とエッジ判定（唯一の入口）——
			for (auto& kv : gConfigs) {
				const std::string& name = kv.first;
				ActionConfig& cfg = kv.second;
				ActionState& st = gStates[name];

				if (cfg.type != ActionType::Button) continue;

				const bool kb = gKbDownSample.count(name) ? gKbDownSample[name] : false;
				const bool pd = gPadDownSample.count(name) ? gPadDownSample[name] : false;
				const bool ms = gMouseDownSample.count(name) ? gMouseDownSample[name] : false;
				const bool now = (kb || pd || ms);
				const bool prev = st.down;

				st.down = now;
				st.pressed = (!prev && now);
				st.released = (prev && !now);

				// タイムスタンプ（統一管理：段階的な OR によるエッジ消失を防止）
				if (st.pressed) {
					st.prevPressTime = st.lastPressTime;
					st.lastPressTime = gTime;
				}
				if (st.released) {
					st.lastReleaseTime = gTime;
				}

				// held / repeat / buffer
				st.heldTime = st.down ? (st.heldTime + dt) : 0.0f;

				if (cfg.enableRepeat) {
					if (st.pressed) {
						st.repeatCount = 0; st.repeatTimer = -cfg.repeatDelay;
					} else if (st.down) {
						st.repeatTimer += dt;
						if (st.repeatTimer >= 0.0f) {
							float interval = (cfg.repeatRate > 0.0f ? 1.0f / cfg.repeatRate : 1.0f);
							if (st.repeatTimer >= interval) {
								st.repeatTimer -= interval; st.repeatCount++;
							}
						}
					} else { st.repeatCount = 0; st.repeatTimer = 0.0f; }
				} else { st.repeatCount = 0; st.repeatTimer = 0.0f; }

				if (st.bufferRemain > 0.0f) {
					st.bufferRemain -= dt; if (st.bufferRemain < 0.0f) st.bufferRemain = 0.0f;
				}
			}

			// 前フレームのキーボード状態を保存
			std::memcpy(gPrev, gKeys, 256);

			// === 振動タイマー更新 ===
			if (gVibActive && gVibRemain > 0.0f) {
				gVibRemain -= dt;
				if (gVibRemain <= 0.0f) {
					gVibRemain = 0.0f;
					StopPadVibration();
				}
			}


		}

		// ===== 照会 =====
		bool  IsDown(const std::string& a) { return gStates[a].down; }
		bool  IsPressed(const std::string& a) { return gStates[a].pressed; }
		bool  IsReleased(const std::string& a) { return gStates[a].released; }
		float HeldTime(const std::string& a) { return gStates[a].heldTime; }
		bool  IsRepeated(const std::string& a) { const auto& st = gStates[a]; if (st.pressed) return true; return st.repeatCount > 0; }
		float GetAxis(const std::string& a) { auto it = gAxisCache.find(a); if (it == gAxisCache.end()) return 0.0f; return it->second; }

		// ===== ダブルタップ：必ず一度「離す」動作を挟む =====
		bool IsDoubleTapped(const std::string& a, float window) {
			ActionState& st = gStates[a];
			if (!st.pressed) return false; // このフレームの押下エッジ時のみ判定
			const float sincePrevPress = gTime - st.prevPressTime;
			const bool  hadReleaseBetween = (st.lastReleaseTime > st.prevPressTime);
			return (sincePrevPress > 0.0f && sincePrevPress <= window && hadReleaseBetween);
		}

		// ===== 入力バッファ =====
		void  OpenBuffer(const std::string& a, float s) { if (s < 0.0f) s = 0.0f; gStates[a].bufferRemain = s; }
		bool  ConsumeBuffer(const std::string& a) { ActionState& st = gStates[a]; if (st.bufferRemain > 0.0f) { st.bufferRemain = 0.0f; return true; } return false; }
		bool  Buffered(const std::string& a) { return gStates[a].bufferRemain > 0.0f; }

		// ===== バインド =====
		void  BindButton(const std::string& a, const std::vector<int>& keys) {
			ActionConfig& cfg = gConfigs[a];
			cfg.type = ActionType::Button;
			cfg.buttonKeys = keys;
		}
		void  BindAxis(const std::string& a, const std::vector<AxisBinding>& keys, bool clamp01) {
			ActionConfig& cfg = gConfigs[a];
			cfg.type = ActionType::Axis;
			cfg.axisKeys = keys;
			cfg.clampAxis01 = clamp01;
		}
		void  BindMouseButtons(const std::string& action, const std::vector<MouseButton>& buttons) {
			ActionConfig& cfg = gConfigs[action];
			cfg.type = ActionType::Button;
			cfg.mouseButtons = buttons;
		}

		// ===== ゲームパッド API =====
		void BindAxisPadLeft(const std::string& actionX, const std::string& actionY) {
			gPadAxisX = actionX;
			gPadAxisY = actionY;
		}
		void SetPadDeadZone(float dz) { gPadDeadZone = (dz < 0.0f) ? 0.0f : (dz > 1.0f ? 1.0f : dz); }
		void SetActiveGamepad(int index) { gActivePad = index; }
		int  GetActiveGamepad() { return (gActivePad >= 0) ? gActivePad : 0; }
		int  GetPadCount() { return gPadCount; }

		void BindPadButtons(const std::string& action, const std::vector<int>& padButtons) {
			ActionConfig& cfg = gConfigs[action];
			cfg.type = ActionType::Button;
			cfg.padButtons = padButtons;
		}
		// ===== 振動関連 API =====
		void SetPadVibration(float leftMotor, float rightMotor, float seconds)
		{
			auto clamp01 = [](float v) {
				if (v < 0.0f) return 0.0f;
				if (v > 1.0f) return 1.0f;
				return v;
				};

			leftMotor = clamp01(leftMotor);
			rightMotor = clamp01(rightMotor);

			int index = (gActivePad >= 0) ? gActivePad : gLastUsedPadIndex;
			if (index < 0 || index > 3) {
				return;
			}

			XINPUT_VIBRATION vib{};
			vib.wLeftMotorSpeed = static_cast<WORD>(leftMotor * 65535.0f);
			vib.wRightMotorSpeed = static_cast<WORD>(rightMotor * 65535.0f);

			if (XInputSetState(static_cast<DWORD>(index), &vib) == ERROR_SUCCESS) {
				gVibIndex = index;
				gVibRemain = (seconds > 0.0f) ? seconds : 0.0f;
				gVibActive = true;
			}
		}

		void StopPadVibration()
		{
			if (gVibIndex < 0 || gVibIndex > 3) {
				gVibActive = false;
				gVibRemain = 0.0f;
				gVibIndex = -1;
				return;
			}

			XINPUT_VIBRATION vib{};
			vib.wLeftMotorSpeed = 0;
			vib.wRightMotorSpeed = 0;
			XInputSetState(static_cast<DWORD>(gVibIndex), &vib);

			gVibActive = false;
			gVibRemain = 0.0f;
			gVibIndex = -1;
		}

		bool IsPadVibrating()
		{
			return gVibActive;
		}


		// 純粋に左スティックだけ取りたいとき用
		Vector2 GetPadLeftStick() {
			Vector2 v{};
			v.x = gPadLX;
			v.y = gPadLY;
			return v;
		}

		bool HasPadLeftStickInput() {
			return (std::fabs(gPadLX) > 0.0001f || std::fabs(gPadLY) > 0.0001f);
		}

		// ===== 統一・正規化済み移動ベクトル =====
		Vector2 GetMoveVectorNormalized() {
			Vector2 v{};
			// ゲームパッド優先
			if (std::fabs(gPadLX) > 0.0001f || std::fabs(gPadLY) > 0.0001f) {
				v.x = gPadLX; v.y = gPadLY;
			} else {
				v.x = GetAxis("MoveX");
				v.y = GetAxis("MoveY");
			}
			float len = std::sqrt(v.x * v.x + v.y * v.y);
			if (len > 1.0f) { v.x /= len; v.y /= len; }
			return v;
		}

		Vector2 GetMousePosition() { return gMousePos; }
		Vector2 GetMouseDelta() { return gMouseDelta; }
		float   GetMouseWheelDelta() { return gWheelDelta; }
		bool    IsMouseDown(MouseButton btn) { return MouseBtnNow(btn); }
		bool    IsMousePressed(MouseButton btn) {
			int idx = static_cast<int>(btn);
			if (idx < 0 || idx >= 3) return false;
			return (!gMousePrevBtns[idx] && gMouseNow[idx]);
		}
		bool    IsMouseReleased(MouseButton btn) {
			int idx = static_cast<int>(btn);
			if (idx < 0 || idx >= 3) return false;
			return (gMousePrevBtns[idx] && !gMouseNow[idx]);
		}

		LastInputDevice GetLastInputDevice() {
			return gLastInputDevice;
		}

		bool IsUsingGamepad() {
			return gLastInputDevice == LastInputDevice::Gamepad;
		}

		bool IsUsingKeyboard() {
			return gLastInputDevice == LastInputDevice::Keyboard;
		}

	} // namespace HINPUT
} // namespace HIKARI
