#pragma once
#include <array>
#include "HIKARI_Camera3D.h"
#include "HIKARI_Transform3D.h"
#include "HIKARI_Transform2D.h"
#include "HIKARI_Renderer.h"

namespace HIKARI::RENDERER3D {

    struct WireCube {
        Transform3D transform{};
        float size = 1.0f;
        unsigned int rgba = 0xFFFFFFFF;
    };

    void Reset();
    void SubmitWireCube(const WireCube& cube);
    void RenderAll(const Camera3D& camera, float screenW, float screenH);

} // namespace HIKARI::RENDERER3D
