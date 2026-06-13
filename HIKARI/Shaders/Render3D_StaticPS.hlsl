// IMPORTANT:
// This cbuffer/register layout must stay in sync with Render3D_StaticFxPS.hlsl
// and MeshRenderer::ObjectGpuData / MaterialGpuData / LightCB / ShadowCB / SkyEnvironmentCB.

#define gFxUser0 gFxUser[0]
#define gFxUser1 gFxUser[1]
#define gFxUser2 gFxUser[2]
#define gFxUser3 gFxUser[3]
#define gFxUser4 gFxUser[4]
#define gFxUser5 gFxUser[5]
#define gFxUser6 gFxUser[6]
#define gFxUser7 gFxUser[7]


cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    float4 gTimeParams;
    float4 gScreenParams;
};

#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshObjectData.hlsli"

static const uint MATERIAL_UNLIT = 1u << 0;
static const uint MATERIAL_ALPHA_MASK = 1u << 1;
static const uint MATERIAL_EMISSIVE = 1u << 2;

#ifndef HIKARI_USE_COOK_TORRANCE_PBR
#define HIKARI_USE_COOK_TORRANCE_PBR 1
#endif

cbuffer LightCB : register(b2)
{
    float4 gDirectionalDir;
    float4 gDirectionalColor;
    float4 gAmbientColor;
    float4 gSpecularParams;
    float4 gPointLightPosRange[8];
    float4 gPointLightColorIntensity[8];
    float gDirectionalIntensity;
    float gAmbientIntensity;
    uint gPointLightCount;
    float gLightPadding;
    float4 gFogColorDensity;
    float4 gFogParams;
    uint gDebugView;
    float3 gDebugPadding;
};

cbuffer ShadowCB : register(b4)
{
    float4x4 gShadowLightViewProj;
    uint gShadowEnabled;
    float gShadowDepthBias;
    float gShadowNormalBias;
    float gShadowStrength;
    uint gShadowPcfEnabled;
    float gShadowPcfRadius;
    float gShadowTexelSizeX;
    float gShadowTexelSizeY;
};

cbuffer SkyEnvironmentCB : register(b5)
{
    float4 gSkyZenithExposure;
    float4 gSkyHorizonReflection;
    float4 gSkyGroundAmbient;
    float4 gSkyParams;
    float4 gIblParams;
    float4 gReflectionProbePositionRadius;
    float4 gReflectionProbeParams;
    float4 gReflectionProbeIntensity;
    float4 gReflectionProbeInfluenceBoxMin;
    float4 gReflectionProbeInfluenceBoxMax;
    float4 gReflectionProbeProjectionBoxMin;
    float4 gReflectionProbeProjectionBoxMax;
    float4 gReflectionProbeShapeParams;
    float4 gAoParams;
    float4 gLightProbeVolumeOrigin;
    float4 gLightProbeVolumeSpacing;
    float4 gLightProbeVolumeCounts;
};

#define gSkyZenithColor gSkyZenithExposure.rgb
#define gSkyExposure gSkyZenithExposure.a
#define gSkyHorizonColor gSkyHorizonReflection.rgb
#define gSkyReflectionIntensity gSkyHorizonReflection.a
#define gSkyGroundColor gSkyGroundAmbient.rgb
#define gSkyAmbientFromSky gSkyGroundAmbient.a
#define gSkyMode gSkyParams.x
#define gSkyHasCubemap gSkyParams.y
#define gSkyHorizonPower gSkyParams.z
#define gSkyYaw gSkyParams.w
#define gIblHasIrradiance gIblParams.x
#define gIblHasPrefiltered gIblParams.y
#define gIblHasBrdfLut gIblParams.z
#define gIblPrefilteredMipCount gIblParams.w
#define gReflectionProbePosition gReflectionProbePositionRadius.xyz
#define gReflectionProbeRadius gReflectionProbePositionRadius.w
#define gReflectionProbeEnabled gReflectionProbeParams.x
#define gReflectionProbeHasPrefiltered gReflectionProbeParams.y
#define gReflectionProbeHasBrdfLut gReflectionProbeParams.z
#define gReflectionProbeMipCount gReflectionProbeParams.w
#define gReflectionProbeSpecularIntensity gReflectionProbeIntensity.x
#define gReflectionProbeInfluenceShape gReflectionProbeShapeParams.x
#define gReflectionProbeProjectionShape gReflectionProbeShapeParams.y
#define gReflectionProbeBlendDistance gReflectionProbeShapeParams.z
#define gReflectionProbePriority gReflectionProbeShapeParams.w
#define gSsaoEnabled gAoParams.x
#define gSsaoDiffuseStrength gAoParams.y
#define gSsaoSpecularStrength gAoParams.z
#define gLightProbeEnabled gLightProbeVolumeOrigin.w
#define gLightProbeOrigin gLightProbeVolumeOrigin.xyz
#define gLightProbeSpacing gLightProbeVolumeSpacing.xyz
#define gLightProbeIntensity gLightProbeVolumeSpacing.w
#define gLightProbeCountX ((uint)(gLightProbeVolumeCounts.x + 0.5f))
#define gLightProbeCountY ((uint)(gLightProbeVolumeCounts.y + 0.5f))
#define gLightProbeCountZ ((uint)(gLightProbeVolumeCounts.z + 0.5f))
#define gLightProbeProbeCount ((uint)(gLightProbeVolumeCounts.w + 0.5f))

