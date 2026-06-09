#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Render3D/Core/HIKARI_ModelAsset.h"

namespace HIKARI {
    class Material;
}

namespace HIKARI::RENDER3D::RUNTIME {

    struct SurfaceDrawPacket;

    enum class SurfaceDrawRouteRejectReason : uint8_t {
        None = 0,
        NoForward,
        NoShadow,
        InvalidPacket,
        InvalidResourceKey,
        LegacyShader,
        DepthAwareMaterialFx,
        RuntimeAnimation,
        SpecialDebug,
        Skinned,
        Transparent,
        AlphaMasked,
        InvalidPrimitive,
    };

    enum class SurfaceDrawRouteBucket : uint8_t {
        MainRoute = 0,
        NoPass,
        AlphaMask,
        Transparent,
        DepthAwareMaterialFx,
        RuntimeSpecial,
        Skinned,
        LegacyShader,
        Invalid,
    };

    struct SurfaceDrawShaderRoute {
        std::string shaderProfileId = "PBR";
        std::string vertexShaderId{};
        std::string pixelShaderId{};
        uint32_t featureBits = 0;
        AlphaMode alphaMode = AlphaMode::Opaque;
        bool doubleSided = false;
        bool objectDataCompatible = false;
        bool depthAwareMaterialFx = false;
    };

    bool IsSurfaceObjectDataVertexShader(std::string_view vertexShaderId);
    bool IsSurfaceObjectDataPixelShader(std::string_view shaderId, std::string_view pixelShaderId);

    SurfaceDrawShaderRoute ResolveSurfaceDrawShaderRoute(
        const Material* materialOverride,
        const MaterialAsset* materialAsset,
        std::string_view materialFxProfileId);

    SurfaceDrawRouteRejectReason ClassifyForwardSurfaceDrawRoute(const SurfaceDrawPacket& packet);
    SurfaceDrawRouteRejectReason ClassifyShadowSurfaceDrawRoute(const SurfaceDrawPacket& packet);
    SurfaceDrawRouteBucket GetSurfaceDrawRouteBucket(SurfaceDrawRouteRejectReason reason);
    bool IsSurfaceDrawRouteAccepted(SurfaceDrawRouteRejectReason reason);

} // namespace HIKARI::RENDER3D::RUNTIME
