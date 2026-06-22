// IMPORTANT:
// This cbuffer/register layout must stay in sync with Render3D_StaticPS.hlsl
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

#undef gFxFlags
#define gFxFlags pixelObjectData.fxFlags
#undef gFxUser
#define gFxUser pixelObjectData.fxUser

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
#include "Include/HIKARI_DebugViewCommon.hlsli"

struct PSInput
{
    float4 position   : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS   : NORMAL;
    float4 tangentWS  : TANGENT;
    float2 uv         : TEXCOORD0;
    float2 uv1        : TEXCOORD10;
    nointerpolation uint materialDataIndex : TEXCOORD2;
    nointerpolation uint receiveShadow : TEXCOORD3;
    nointerpolation uint objectDataIndex : TEXCOORD4;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD5;
    // Keep TEXCOORD slots aligned with ClusterVS/MeshletMS for PSO linkage.
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

float Hash31(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float Noise3D(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);

    float n000 = Hash31(i + float3(0,0,0));
    float n100 = Hash31(i + float3(1,0,0));
    float n010 = Hash31(i + float3(0,1,0));
    float n110 = Hash31(i + float3(1,1,0));
    float n001 = Hash31(i + float3(0,0,1));
    float n101 = Hash31(i + float3(1,0,1));
    float n011 = Hash31(i + float3(0,1,1));
    float n111 = Hash31(i + float3(1,1,1));

    float3 u = f * f * (3.0 - 2.0 * f);

    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);

    float nxy0 = lerp(nx00, nx10, u.y);
    float nxy1 = lerp(nx01, nx11, u.y);

    return lerp(nxy0, nxy1, u.z);
}