Texture2D gShadowMap : register(t2);
TextureCube gSkyCube : register(t6);
Texture2D gSceneDepthTex : register(t7);
Texture2D gSceneColorTex : register(t8);
TextureCube gIblIrradianceTex : register(t9);
TextureCube gIblPrefilteredTex : register(t10);
Texture2D gIblBrdfLutTex : register(t11);
TextureCube gReflectionProbePrefilteredTex : register(t12);
Texture2D gSsaoTex : register(t13);
StructuredBuffer<float4> gLightProbeSh : register(t14);
SamplerState gLinearWrap : register(s0);
SamplerState gShadowSampler : register(s1);

#include "Include/HIKARI_PbrCommon.hlsli"
#include "Include/HIKARI_SkyEnvironmentCommon.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
    nointerpolation uint materialDataIndex : TEXCOORD2;
    nointerpolation uint receiveShadow : TEXCOORD3;
    nointerpolation uint debugClusterId : TEXCOORD6;
    nointerpolation uint debugSurfaceId : TEXCOORD7;
    nointerpolation uint debugLodIndex : TEXCOORD8;
    nointerpolation uint debugDrawBucket : TEXCOORD9;
};

uint HikariHashDebugId(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float3 HikariDebugColorFromId(uint value)
{
    uint h = HikariHashDebugId(value + 1u);
    return float3(
        0.18f + float((h >> 0u) & 255u) / 255.0f * 0.78f,
        0.18f + float((h >> 8u) & 255u) / 255.0f * 0.78f,
        0.18f + float((h >> 16u) & 255u) / 255.0f * 0.78f);
}

float3 HikariLodDebugColor(uint lodIndex)
{
    if (lodIndex == 0u) return float3(0.95f, 0.20f, 0.18f);
    if (lodIndex == 1u) return float3(0.95f, 0.70f, 0.16f);
    if (lodIndex == 2u) return float3(0.38f, 0.86f, 0.30f);
    if (lodIndex == 3u) return float3(0.18f, 0.72f, 0.96f);
    return float3(0.55f, 0.38f, 0.95f);
}

float3 HikariLodHeatColor(uint lodIndex)
{
    float t = saturate(float(lodIndex) / 4.0f);
    return lerp(float3(1.0f, 0.16f, 0.10f), float3(0.14f, 0.45f, 1.0f), t);
}

float3 ResolveShadingNormal(HikariMeshMaterialData materialData, float3 normalWS, float4 tangentWS, float2 uv)
{
    float3 n = normalize(normalWS);
    if (materialData.hasNormalTexture == 0)
    {
        return n;
    }

    float3 t = tangentWS.xyz;
    if (dot(t, t) < 1e-5f)
    {
        return n;
    }

    t = normalize(t);
    t = t - n * dot(n, t);
    if (dot(t, t) < 1e-5f)
    {
        return n;
    }
    t = normalize(t);
    float3 b = normalize(cross(n, t) * tangentWS.w);

    float3 normalTS =
        HikariSampleMaterialTexture(
            materialData.normalTextureDescriptorIndex,
            gLinearWrap,
            uv,
            float4(0.5f, 0.5f, 1.0f, 1.0f)).xyz * 2.0f - 1.0f;
    normalTS.xy *= materialData.normalScale;
    normalTS = normalize(normalTS);
    return normalize(normalTS.x * t + normalTS.y * b + normalTS.z * n);
}

float3 AccumulatePointLight(float3 normalWS, float3 worldPosWS, float3 viewDir)
{
    float3 sum = 0.0f.xxx;
    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        if (i >= gPointLightCount)
        {
            break;
        }

        float3 lightPos = gPointLightPosRange[i].xyz;
        float range = max(gPointLightPosRange[i].w, 0.001f);
        float3 toLight = lightPos - worldPosWS;
        float dist = length(toLight);
        float3 l = (dist > 1e-5f) ? (toLight / dist) : float3(0.0f, 1.0f, 0.0f);

        float atten = saturate(1.0f - dist / range);
        atten *= atten;

        float ndotl = saturate(dot(normalWS, l));
        float3 h = normalize(l + viewDir);
        float spec = pow(saturate(dot(normalWS, h)), gSpecularParams.y);

        float3 color = gPointLightColorIntensity[i].rgb;
        float intensity = gPointLightColorIntensity[i].w;
        sum += color * (atten * intensity) * (ndotl + gSpecularParams.x * spec);
    }
    return sum;
}

