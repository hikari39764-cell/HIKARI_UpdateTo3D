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
static const uint MATERIAL_SPECULAR_GLOSS_COMPAT = 1u << 4;

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
    float gForwardCostMode;
    float2 gDebugPadding;
};

static const uint HIKARI_FORWARD_COST_FULL = 0u;
static const uint HIKARI_FORWARD_COST_ALBEDO_ONLY = 1u;
static const uint HIKARI_FORWARD_COST_NO_NORMAL_MAP = 2u;
static const uint HIKARI_FORWARD_COST_NO_SHADOW = 3u;
static const uint HIKARI_FORWARD_COST_NO_SSAO = 4u;
static const uint HIKARI_FORWARD_COST_NO_MATERIAL_EXTRAS = 5u;

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
    float gShadowEdgeFade;
    float3 gShadowPadding;
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
#include "Include/HIKARI_DebugViewCommon.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD10;
    nointerpolation uint materialDataIndex : TEXCOORD2;
    nointerpolation uint receiveShadow : TEXCOORD3;
    // VS/MS 側の TEXCOORD スロットと一致させ、PSO リンク時の再割り当てを避ける。
    nointerpolation uint objectDataIndex : TEXCOORD4;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD5;
    nointerpolation uint debugClusterId : TEXCOORD6;
    nointerpolation uint debugSurfaceId : TEXCOORD7;
    nointerpolation uint debugLodIndex : TEXCOORD8;
    nointerpolation uint debugDrawBucket : TEXCOORD9;
};

float3 ResolveShadingNormal(
    HikariMeshMaterialData materialData,
    float3 normalWS,
    float4 tangentWS,
    float2 uv0,
    float2 uv1)
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

    const float2 normalUv =
        HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_NORMAL, uv0, uv1);
    float3 normalTS =
        HikariSampleMaterialTexture(
            materialData.normalTextureDescriptorIndex,
            gLinearWrap,
            normalUv,
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
    float3 specularColor,
    float specularFactor,
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
            specularColor,
            specularFactor,
            normalWS,
            viewDir,
            l,
            color,
            intensity * atten);
    }

    return sum;
}

float3 ResolveEmissive(HikariMeshMaterialData materialData, float2 uv0, float2 uv1)
{
    float3 emissive = materialData.emissiveFactor.rgb;
    if (materialData.hasEmissiveTexture != 0)
    {
        const float2 emissiveUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_EMISSIVE, uv0, uv1);
        emissive *= HikariSampleMaterialTexture(
            materialData.emissiveTextureDescriptorIndex,
            gLinearWrap,
            emissiveUv,
            float4(0.0f, 0.0f, 0.0f, 1.0f)).rgb;
    }
    return emissive * materialData.emissiveFactor.a;
}

void ResolveSpecularInputs(
    HikariMeshMaterialData materialData,
    float2 uv0,
    float2 uv1,
    out float3 specularColor,
    out float specularFactor)
{
    specularColor = max(0.0f.xxx, materialData.specularParams.rgb);
    specularFactor = max(0.0f, materialData.specularParams.w);

    if (materialData.hasSpecularTexture != 0)
    {
        const float2 specularUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_SPECULAR, uv0, uv1);
        specularFactor *= HikariSampleMaterialTexture(
            materialData.specularTextureDescriptorIndex,
            gLinearWrap,
            specularUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f)).a;
    }

    if (materialData.hasSpecularColorTexture != 0)
    {
        const float2 specularColorUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_SPECULAR_COLOR, uv0, uv1);
        specularColor *= HikariSampleMaterialTexture(
            materialData.specularColorTextureDescriptorIndex,
            gLinearWrap,
            specularColorUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f)).rgb;
    }
}

