#pragma once

#include <d3d12.h>

namespace HIKARI::MESHRENDERER::ROOT_PARAM {

    // Camera -> b0
    constexpr UINT Camera = 0;
    // Object -> b1
    constexpr UINT Object = 1;
    // Light -> b2
    constexpr UINT Light = 2;
    // BaseColor -> t0
    constexpr UINT BaseColor = 3;
    // Normal -> t1
    constexpr UINT Normal = 4;
    // ShadowMap -> t2
    constexpr UINT ShadowMap = 5;
    // ShadowCB -> b4
    constexpr UINT ShadowCB = 6;
    // Emissive -> t3
    constexpr UINT Emissive = 7;
    // MetallicRoughness -> t4
    constexpr UINT MetallicRoughness = 8;
    // Occlusion -> t5
    constexpr UINT Occlusion = 9;
    // SkyEnvironment -> b5
    constexpr UINT SkyEnvironment = 10;
    // SkyCube -> t6
    constexpr UINT SkyCube = 11;
    // SceneDepth -> t7
    constexpr UINT SceneDepth = 12;
    // JointPalette -> b3, skinned only
    constexpr UINT JointPalette = 13;

} // namespace HIKARI::MESHRENDERER::ROOT_PARAM
