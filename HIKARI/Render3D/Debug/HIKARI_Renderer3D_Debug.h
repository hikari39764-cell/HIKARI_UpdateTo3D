#pragma once
#include <array>
#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_Transform3D.h"
#include "Render2D/HIKARI_Transform2D.h"

namespace HIKARI::RENDERER3D::DEBUG {

    // 3D Debug Primitive Layer:
    // このモジュールはワイヤーフレーム/補助図元を描画するための一時的なデバッグ層。
    // 将来の Mesh/Material/Model ベースの正式 3D レンダラ責務は持たない。

    struct WireCube {
        Transform3D transform{};
        float size = 1.0f;
        unsigned int rgba = 0xFFFFFFFF;
    };

    struct Line3D {
        MATH::Vec3 from{};
        MATH::Vec3 to{};
        unsigned int rgba = 0xFFFFFFFF;
    };

    struct Axis3D {
        Transform3D transform{};
        float length = 1.0f;
        unsigned int xColor = 0xFF4C4CFF;
        unsigned int yColor = 0x4CFF4CFF;
        unsigned int zColor = 0x4C4CFFFF;
    };

    struct Grid3D {
        int halfCount = 10;
        float spacing = 1.0f;
        unsigned int rgba = 0x888888FF;
    };

    void Reset();
    void SubmitWireCube(const WireCube& cube);
    void SubmitLine3D(const Line3D& line);
    void SubmitAxis3D(const Axis3D& axis);
    void SubmitGrid3D(const Grid3D& grid);
    void RenderAll(const Camera3D& camera, float screenW, float screenH);

} // namespace HIKARI::RENDERER3D::DEBUG
