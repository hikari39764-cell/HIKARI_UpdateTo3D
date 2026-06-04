#pragma once

#include <d3d12.h>

namespace HIKARI::MESHRENDERER::ROOT_PARAM {

    // Camera -> b0
    constexpr UINT Camera = 0;
    // Object -> b1
    constexpr UINT Object = 1;
    // Light -> b2
    constexpr UINT Light = 2;
    // ShadowMap -> t2
    constexpr UINT ShadowMap = 3;
    // ShadowCB -> b4
    constexpr UINT ShadowCB = 4;
    // SkyEnvironment -> b5
    constexpr UINT SkyEnvironment = 5;
    // SkyCube -> t6
    constexpr UINT SkyCube = 6;
    // SceneDepth -> t7
    constexpr UINT SceneDepth = 7;
    // SceneColor -> t8
    constexpr UINT SceneColor = 8;
    // IBL Irradiance Cubemap -> t9
    constexpr UINT IblIrradiance = 9;
    // IBL Prefiltered Cubemap -> t10
    constexpr UINT IblPrefiltered = 10;
    // IBL BRDF LUT -> t11
    constexpr UINT IblBrdfLut = 11;
    // Reflection Probe Prefiltered Cubemap -> t12
    constexpr UINT ReflectionProbePrefiltered = 12;
    // SSAO texture -> t13
    constexpr UINT Ssao = 13;
    // LightProbe SH buffer -> t14
    constexpr UINT LightProbeSh = 14;
    // Mesh object data buffer -> t15
    constexpr UINT ObjectData = 15;
    // Mesh object data index -> b6
    constexpr UINT ObjectIndex = 16;
    // Mesh material data buffer -> t16
    constexpr UINT MaterialData = 17;
    // Mesh material data index -> b7
    constexpr UINT MaterialIndex = 18;
    // 材質テクスチャプール -> t20[]
    constexpr UINT TexturePool = 19;
    // JointPalette -> b3, skinned only
    constexpr UINT JointPalette = 20;

} // namespace HIKARI::MESHRENDERER::ROOT_PARAM
