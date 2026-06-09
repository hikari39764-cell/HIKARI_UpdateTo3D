#include "Render3D/Runtime/HIKARI_SurfaceDrawRoute.h"

#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPacket.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI::RENDER3D::RUNTIME {

    namespace {
        bool HasValidSubmitPrimitiveTarget(const SurfaceDrawPacket& packet) {
            return
                packet.model != nullptr &&
                packet.meshIndex != kInvalidRenderSurfaceIndex &&
                packet.primitiveIndex != kInvalidRenderSurfaceIndex &&
                packet.meshIndex < packet.model->meshes.size() &&
                packet.primitiveIndex < packet.model->meshes[packet.meshIndex].primitives.size();
        }

        SurfaceDrawRouteRejectReason ClassifyCommonSurfaceDrawRoute(
            const SurfaceDrawPacket& packet,
            bool requireObjectDataShader) {
            if (!packet.valid) {
                return SurfaceDrawRouteRejectReason::InvalidPacket;
            }
            if (!packet.key.resourceKeyValid) {
                return SurfaceDrawRouteRejectReason::InvalidResourceKey;
            }
            if (requireObjectDataShader && !packet.key.objectDataCompatible) {
                return SurfaceDrawRouteRejectReason::LegacyShader;
            }
            if (packet.hasRuntimeAnimation) {
                return SurfaceDrawRouteRejectReason::RuntimeAnimation;
            }
            if (packet.hasSpecialRenderDebug) {
                return SurfaceDrawRouteRejectReason::SpecialDebug;
            }
            if (packet.skinned) {
                return SurfaceDrawRouteRejectReason::Skinned;
            }
            if (!HasValidSubmitPrimitiveTarget(packet)) {
                return SurfaceDrawRouteRejectReason::InvalidPrimitive;
            }
            return SurfaceDrawRouteRejectReason::None;
        }
    }

    bool IsSurfaceObjectDataVertexShader(std::string_view vertexShaderId) {
        return vertexShaderId.empty() || vertexShaderId == "Render3D_StaticVS";
    }

    bool IsSurfaceObjectDataPixelShader(std::string_view shaderId, std::string_view pixelShaderId) {
        const std::string_view id = !pixelShaderId.empty() ? pixelShaderId : shaderId;
        return id.empty() ||
            id == "PBR" ||
            id == "StaticLit" ||
            id == "StaticFx" ||
            id == "MaterialFx" ||
            id == "Render3D_StaticPS" ||
            id == "Render3D_StaticFxPS" ||
            id == "Render3D_GeometryBufferPS";
    }

    SurfaceDrawShaderRoute ResolveSurfaceDrawShaderRoute(
        const Material* materialOverride,
        const MaterialAsset* materialAsset,
        std::string_view materialFxProfileId) {

        SurfaceDrawShaderRoute route{};
        if (materialOverride != nullptr) {
            route.shaderProfileId = materialOverride->GetShaderProfileId();
            if (route.shaderProfileId.empty()) {
                route.shaderProfileId = "PBR";
            }
            route.featureBits = materialOverride->GetFeatureBits();
        } else if (materialAsset != nullptr) {
            route.shaderProfileId = materialAsset->shaderProfileId.empty() ? "PBR" : materialAsset->shaderProfileId;
            route.featureBits = materialAsset->featureBits;
            route.alphaMode = materialAsset->alphaMode;
            route.doubleSided = materialAsset->doubleSided;
        }

        if (!materialFxProfileId.empty()) {
            MaterialFxProfile profile{};
            if (MaterialFxProfile::LoadById(std::string(materialFxProfileId), profile)) {
                if (!profile.shaderProfileId.empty()) {
                    route.shaderProfileId = profile.shaderProfileId;
                }
                if (!profile.vertexShaderId.empty()) {
                    route.vertexShaderId = profile.vertexShaderId;
                }
                if (!profile.pixelShaderId.empty()) {
                    route.pixelShaderId = profile.pixelShaderId;
                }
                route.featureBits = profile.featureBits;
                route.doubleSided = route.doubleSided || profile.doubleSided;
                route.depthAwareMaterialFx = profile.renderPhase == MaterialFxRenderPhase::SceneDepth;
                if (materialAsset != nullptr &&
                    (materialAsset->featureBits & MATERIAL_FEATURES::AlphaMask) != 0) {
                    route.featureBits |= MATERIAL_FEATURES::AlphaMask;
                }
            }
        }

        route.objectDataCompatible =
            IsSurfaceObjectDataVertexShader(route.vertexShaderId) &&
            IsSurfaceObjectDataPixelShader(route.shaderProfileId, route.pixelShaderId);
        return route;
    }

    SurfaceDrawRouteRejectReason ClassifyForwardSurfaceDrawRoute(const SurfaceDrawPacket& packet) {
        if (!packet.forwardCandidate) {
            return SurfaceDrawRouteRejectReason::NoForward;
        }
        const SurfaceDrawRouteRejectReason commonReason = ClassifyCommonSurfaceDrawRoute(packet, true);
        if (commonReason != SurfaceDrawRouteRejectReason::None) {
            return commonReason;
        }
        if (packet.key.depthAwareMaterialFx) {
            return SurfaceDrawRouteRejectReason::DepthAwareMaterialFx;
        }
        return SurfaceDrawRouteRejectReason::None;
    }

    SurfaceDrawRouteRejectReason ClassifyShadowSurfaceDrawRoute(const SurfaceDrawPacket& packet) {
        if (!packet.shadowCandidate) {
            return SurfaceDrawRouteRejectReason::NoShadow;
        }
        const SurfaceDrawRouteRejectReason commonReason = ClassifyCommonSurfaceDrawRoute(packet, false);
        if (commonReason != SurfaceDrawRouteRejectReason::None) {
            return commonReason;
        }
        if (packet.key.transparent) {
            return SurfaceDrawRouteRejectReason::Transparent;
        }
        return SurfaceDrawRouteRejectReason::None;
    }

    SurfaceDrawRouteBucket GetSurfaceDrawRouteBucket(SurfaceDrawRouteRejectReason reason) {
        switch (reason) {
        case SurfaceDrawRouteRejectReason::None:
            return SurfaceDrawRouteBucket::MainRoute;
        case SurfaceDrawRouteRejectReason::NoForward:
        case SurfaceDrawRouteRejectReason::NoShadow:
            return SurfaceDrawRouteBucket::NoPass;
        case SurfaceDrawRouteRejectReason::AlphaMasked:
            return SurfaceDrawRouteBucket::AlphaMask;
        case SurfaceDrawRouteRejectReason::Transparent:
            return SurfaceDrawRouteBucket::Transparent;
        case SurfaceDrawRouteRejectReason::DepthAwareMaterialFx:
            return SurfaceDrawRouteBucket::DepthAwareMaterialFx;
        case SurfaceDrawRouteRejectReason::RuntimeAnimation:
        case SurfaceDrawRouteRejectReason::SpecialDebug:
            return SurfaceDrawRouteBucket::RuntimeSpecial;
        case SurfaceDrawRouteRejectReason::Skinned:
            return SurfaceDrawRouteBucket::Skinned;
        case SurfaceDrawRouteRejectReason::LegacyShader:
            return SurfaceDrawRouteBucket::LegacyShader;
        case SurfaceDrawRouteRejectReason::InvalidPacket:
        case SurfaceDrawRouteRejectReason::InvalidResourceKey:
        case SurfaceDrawRouteRejectReason::InvalidPrimitive:
        default:
            return SurfaceDrawRouteBucket::Invalid;
        }
    }

    bool IsSurfaceDrawRouteAccepted(SurfaceDrawRouteRejectReason reason) {
        return reason == SurfaceDrawRouteRejectReason::None;
    }

} // namespace HIKARI::RENDER3D::RUNTIME
