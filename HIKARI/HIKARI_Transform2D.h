#pragma once
#include "Matrix3x3.h"

namespace HIKARI {

	struct Transform2D {
		Vector2 position{ 0.0f, 0.0f };
		Vector2 scale{ 1.0f, 1.0f };
		float rotation{ 0.0f }; // ラジアン角度
		Vector2 pivotPx{ 0.0f, 0.0f }; // オブジェクト内のピクセル基準点

		// ローカル座標 → ワールド座標（カメラ無し）
		Matrix3x3 ToWorld(float width, float height) const;
	};

} // namespace HIKARI