void ResolvePbrInputs(
    HikariMeshMaterialData materialData,
    float2 uv0,
    float2 uv1,
    out float metallic,
    out float roughness,
    out float occlusion)
{
    metallic = saturate(materialData.pbrParams.x);
    roughness = clamp(materialData.pbrParams.y, 0.04f, 1.0f);
    occlusion = 1.0f;

    if (materialData.hasMetallicRoughnessTexture != 0)
    {
        const float2 metallicRoughnessUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_METALLIC_ROUGHNESS, uv0, uv1);
        float4 mr = HikariSampleMaterialTexture(
            materialData.metallicRoughnessTextureDescriptorIndex,
            gLinearWrap,
            metallicRoughnessUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f));
        roughness = clamp(roughness * mr.g, 0.04f, 1.0f);
        metallic = saturate(metallic * mr.b);
    }

    if (materialData.hasOcclusionTexture != 0)
    {
        const float2 occlusionUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_OCCLUSION, uv0, uv1);
        float ao = HikariSampleMaterialTexture(
            materialData.occlusionTextureDescriptorIndex,
            gLinearWrap,
            occlusionUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f)).r;
        occlusion = lerp(1.0f, ao, saturate(materialData.pbrParams.z));
    }
}

bool HikariNeedsSpecularGlossCompatibility(HikariMeshMaterialData materialData, float metallic)
{
    if ((materialData.materialFlags & MATERIAL_SPECULAR_GLOSS_COMPAT) != 0)
    {
        return true;
    }

    return
        materialData.hasSpecularColorTexture != 0 &&
        materialData.hasMetallicRoughnessTexture == 0 &&
        metallic < 0.001f;
}

void ApplyPbrMaterialCompatibility(
    HikariMeshMaterialData materialData,
    float metallic,
    inout float roughness,
    inout float3 specularColor,
    inout float specularFactor)
{
    const bool alphaMasked = (materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0;
    if (alphaMasked)
    {
        roughness = max(roughness, 0.72f);
        specularFactor *= 0.75f;
    }

    if (!HikariNeedsSpecularGlossCompatibility(materialData, metallic))
    {
        return;
    }

    roughness = max(roughness, alphaMasked ? 0.86f : 0.72f);
    specularFactor *= alphaMasked ? 0.45f : 0.65f;
    specularColor *= alphaMasked ? 0.70f : 0.85f;
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

float HikariShadowReceiverFade(float2 uv)
{
    if (gShadowEdgeFade <= 0.00001f)
    {
        return 1.0f;
    }

    float edgeDistance = min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y));
    return saturate(edgeDistance / gShadowEdgeFade);
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
    float shadowFactor = lerp(1.0f - gShadowStrength, 1.0f, visibility);
    return lerp(1.0f, shadowFactor, HikariShadowReceiverFade(uv));
}