float3 AccumulatePointLightPbr(
    float3 baseColor,
    float metallic,
    float roughness,
    float3 normalWS,
    float3 worldPosWS,
    float3 viewDir)
{
    float3 sum = 0.0f.xxx;

    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        if (i >= gPointLightCount)
        {
            break;
        }

        float3 lightPos = gPointLightPosRange[i].xyz;
        float range = max(gPointLightPosRange[i].w, 0.001f);
        float3 toLight = lightPos - worldPosWS;
        float dist = length(toLight);
        float3 l = (dist > 1e-5f) ? (toLight / dist) : float3(0.0f, 1.0f, 0.0f);

        float atten = saturate(1.0f - dist / range);
        atten *= atten;

        float3 color = gPointLightColorIntensity[i].rgb;
        float intensity = gPointLightColorIntensity[i].w;

        sum += HikariEvaluateDirectPbr(
            baseColor,
            metallic,
            roughness,
            normalWS,
            viewDir,
            l,
            color,
            intensity * atten);
    }

    return sum;
}

float3 ResolveEmissive(HikariMeshMaterialData materialData, float2 uv)
{
    float3 emissive = materialData.emissiveFactor.rgb;
    if (materialData.hasEmissiveTexture != 0)
    {
        emissive *= HikariSampleMaterialTexture(
            materialData.emissiveTextureDescriptorIndex,
            gLinearWrap,
            uv,
            float4(0.0f, 0.0f, 0.0f, 1.0f)).rgb;
    }
    return emissive * materialData.emissiveFactor.a;
}

void ResolvePbrInputs(HikariMeshMaterialData materialData, float2 uv, out float metallic, out float roughness, out float occlusion)
{
    metallic = saturate(materialData.pbrParams.x);
    roughness = clamp(materialData.pbrParams.y, 0.04f, 1.0f);
    occlusion = 1.0f;

    if (materialData.hasMetallicRoughnessTexture != 0)
    {
        float4 mr = HikariSampleMaterialTexture(
            materialData.metallicRoughnessTextureDescriptorIndex,
            gLinearWrap,
            uv,
            float4(1.0f, 1.0f, 1.0f, 1.0f));
        roughness = clamp(roughness * mr.g, 0.04f, 1.0f);
        metallic = saturate(metallic * mr.b);
    }

    if (materialData.hasOcclusionTexture != 0)
    {
        float ao = HikariSampleMaterialTexture(
            materialData.occlusionTextureDescriptorIndex,
            gLinearWrap,
            uv,
            float4(1.0f, 1.0f, 1.0f, 1.0f)).r;
        occlusion = lerp(1.0f, ao, saturate(materialData.pbrParams.z));
    }
}

float3 ApplyFog(float3 color, float3 worldPosWS)
{
    if (gFogParams.x < 0.5f)
    {
        return color;
    }

    float dist = length(gCameraPos.xyz - worldPosWS) * 0.8f;
    float fogRange = max(0.001f, gFogParams.z - gFogParams.y);
    float fogFactor = saturate((dist - gFogParams.y) / fogRange);
    fogFactor = saturate(fogFactor * max(0.0f, gFogColorDensity.a) * dist);
    return lerp(color, gFogColorDensity.rgb, fogFactor);
}