float Hash21(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float2 RotateSceneScanUv(float2 uv, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float2(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
}

float2 ResolveSceneScanUv(float3 worldPosWS, float3 normalWS)
{
    float3 n = abs(normalWS);
    if (n.y >= n.x && n.y >= n.z)
    {
        return worldPosWS.xz;
    }
    if (n.x >= n.z)
    {
        return worldPosWS.zy;
    }
    return worldPosWS.xy;
}

void SceneScanTriangleFacet(
    float2 uv,
    float lineWidth,
    out float edgeMask,
    out float fillMask,
    out float triSeed,
    out float2 triCenterUv)
{
    const float kTriHeight = 0.8660254f;
    float2 p = float2(uv.x + uv.y * 0.5f, uv.y * kTriHeight);
    float2 cell = floor(p);
    float2 f = frac(p);
    float upper = step(1.0f, f.x + f.y);

    float3 lowerBary = float3(f.x, f.y, 1.0f - f.x - f.y);
    float3 upperBary = float3(1.0f - f.x, 1.0f - f.y, f.x + f.y - 1.0f);
    float3 bary = max(lerp(lowerBary, upperBary, upper), 0.0f.xxx);
    float edgeDistance = min(min(bary.x, bary.y), bary.z);

    float aa = max(fwidth(edgeDistance), 0.001f);
    float w = clamp(lineWidth, 0.002f, 0.24f);
    edgeMask = 1.0f - smoothstep(w, w + aa, edgeDistance);
    fillMask = smoothstep(w * 0.8f, w * 1.85f + aa, edgeDistance);

    float2 centerP = cell + lerp(float2(0.3333333f, 0.3333333f), float2(0.6666667f, 0.6666667f), upper);
    triCenterUv = float2(centerP.x - (centerP.y / kTriHeight) * 0.5f, centerP.y / kTriHeight);

    triSeed = Hash21(cell + float2(upper * 17.0f, upper * 31.0f));
}

float3 AccumulatePointLight(float3 normalWS, float3 worldPosWS, float3 viewDir)
{
    float3 sum = 0.0f.xxx;
    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        if (i >= gPointLightCount) break;

        float3 lightPos = gPointLightPosRange[i].xyz;
        float range = max(gPointLightPosRange[i].w, 0.001f);

        float3 toLight = lightPos - worldPosWS;
        float dist = length(toLight);
        float3 l = (dist > 1e-5f) ? toLight / dist : float3(0,1,0);

        float atten = saturate(1.0 - dist / range);
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
        float3 l = (dist > 1e-5f) ? toLight / dist : float3(0, 1, 0);

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

float3 ApplyFog(float3 color, float3 worldPosWS)
{
    if (gFogParams.x < 0.5f)
    {
        return color;
    }

    float dist = length(gCameraPos.xyz - worldPosWS);
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

bool HikariShouldApplyStaticFx(HikariMeshObjectData pixelObjectData, uint surfaceGpuSceneIndex)
{
#if HIKARI_SURFACE_GPU_SCENE_CONSUME
#if defined(HIKARI_FORCE_SURFACE_GPU_SCENE_PIXEL) && HIKARI_FORCE_SURFACE_GPU_SCENE_PIXEL
    HikariSurfaceGpuSceneInstance surfaceInstance =
        HikariGetSurfaceGpuSceneInstanceAt(surfaceGpuSceneIndex);
    return (surfaceInstance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_MATERIAL_FX) != 0u;
#else
    if (gUseSurfaceGpuScene != 0u)
    {
        HikariSurfaceGpuSceneInstance surfaceInstance =
            HikariGetSurfaceGpuSceneInstanceAt(surfaceGpuSceneIndex);
        return (surfaceInstance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_MATERIAL_FX) != 0u;
    }
#endif
#endif

    if (pixelObjectData.fxFlags != 0u)
    {
        return true;
    }

    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        if (any(abs(pixelObjectData.fxUser[i]) > 0.000001f))
        {
            return true;
        }
    }
    return false;
}

float4 main(PSInput input) : SV_TARGET
{
    HikariMeshObjectData pixelObjectData =
        HikariGetMeshObjectDataForPixel(input.objectDataIndex, input.surfaceGpuSceneIndex);
    HikariMeshMaterialData materialData = HikariGetMeshMaterialData(input.materialDataIndex);
    float3 n = ResolveShadingNormal(materialData, input.normalWS, input.tangentWS, input.uv, input.uv1);
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

    float metallic = 0.0f;
    float roughness = 1.0f;
    float occlusion = 1.0f;
    float3 specularColor = 1.0f.xxx;
    float specularFactor = 1.0f;
    float shadowFactor = 1.0f;
    float3 emissive = ((materialData.materialFlags & MATERIAL_EMISSIVE) != 0)
        ? ResolveEmissive(materialData, input.uv, input.uv1)
        : 0.0f.xxx;
    float3 lit = albedo.rgb;
    if ((materialData.materialFlags & MATERIAL_UNLIT) == 0)
    {
        ResolvePbrInputs(materialData, input.uv, input.uv1, metallic, roughness, occlusion);
        float screenAo = 1.0f;
        if (gSsaoEnabled > 0.5f)
        {
            screenAo = gSsaoTex.Load(int3(int2(input.position.xy), 0)).r;
        }
        ResolveSpecularInputs(materialData, input.uv, input.uv1, specularColor, specularFactor);
#if HIKARI_USE_COOK_TORRANCE_PBR
        shadowFactor = SampleDirectionalShadow(input.worldPosWS, geometricNormal, input.receiveShadow);

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

        lit = direct + pointDirect + ambient;
#else
        float specPower = lerp(gSpecularParams.y, 8.0f, roughness);
        float spec = pow(saturate(dot(n, h)), max(1.0f, specPower));

        float3 ambient = gAmbientColor.rgb * gAmbientIntensity * occlusion;
        float3 diffuse = gDirectionalColor.rgb * (gDirectionalIntensity * ndotl) * (1.0f - metallic * 0.65f);
        float3 specular = gDirectionalColor.rgb * (gDirectionalIntensity * gSpecularParams.x * spec) * lerp(1.0f, 1.8f, metallic);
        float3 pointLightContribution = AccumulatePointLight(n, input.worldPosWS, v);
        shadowFactor = SampleDirectionalShadow(input.worldPosWS, geometricNormal, input.receiveShadow);
        lit = albedo.rgb * (ambient + (diffuse + specular) * shadowFactor + pointLightContribution);
#endif
    }
    if ((materialData.materialFlags & MATERIAL_EMISSIVE) != 0)
    {
        lit += emissive;
    }
    lit = ApplyFog(lit, input.worldPosWS);
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
        return float4(lit, albedo.a);
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

    if (!HikariShouldApplyStaticFx(pixelObjectData, input.surfaceGpuSceneIndex))
    {
        return float4(lit, albedo.a);
    }

    float rimStrength = gFxUser0.x;
    float rimPower    = max(gFxUser0.y, 0.01);
    float dissolveAmt = saturate(gFxUser0.z);
    float edgeWidth   = max(gFxUser0.w, 0.0001);

    float pulseSpeed  = gFxUser1.x;
    float edgeBoost   = gFxUser1.y;
    float noiseScale  = max(gFxUser1.z, 0.0001);

    float pulse = 0.5 + 0.5 * sin(pulseSpeed);
    float rim = pow(1.0 - saturate(dot(n, v)), rimPower);
    float3 rimColor = float3(0.15, 0.75, 1.0) * rim * rimStrength * (0.75 + pulse * 0.25);

    float noise = Noise3D(input.worldPosWS * noiseScale + float3(0.0, 0.0, 0.0));
    float cutoff = dissolveAmt;
    float edge = smoothstep(cutoff, cutoff + edgeWidth, noise);
    float edgeBand = smoothstep(cutoff - edgeWidth, cutoff, noise) - smoothstep(cutoff, cutoff + edgeWidth, noise);

    if (noise < cutoff)
    {
        discard;
    }

    float3 edgeColor = float3(0.4, 0.5, 1.0) * edgeBand * edgeBoost;
    float3 finalColor = lit * edge + rimColor + edgeColor;

    float sceneColorDistortionStrength = gFxUser2.x;
    float sceneColorMix = saturate(gFxUser2.y);
    float sceneColorNoiseScale = max(gFxUser2.z, 0.0001f);

    if (sceneColorMix > 0.0001f)
    {
        // SceneColor is a snapshot captured before the DepthAware phase.
        // Do not sample the currently bound render target directly.
        float2 screenUv = input.position.xy * gScreenParams.zw;
        float n0 = Noise3D(input.worldPosWS * sceneColorNoiseScale);
        float n1 = Noise3D(input.worldPosWS * sceneColorNoiseScale + float3(13.1f, 7.7f, 3.3f));
        float2 distortion = (float2(n0, n1) * 2.0f - 1.0f) * sceneColorDistortionStrength;
        float3 sceneColor = gSceneColorTex.Sample(gLinearWrap, saturate(screenUv + distortion)).rgb;
        finalColor = lerp(finalColor, sceneColor, sceneColorMix);
    }

    float3 scanOrigin = gFxUser3.xyz;
    float scanTime = gFxUser3.w;
    float scanRadius = max(gFxUser4.x, 0.0f);
    float scanBandWidth = max(gFxUser4.y, 0.001f);
    float scanSpeed = max(gFxUser4.z, 0.001f);
    float scanIntensity = max(gFxUser4.w, 0.0f);
    float4 scanColor = gFxUser5;
    float triangleCellSize = max(gFxUser6.x, 0.001f);
    float triangleLineWidth = clamp(gFxUser6.y, 0.001f, 0.45f);
    float triangleNoiseScale = max(gFxUser6.z, 0.001f);
    float triangleFlicker = saturate(gFxUser6.w);
    float scanAfterglowStrength = max(gFxUser7.x, 0.0f);
    float scanFrontLineStrength = max(gFxUser7.y, 0.0f);
    float scanGeometryEdgeStrength = max(gFxUser7.z, 0.0f);
    float scanDistortionStrength = max(gFxUser7.w, 0.0f);

    if (scanIntensity > 0.0001f && scanRadius > 0.0001f)
    {
        float2 surfacePlane = ResolveSceneScanUv(input.worldPosWS, geometricNormal);
        float2 originPlane = ResolveSceneScanUv(scanOrigin, geometricNormal);
        float distOnSurface = length(surfacePlane - originPlane);
        float waveRadius = scanTime * scanSpeed;
        float bandDistance = abs(distOnSurface - waveRadius);
        float waveBand = 1.0f - smoothstep(scanBandWidth, scanBandWidth * 1.35f, bandDistance);
        float radiusMask = 1.0f - smoothstep(scanRadius, scanRadius + scanBandWidth, distOnSurface);
        float waveAlive = 1.0f - smoothstep(
            scanRadius + scanBandWidth * (0.5f + scanAfterglowStrength * 1.8f),
            scanRadius + scanBandWidth * (1.0f + scanAfterglowStrength * 2.4f),
            waveRadius);

        float2 triangleDrift = float2(scanTime * 0.055f, -scanTime * 0.032f);
        float2 randomTile = floor(surfacePlane / max(triangleCellSize * 3.25f, 0.001f));
        float randomA = Hash21(randomTile + float2(19.17f, 7.31f));
        float randomB = Hash21(randomTile + float2(3.91f, 23.53f));
        float randomAngle = (randomA - 0.5f) * 0.68f;
        float warpNoiseA = Noise3D(float3(surfacePlane * 0.23f, scanTime * 0.16f));
        float warpNoiseB = Noise3D(float3(surfacePlane * 0.23f + float2(11.3f, 5.7f), scanTime * 0.16f + 4.1f));
        float2 warpedPlane = originPlane + RotateSceneScanUv(surfacePlane - originPlane, randomAngle);
        warpedPlane += (float2(randomA, randomB) - 0.5f) * triangleCellSize * 0.52f;
        warpedPlane += (float2(warpNoiseA, warpNoiseB) - 0.5f) * triangleCellSize * 0.34f;
        float2 scanUv = warpedPlane / triangleCellSize + triangleDrift;
        float triangleEdge = 0.0f;
        float triangleFill = 0.0f;
        float triangleSeed = 0.0f;
        float2 triangleCenterUv = 0.0f.xx;
        SceneScanTriangleFacet(scanUv, triangleLineWidth, triangleEdge, triangleFill, triangleSeed, triangleCenterUv);

        float2 triangleCenterPlane = (triangleCenterUv - triangleDrift) * triangleCellSize;
        float triangleDistance = length(triangleCenterPlane - originPlane);
        float triangleAge = (waveRadius - triangleDistance) / scanBandWidth;
        float stagger = (triangleSeed - 0.5f) * 0.7f;
        float detailNoise = Noise3D(float3(scanUv * triangleNoiseScale, scanTime * 1.7f));
        float triangleEnter = smoothstep(-0.32f, 0.24f, triangleAge + stagger + detailNoise * 0.12f);
        float triangleLeave = 1.0f - smoothstep(1.25f, 2.35f, triangleAge + stagger * 0.35f);
        float triangleSweep = triangleEnter * triangleLeave;
        float hotFront = 1.0f - smoothstep(scanBandWidth * 0.08f, scanBandWidth * 0.48f, abs(triangleDistance - waveRadius + stagger * scanBandWidth * 0.35f));
        float frontLine = 1.0f - smoothstep(scanBandWidth * 0.015f, scanBandWidth * 0.13f, abs(triangleDistance - waveRadius + stagger * scanBandWidth * 0.18f));
        float shardBrightness = lerp(0.72f, 1.28f, triangleSeed);
        float flicker = lerp(1.0f, 0.68f + 0.32f * sin(scanTime * 24.0f + triangleSeed * 28.0f), triangleFlicker);

        float smallTriangleCellSize = max(triangleCellSize * 0.48f, 0.001f);
        float2 smallDrift = float2(-scanTime * 0.083f, scanTime * 0.061f);
        float2 smallRandomTile = floor(surfacePlane / max(smallTriangleCellSize * 4.0f, 0.001f));
        float smallRandomA = Hash21(smallRandomTile + float2(41.9f, 5.2f));
        float smallRandomB = Hash21(smallRandomTile + float2(9.6f, 37.4f));
        float2 smallWarpedPlane = surfacePlane;
        smallWarpedPlane += (float2(smallRandomA, smallRandomB) - 0.5f) * smallTriangleCellSize * 0.75f;
        smallWarpedPlane += (float2(detailNoise, warpNoiseB) - 0.5f) * smallTriangleCellSize * 0.36f;
        float2 smallScanUv = smallWarpedPlane / smallTriangleCellSize + smallDrift;
        float smallTriangleEdge = 0.0f;
        float smallTriangleFill = 0.0f;
        float smallTriangleSeed = 0.0f;
        float2 smallTriangleCenterUv = 0.0f.xx;
        SceneScanTriangleFacet(
            smallScanUv,
            clamp(triangleLineWidth * 0.78f, 0.001f, 0.45f),
            smallTriangleEdge,
            smallTriangleFill,
            smallTriangleSeed,
            smallTriangleCenterUv);

        float2 smallTriangleCenterPlane = (smallTriangleCenterUv - smallDrift) * smallTriangleCellSize;
        float smallTriangleDistance = length(smallTriangleCenterPlane - originPlane);
        float smallTriangleAge = (waveRadius - smallTriangleDistance) / scanBandWidth;
        float smallStagger = (smallTriangleSeed - 0.5f) * 1.15f;
        float smallDetailNoise = Noise3D(float3(smallScanUv * triangleNoiseScale * 1.65f, scanTime * 2.35f));
        float smallPresence = smoothstep(0.20f, 0.92f, smallTriangleSeed + smallDetailNoise * 0.26f);
        float smallTriangleEnter = smoothstep(-0.42f, 0.20f, smallTriangleAge + smallStagger + smallDetailNoise * 0.18f);
        float smallTriangleLeave = 1.0f - smoothstep(0.95f, 2.8f, smallTriangleAge + smallStagger * 0.25f);
        float smallTriangleSweep = smallTriangleEnter * smallTriangleLeave * smallPresence;
        float smallHotFront = 1.0f - smoothstep(scanBandWidth * 0.06f, scanBandWidth * 0.42f, abs(smallTriangleDistance - waveRadius + smallStagger * scanBandWidth * 0.22f));

        float trailAge = triangleAge + stagger * 0.22f;
        float afterglow = smoothstep(0.45f, 1.25f, trailAge) * (1.0f - smoothstep(2.35f, 5.5f, trailAge));
        afterglow *= scanAfterglowStrength;

        float3 edgeNormal = normalize(geometricNormal);
        float normalEdge = length(ddx(edgeNormal)) + length(ddy(edgeNormal));
        float silhouetteEdge = pow(1.0f - saturate(dot(edgeNormal, v)), 3.5f);
        float geometryEdge = saturate(normalEdge * 3.4f + silhouetteEdge * 0.55f) * scanGeometryEdgeStrength;

        float fillLayer = triangleFill * triangleSweep * waveBand * shardBrightness;
        float afterglowLayer = triangleFill * afterglow * shardBrightness * (0.72f + detailNoise * 0.28f);
        float smallFillLayer = smallTriangleFill * smallTriangleSweep * waveBand * (0.44f + smallTriangleSeed * 0.44f);
        float smallEdgeLayer = smallTriangleEdge * smallHotFront * smallPresence * 0.72f;
        float edgeLayer = triangleEdge * saturate(hotFront * 1.45f + triangleSweep * 0.65f + afterglowLayer * 0.35f);
        float frontLineLayer = frontLine * scanFrontLineStrength * (0.28f + triangleFill * 0.42f + triangleEdge * 1.15f);
        float geometryEdgeLayer = geometryEdge * saturate(hotFront * 0.75f + triangleSweep * 0.55f + afterglowLayer * 0.28f);
        float frontWash = hotFront * (0.18f + triangleFill * 0.42f);
        float scanMask = saturate(
            fillLayer * 0.85f +
            afterglowLayer * 0.65f +
            smallFillLayer * 0.55f +
            smallEdgeLayer * 0.85f +
            edgeLayer * 1.25f +
            frontLineLayer * 1.55f +
            geometryEdgeLayer * 0.95f +
            frontWash) * radiusMask * waveAlive * scanColor.a * flicker;

        if (scanDistortionStrength > 0.00001f && scanMask > 0.0001f)
        {
            float2 screenUv = input.position.xy * gScreenParams.zw;
            float2 distortion = normalize(float2(detailNoise - 0.5f, triangleSeed - 0.5f) + 0.0001f.xx);
            distortion *= scanDistortionStrength * saturate(frontLine + hotFront * 0.5f + afterglowLayer * 0.25f);
            float3 distortedSceneColor = gSceneColorTex.Sample(gLinearWrap, saturate(screenUv + distortion)).rgb;
            finalColor = lerp(finalColor, distortedSceneColor, saturate(scanMask * 0.12f));
        }

        float overlayBoost = saturate((fillLayer * 0.45f + afterglowLayer * 0.38f + smallFillLayer * 0.32f + smallEdgeLayer * 0.42f + edgeLayer * 0.8f + frontLineLayer * 0.95f + geometryEdgeLayer * 0.55f) * scanIntensity * 0.2f);
        float3 frontColor = lerp(scanColor.rgb, 1.0f.xxx, saturate(frontLine * 0.8f));
        float3 overlayColor = lerp(scanColor.rgb, frontColor, saturate(frontLineLayer)) * (0.72f + hotFront * 1.1f + smallEdgeLayer * 0.34f + edgeLayer * 0.42f + geometryEdgeLayer * 0.38f);
        finalColor = finalColor * (1.0f + scanColor.rgb * overlayBoost);
        finalColor += overlayColor * scanMask * scanIntensity;
    }

    return float4(finalColor, albedo.a);
}