float4 main(PSInput input) : SV_TARGET
{
    HikariMeshMaterialData materialData = HikariGetMeshMaterialData(input.materialDataIndex);
    const uint forwardCostMode = (uint)(gForwardCostMode + 0.5f);
    const bool costAlbedoOnly = forwardCostMode == HIKARI_FORWARD_COST_ALBEDO_ONLY;
    const bool costNoNormalMap = forwardCostMode == HIKARI_FORWARD_COST_NO_NORMAL_MAP;
    const bool costNoShadow = forwardCostMode == HIKARI_FORWARD_COST_NO_SHADOW;
    const bool costNoSsao = forwardCostMode == HIKARI_FORWARD_COST_NO_SSAO;
    const bool costNoMaterialExtras =
        forwardCostMode == HIKARI_FORWARD_COST_NO_MATERIAL_EXTRAS;

    float4 albedo = materialData.baseColor;
    if (materialData.hasBaseColorTexture != 0)
    {
        const float2 baseColorUv =
            HikariResolveMaterialUv(materialData, HIKARI_MATERIAL_UV_BASE_COLOR, input.uv, input.uv1);
        albedo *= HikariSampleMaterialTexture(
            materialData.baseColorTextureDescriptorIndex,
            gLinearWrap,
            baseColorUv,
            float4(1.0f, 1.0f, 1.0f, 1.0f));
    }
    if ((materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0 && albedo.a < materialData.pbrParams.w)
    {
        discard;
    }

    if (gDebugView == 4 || costAlbedoOnly)
    {
        return float4(albedo.rgb, albedo.a);
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

    float4 geometryDebugColor;
    if (HikariTryResolveGeometryDebugView(
        gDebugView,
        input.debugClusterId,
        input.debugSurfaceId,
        input.debugLodIndex,
        input.debugDrawBucket,
        albedo.a,
        geometryDebugColor))
    {
        return geometryDebugColor;
    }

    float3 geometricNormal = normalize(input.normalWS);
    float3 n = costNoNormalMap
        ? geometricNormal
        : ResolveShadingNormal(materialData, input.normalWS, input.tangentWS, input.uv, input.uv1);
    if (gDebugView == 1)
    {
        return float4(normalize(n) * 0.5f + 0.5f, albedo.a);
    }
    if (gDebugView == 2)
    {
        return float4(normalize(input.tangentWS.xyz) * 0.5f + 0.5f, albedo.a);
    }

    float3 l = normalize(-gDirectionalDir.xyz);
    float3 v = normalize(gCameraPos.xyz - input.worldPosWS);
#if !HIKARI_USE_COOK_TORRANCE_PBR
    float3 h = normalize(l + v);
#endif

    float ndotl = saturate(dot(n, l));
    if (gDebugView == 9)
    {
        return float4(ndotl.xxx, albedo.a);
    }

    float metallic = 0.0f;
    float roughness = 1.0f;
    float occlusion = 1.0f;
    float3 specularColor = 1.0f.xxx;
    float specularFactor = 1.0f;
    if (costNoMaterialExtras)
    {
        metallic = saturate(materialData.pbrParams.x);
        roughness = clamp(materialData.pbrParams.y, 0.04f, 1.0f);
        occlusion = 1.0f;
        specularColor = max(0.0f.xxx, materialData.specularParams.rgb);
        specularFactor = max(0.0f, materialData.specularParams.w);
    }
    else
    {
        ResolvePbrInputs(materialData, input.uv, input.uv1, metallic, roughness, occlusion);
        ResolveSpecularInputs(materialData, input.uv, input.uv1, specularColor, specularFactor);
        ApplyPbrMaterialCompatibility(materialData, metallic, roughness, specularColor, specularFactor);
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

    float screenAo = 1.0f;
    if (!costNoSsao && gSsaoEnabled > 0.5f)
    {
        screenAo = gSsaoTex.Load(int3(int2(input.position.xy), 0)).r;
    }
    float shadowFactor = costNoShadow
        ? 1.0f
        : SampleDirectionalShadow(input.worldPosWS, geometricNormal, input.receiveShadow);
    if (gDebugView == 8)
    {
        return float4(shadowFactor.xxx, albedo.a);
    }

    float3 emissive = (!costNoMaterialExtras && (materialData.materialFlags & MATERIAL_EMISSIVE) != 0)
        ? ResolveEmissive(materialData, input.uv, input.uv1)
        : 0.0f.xxx;
    if (gDebugView == 10)
    {
        return float4(emissive, albedo.a);
    }

    float3 shadedColor = albedo.rgb;
    if ((materialData.materialFlags & MATERIAL_UNLIT) == 0)
    {
#if HIKARI_USE_COOK_TORRANCE_PBR
        float3 direct =
            HikariEvaluateDirectPbr(
                albedo.rgb,
                metallic,
                roughness,
                specularColor,
                specularFactor,
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
            specularColor,
            specularFactor,
            n,
            input.worldPosWS,
            v);

        float3 ambient = HikariEvaluateAmbientIbl(
            albedo.rgb,
            metallic,
            roughness,
            specularColor,
            specularFactor,
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
    if (gDebugView == 3)
    {
        return float4(ApplyFog(shadedColor, input.worldPosWS), albedo.a);
    }

    float3 finalColor = shadedColor + emissive;
    finalColor = ApplyFog(finalColor, input.worldPosWS);
    return float4(finalColor, albedo.a);
}