float CompareShadowDepth(float2 uv, float currentDepth)
{
    float shadowDepth = gShadowMap.SampleLevel(gShadowSampler, uv, 0).r;
    return currentDepth <= shadowDepth ? 1.0f : 0.0f;
}

float SampleShadowPcf(float2 uv, float currentDepth)
{
    float visibility = CompareShadowDepth(uv, currentDepth);
    if (gShadowPcfEnabled == 0)
    {
        return visibility;
    }

    float2 texelSize = float2(gShadowTexelSizeX, gShadowTexelSizeY) * gShadowPcfRadius;
    float sum = 0.0f;
    sum += CompareShadowDepth(uv + texelSize * float2(-1.0f, -1.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2( 0.0f, -1.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2( 1.0f, -1.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2(-1.0f,  0.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2( 0.0f,  0.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2( 1.0f,  0.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2(-1.0f,  1.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2( 0.0f,  1.0f), currentDepth);
    sum += CompareShadowDepth(uv + texelSize * float2( 1.0f,  1.0f), currentDepth);
    visibility = sum / 9.0f;
    return visibility;
}

float SampleDirectionalShadow(float3 worldPosWS, float3 geometricNormalWS, uint receiveShadow)
{
    if (gShadowEnabled == 0 || receiveShadow == 0)
    {
        return 1.0f;
    }

    float3 biasedWorldPos = worldPosWS + geometricNormalWS * gShadowNormalBias;
    float4 lightClip = mul(gShadowLightViewProj, float4(biasedWorldPos, 1.0f));
    if (abs(lightClip.w) < 1e-5f)
    {
        return 1.0f;
    }

    float3 proj = lightClip.xyz / lightClip.w;
    float2 uv = float2(proj.x * 0.5f + 0.5f, -proj.y * 0.5f + 0.5f);
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || proj.z < 0.0f || proj.z > 1.0f)
    {
        return 1.0f;
    }

    float currentDepth = proj.z - gShadowDepthBias;
    float visibility = SampleShadowPcf(uv, currentDepth);
    return lerp(1.0f - gShadowStrength, 1.0f, visibility);
}

float4 main(PSInput input) : SV_TARGET
{
    HikariMeshMaterialData materialData = HikariGetMeshMaterialData(input.materialDataIndex);
    float3 n = ResolveShadingNormal(materialData, input.normalWS, input.tangentWS, input.uv);
    float3 geometricNormal = normalize(input.normalWS);
    float3 l = normalize(-gDirectionalDir.xyz);
    float3 v = normalize(gCameraPos.xyz - input.worldPosWS);
#if !HIKARI_USE_COOK_TORRANCE_PBR
    float3 h = normalize(l + v);
#endif

    float ndotl = saturate(dot(n, l));

    float4 albedo = materialData.baseColor;
    if (materialData.hasBaseColorTexture != 0)
    {
        albedo *= HikariSampleMaterialTexture(
            materialData.baseColorTextureDescriptorIndex,
            gLinearWrap,
            input.uv,
            float4(1.0f, 1.0f, 1.0f, 1.0f));
    }
    if ((materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0 && albedo.a < materialData.pbrParams.w)
    {
        discard;
    }

    float metallic = 0.0f;
    float roughness = 1.0f;
    float occlusion = 1.0f;
    ResolvePbrInputs(materialData, input.uv, metallic, roughness, occlusion);
    float screenAo = 1.0f;
    if (gSsaoEnabled > 0.5f)
    {
        screenAo = gSsaoTex.Load(int3(int2(input.position.xy), 0)).r;
    }
    float shadowFactor = SampleDirectionalShadow(input.worldPosWS, geometricNormal, input.receiveShadow);
    float3 emissive = ((materialData.materialFlags & MATERIAL_EMISSIVE) != 0) ? ResolveEmissive(materialData, input.uv) : 0.0f.xxx;

    float3 shadedColor = albedo.rgb;
    if ((materialData.materialFlags & MATERIAL_UNLIT) == 0)
    {
#if HIKARI_USE_COOK_TORRANCE_PBR
        float3 direct =
            HikariEvaluateDirectPbr(
                albedo.rgb,
                metallic,
                roughness,
                n,
                v,
                l,
                gDirectionalColor.rgb,
                gDirectionalIntensity);
        direct *= shadowFactor;

        float3 pointDirect = AccumulatePointLightPbr(
            albedo.rgb,
            metallic,
            roughness,
            n,
            input.worldPosWS,
            v);

        float3 ambient = HikariEvaluateAmbientIbl(
            albedo.rgb,
            metallic,
            roughness,
            occlusion,
            screenAo,
            n,
            v,
            input.worldPosWS);

        shadedColor = direct + pointDirect + ambient;
#else
    float specPower = lerp(gSpecularParams.y, 8.0f, roughness);
    float spec = pow(saturate(dot(n, h)), max(1.0f, specPower));

    float3 ambient = gAmbientColor.rgb * gAmbientIntensity * occlusion;
    float3 diffuse = gDirectionalColor.rgb * (gDirectionalIntensity * ndotl) * (1.0f - metallic * 0.65f);
    float3 specular = gDirectionalColor.rgb * (gDirectionalIntensity * gSpecularParams.x * spec) * lerp(1.0f, 1.8f, metallic);
    float3 pointLightContribution = AccumulatePointLight(n, input.worldPosWS, v);
    float3 lit = ambient + (diffuse + specular) * shadowFactor + pointLightContribution;
        shadedColor = albedo.rgb * lit;
#endif
    }
    if (gDebugView == 1)
    {
        return float4(normalize(n) * 0.5f + 0.5f, albedo.a);
    }
    if (gDebugView == 2)
    {
        return float4(normalize(input.tangentWS.xyz) * 0.5f + 0.5f, albedo.a);
    }
    if (gDebugView == 3)
    {
        return float4(ApplyFog(shadedColor, input.worldPosWS), albedo.a);
    }
    if (gDebugView == 4)
    {
        return float4(albedo.rgb, albedo.a);
    }
    if (gDebugView == 5)
    {
        return float4(roughness.xxx, albedo.a);
    }
    if (gDebugView == 6)
    {
        return float4(metallic.xxx, albedo.a);
    }
    if (gDebugView == 7)
    {
        return float4(occlusion.xxx, albedo.a);
    }
    if (gDebugView == 8)
    {
        return float4(shadowFactor.xxx, albedo.a);
    }
    if (gDebugView == 9)
    {
        return float4(ndotl.xxx, albedo.a);
    }
    if (gDebugView == 10)
    {
        return float4(emissive, albedo.a);
    }
    if (gDebugView == 11)
    {
        float sceneDepth = gSceneDepthTex.Load(int3(int2(input.position.xy), 0)).r;
        return float4(sceneDepth.xxx, albedo.a);
    }
    if (gDebugView == 12)
    {
        float2 screenUv = input.position.xy * gScreenParams.zw;
        return float4(gSceneColorTex.Sample(gLinearWrap, saturate(screenUv)).rgb, albedo.a);
    }
    if (gDebugView == 32)
    {
        uint meshletKey =
            input.debugClusterId ^
            (input.debugSurfaceId * 1664525u) ^
            (input.debugLodIndex * 1013904223u);
        return float4(HikariDebugColorFromId(meshletKey), albedo.a);
    }
    if (gDebugView == 33)
    {
        return float4(HikariDebugColorFromId(input.debugClusterId), albedo.a);
    }
    if (gDebugView == 34)
    {
        return float4(HikariDebugColorFromId(input.debugSurfaceId), albedo.a);
    }
    if (gDebugView == 35)
    {
        return float4(HikariLodDebugColor(input.debugLodIndex), albedo.a);
    }
    if (gDebugView == 36)
    {
        return float4(HikariLodHeatColor(input.debugLodIndex), albedo.a);
    }
    if (gDebugView == 37)
    {
        float3 bucketColor = input.debugDrawBucket == 0u
            ? float3(0.28f, 0.92f, 0.48f)
            : float3(0.25f, 0.68f, 1.0f);
        return float4(bucketColor, albedo.a);
    }

    float3 finalColor = shadedColor + emissive;
    finalColor = ApplyFog(finalColor, input.worldPosWS);
    return float4(finalColor, albedo.a);
}
