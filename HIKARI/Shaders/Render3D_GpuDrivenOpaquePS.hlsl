// GPU-driven opaque entry point for meshlet/cluster mainline.
// Default must preserve the full StaticPS quality path. The lightweight body
// below is diagnostic-only and should not become the production default.
#ifndef HIKARI_GPU_DRIVEN_OPAQUE_LEAN_DIAGNOSTIC
#define HIKARI_FORCE_SURFACE_GPU_SCENE_PIXEL 1
#include "Render3D_StaticFxPS.hlsl"
#else

// Lightweight GPU-driven opaque path for PS bottleneck isolation.
// Keep register layout compatible with Render3D_StaticPS.hlsl.

cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    float4 gTimeParams;
    float4 gScreenParams;
};

#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshMaterialData.hlsli"

static const uint MATERIAL_UNLIT = 1u << 0;
static const uint MATERIAL_ALPHA_MASK = 1u << 1;
static const uint MATERIAL_EMISSIVE = 1u << 2;
static const uint MATERIAL_SPECULAR_GLOSS_COMPAT = 1u << 4;

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
#define gSsaoEnabled gAoParams.x
#define gSsaoDiffuseStrength gAoParams.y
#define gSsaoSpecularStrength gAoParams.z

Texture2D gShadowMap : register(t2);
Texture2D gSsaoTex : register(t13);
SamplerState gLinearWrap : register(s0);
SamplerState gShadowSampler : register(s1);

#include "Include/HIKARI_PbrCommon.hlsli"

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

bool NeedsSpecularGlossCompatibility(HikariMeshMaterialData materialData, float metallic)
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

    if (!NeedsSpecularGlossCompatibility(materialData, metallic))
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
    return sum / 9.0f;
}

float ShadowReceiverFade(float2 uv)
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
    if (uv.x < 0.0f ||
        uv.x > 1.0f ||
        uv.y < 0.0f ||
        uv.y > 1.0f ||
        proj.z < 0.0f ||
        proj.z > 1.0f)
    {
        return 1.0f;
    }

    float currentDepth = proj.z - gShadowDepthBias;
    float visibility = SampleShadowPcf(uv, currentDepth);
    float shadowFactor = lerp(1.0f - gShadowStrength, 1.0f, visibility);
    return lerp(1.0f, shadowFactor, ShadowReceiverFade(uv));
}

float3 EvaluateLeanAmbient(
    float3 baseColor,
    float metallic,
    float roughness,
    float3 specularColor,
    float specularFactor,
    float occlusion,
    float screenAo,
    float3 n,
    float3 v)
{
    float3 F0 = HikariSpecularF0(baseColor, metallic, specularColor, specularFactor);
    float3 F = HikariFresnelSchlick(saturate(dot(n, v)), F0);
    float3 kD = (1.0f.xxx - F) * (1.0f - metallic);

    float3 diffuseAmbient = gAmbientColor.rgb * gAmbientIntensity;
    if (gSkyAmbientFromSky > 0.0001f)
    {
        float3 skyDiffuse =
            lerp(gSkyGroundColor, gSkyHorizonColor, saturate(n.y * 0.5f + 0.5f));
        skyDiffuse *= gSkyExposure * gSkyAmbientFromSky;
        diffuseAmbient = lerp(diffuseAmbient, skyDiffuse, saturate(gSkyAmbientFromSky));
    }

    float materialAo = saturate(occlusion);
    float ssao = saturate(screenAo);
    float diffuseAo = materialAo * lerp(1.0f, ssao, saturate(gSsaoDiffuseStrength));
    float specularAo =
        lerp(
            1.0f,
            materialAo * ssao,
            saturate(roughness * roughness) * saturate(gSsaoSpecularStrength));

    float3 diffuse = kD * baseColor * diffuseAmbient * diffuseAo;
    float roughnessFade = 1.0f - saturate(roughness * 0.85f);
    float3 specular =
        F *
        max(0.0f.xxx, gSkyHorizonColor) *
        max(0.0f, gSkyReflectionIntensity) *
        roughnessFade *
        specularAo;
    return diffuse + specular;
}

float4 main(PSInput input) : SV_TARGET
{
    HikariMeshMaterialData materialData = HikariGetMeshMaterialData(input.materialDataIndex);

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

    if ((materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0 &&
        albedo.a < materialData.pbrParams.w)
    {
        discard;
    }

    float3 n = ResolveShadingNormal(materialData, input.normalWS, input.tangentWS, input.uv, input.uv1);
    float3 geometricNormal = normalize(input.normalWS);
    float3 l = normalize(-gDirectionalDir.xyz);
    float3 v = normalize(gCameraPos.xyz - input.worldPosWS);
    float ndotl = saturate(dot(n, l));

    float metallic = 0.0f;
    float roughness = 1.0f;
    float occlusion = 1.0f;
    float3 specularColor = 1.0f.xxx;
    float specularFactor = 1.0f;
    ResolvePbrInputs(materialData, input.uv, input.uv1, metallic, roughness, occlusion);
    ResolveSpecularInputs(materialData, input.uv, input.uv1, specularColor, specularFactor);
    ApplyPbrMaterialCompatibility(materialData, metallic, roughness, specularColor, specularFactor);

    float screenAo = 1.0f;
    if (gSsaoEnabled > 0.5f)
    {
        screenAo = gSsaoTex.Load(int3(int2(input.position.xy), 0)).r;
    }

    float shadowFactor =
        SampleDirectionalShadow(input.worldPosWS, geometricNormal, input.receiveShadow);
    float3 emissive =
        ((materialData.materialFlags & MATERIAL_EMISSIVE) != 0)
            ? ResolveEmissive(materialData, input.uv, input.uv1)
            : 0.0f.xxx;

    float3 finalColor = albedo.rgb;
    if ((materialData.materialFlags & MATERIAL_UNLIT) == 0)
    {
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

        float3 ambient =
            EvaluateLeanAmbient(
                albedo.rgb,
                metallic,
                roughness,
                specularColor,
                specularFactor,
                occlusion,
                screenAo,
                n,
                v);
        finalColor = direct + ambient;
    }

    if (gDebugView == 1)
    {
        return float4(normalize(n) * 0.5f + 0.5f, albedo.a);
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

    finalColor += emissive;
    finalColor = ApplyFog(finalColor, input.worldPosWS);
    return float4(finalColor, albedo.a);
}

#endif
