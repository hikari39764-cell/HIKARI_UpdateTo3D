#ifndef HIKARI_SHADOW_SAMPLING_INCLUDED
#define HIKARI_SHADOW_SAMPLING_INCLUDED

// 方向光影の受光側サンプリング (全 forward PS 共通)。
//
// 前提: この include より前に以下が宣言されていること。
// - Texture2D gShadowMap
// - SamplerComparisonState gShadowCmpSampler (s2, LESS_EQUAL, border=white)
// - ShadowCB: gShadowLightViewProj / gShadowEnabled / gShadowDepthBias /
//   gShadowNormalBias / gShadowStrength / gShadowPcfEnabled /
//   gShadowPcfRadius / gShadowTexelSizeX / gShadowTexelSizeY / gShadowEdgeFade

// Comparison sampler は 1 tap で 2x2 の hardware PCF を行うため、
// 中心 + 対角 4 の 5 tap で旧 9-tap 手動比較と同等以上の滑らかさになる。
float HikariSampleShadowVisibility(float2 uv, float currentDepth)
{
    float visibility =
        gShadowMap.SampleCmpLevelZero(gShadowCmpSampler, uv, currentDepth);
    if (gShadowPcfEnabled == 0)
    {
        return visibility;
    }

    float2 texelSize = float2(gShadowTexelSizeX, gShadowTexelSizeY) * gShadowPcfRadius;
    float sum = visibility;
    sum += gShadowMap.SampleCmpLevelZero(
        gShadowCmpSampler, uv + texelSize * float2(-1.0f, -1.0f), currentDepth);
    sum += gShadowMap.SampleCmpLevelZero(
        gShadowCmpSampler, uv + texelSize * float2( 1.0f, -1.0f), currentDepth);
    sum += gShadowMap.SampleCmpLevelZero(
        gShadowCmpSampler, uv + texelSize * float2(-1.0f,  1.0f), currentDepth);
    sum += gShadowMap.SampleCmpLevelZero(
        gShadowCmpSampler, uv + texelSize * float2( 1.0f,  1.0f), currentDepth);
    return sum / 5.0f;
}

float HikariShadowReceiverFade(float2 uv)
{
    if (gShadowEdgeFade <= 0.00001f)
    {
        return 1.0f;
    }

    float edgeDistance = min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y));
    return saturate(edgeDistance / gShadowEdgeFade);
}

float HikariSampleDirectionalShadow(
    float3 worldPosWS,
    float3 normalWS,
    uint receiveShadow)
{
    if (gShadowEnabled == 0 || receiveShadow == 0)
    {
        return 1.0f;
    }

    float3 biasedWorldPos = worldPosWS + normalWS * gShadowNormalBias;
    float4 lightClip = mul(gShadowLightViewProj, float4(biasedWorldPos, 1.0f));
    if (abs(lightClip.w) < 1e-5f)
    {
        return 1.0f;
    }

    float3 proj = lightClip.xyz / lightClip.w;
    float2 uv = float2(proj.x * 0.5f + 0.5f, -proj.y * 0.5f + 0.5f);
    if (uv.x < 0.0f || uv.x > 1.0f ||
        uv.y < 0.0f || uv.y > 1.0f ||
        proj.z < 0.0f || proj.z > 1.0f)
    {
        return 1.0f;
    }

    float currentDepth = proj.z - gShadowDepthBias;
    float visibility = HikariSampleShadowVisibility(uv, currentDepth);
    float shadowFactor = lerp(1.0f - gShadowStrength, 1.0f, visibility);
    return lerp(1.0f, shadowFactor, HikariShadowReceiverFade(uv));
}

#endif
