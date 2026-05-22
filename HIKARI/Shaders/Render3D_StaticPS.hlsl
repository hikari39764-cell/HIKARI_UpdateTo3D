// IMPORTANT:
// This cbuffer/register layout must stay in sync with Render3D_StaticFxPS.hlsl
// and MeshRenderer::ObjectCB / LightCB / ShadowCB / SkyEnvironmentCB.

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
    float4 gCameraPos;
    float4 gTimeParams;
    float4 gScreenParams;
};

cbuffer ObjectCB : register(b1)
{
    float4x4 gWorld;
    float4x4 gNormalMatrix;
    float4 gBaseColor;
    uint gHasBaseColorTexture;
    uint gFxFlags;
    uint gMaterialFlags;
    float gAlphaCutoff;
    float4 gEmissiveFactor;
    uint gHasNormalTexture;
    float gNormalScale;
    float2 gNormalPadding;
    uint gReceiveShadow;
    float3 gShadowObjectPadding;
    uint gHasEmissiveTexture;
    float3 gEmissivePadding;
    float gMetallicFactor;
    float gRoughnessFactor;
    uint gHasMetallicRoughnessTexture;
    uint gHasOcclusionTexture;
    float gOcclusionStrength;
    float3 gPbrPadding;
    float4 gFxUser[8];
};

static const uint MATERIAL_UNLIT = 1u << 0;
static const uint MATERIAL_ALPHA_MASK = 1u << 1;
static const uint MATERIAL_EMISSIVE = 1u << 2;

#ifndef HIKARI_USE_COOK_TORRANCE_PBR
#define HIKARI_USE_COOK_TORRANCE_PBR 1
#endif

static const float PI = 3.14159265359f;

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

Texture2D gBaseColorTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gShadowMap : register(t2);
Texture2D gEmissiveTex : register(t3);
Texture2D gMetallicRoughnessTex : register(t4);
Texture2D gOcclusionTex : register(t5);
TextureCube gSkyCube : register(t6);
Texture2D gSceneDepthTex : register(t7);
Texture2D gSceneColorTex : register(t8);
SamplerState gLinearWrap : register(s0);
SamplerState gShadowSampler : register(s1);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
};

float3 ResolveShadingNormal(float3 normalWS, float4 tangentWS, float2 uv)
{
    float3 n = normalize(normalWS);
    if (gHasNormalTexture == 0)
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

    float3 normalTS = gNormalTex.Sample(gLinearWrap, uv).xyz * 2.0f - 1.0f;
    normalTS.xy *= gNormalScale;
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

float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f.xxx - F0) * Pow5(1.0f - saturate(cosTheta));
}

float DistributionGGX(float3 n, float3 h, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float ndoth = saturate(dot(n, h));
    float ndoth2 = ndoth * ndoth;

    float denom = ndoth2 * (a2 - 1.0f) + 1.0f;
    denom = PI * denom * denom;

    return a2 / max(denom, 0.00001f);
}

float GeometrySchlickGGX(float ndotv, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;

    return ndotv / max(ndotv * (1.0f - k) + k, 0.00001f);
}

float GeometrySmith(float3 n, float3 v, float3 l, float roughness)
{
    float ndotv = saturate(dot(n, v));
    float ndotl = saturate(dot(n, l));
    float ggxV = GeometrySchlickGGX(ndotv, roughness);
    float ggxL = GeometrySchlickGGX(ndotl, roughness);
    return ggxV * ggxL;
}

float3 EvaluateDirectPbr(
    float3 baseColor,
    float metallic,
    float roughness,
    float3 n,
    float3 v,
    float3 l,
    float3 lightColor,
    float lightIntensity)
{
    float3 h = normalize(v + l);
    float ndotv = max(saturate(dot(n, v)), 0.0001f);
    float ndotl = saturate(dot(n, l));

    if (ndotl <= 0.0f)
    {
        return 0.0f.xxx;
    }

    float3 F0 = lerp(0.04f.xxx, baseColor, metallic);
    float3 F = FresnelSchlick(saturate(dot(h, v)), F0);
    float D = DistributionGGX(n, h, roughness);
    float G = GeometrySmith(n, v, l, roughness);

    float3 numerator = D * G * F;
    float denominator = max(4.0f * ndotv * ndotl, 0.0001f);
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = (1.0f.xxx - kS) * (1.0f - metallic);

    float3 diffuse = kD * baseColor / PI;

    return (diffuse + specular) * lightColor * lightIntensity * ndotl;
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

        sum += EvaluateDirectPbr(
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

float3 RotateSkyYaw(float3 dir, float yaw)
{
    float s = sin(yaw);
    float c = cos(yaw);

    return float3(
        dir.x * c - dir.z * s,
        dir.y,
        dir.x * s + dir.z * c
    );
}

float3 EvaluateSkyApprox(float3 dir)
{
    dir = normalize(RotateSkyYaw(dir, gSkyYaw));

    float y = saturate(dir.y * 0.5f + 0.5f);

    float3 upper = lerp(gSkyHorizonColor, gSkyZenithColor, y);
    float3 lower = lerp(gSkyGroundColor, gSkyHorizonColor, y);
    float3 color = (dir.y >= 0.0f) ? upper : lower;

    float horizon = pow(
        saturate(1.0f - abs(dir.y)),
        max(0.01f, gSkyHorizonPower)
    );

    color = lerp(color, gSkyHorizonColor, horizon * 0.25f);
    color *= max(0.0f, gSkyExposure);

    return color;
}

float3 SampleSkyEnvironment(float3 dir)
{
    float3 sky = EvaluateSkyApprox(dir);

    uint mode = (uint)(gSkyMode + 0.5f);

    if (mode == 2u && gSkyHasCubemap > 0.5f)
    {
        float3 cubeDir = normalize(RotateSkyYaw(dir, gSkyYaw));
        sky = gSkyCube.Sample(gLinearWrap, cubeDir).rgb;
        sky *= max(0.0f, gSkyExposure);
    }

    return sky;
}

float3 EvaluateAmbientIblApprox(
    float3 baseColor,
    float metallic,
    float roughness,
    float occlusion,
    float3 n,
    float3 v)
{
    float3 F0 = lerp(0.04f.xxx, baseColor, metallic);
    float ndotv = saturate(dot(n, v));
    float3 F = FresnelSchlick(ndotv, F0);

    float3 kS = F;
    float3 kD = (1.0f.xxx - kS) * (1.0f - metallic);

    float3 diffuseAmbient = gAmbientColor.rgb * gAmbientIntensity;

    if (gSkyAmbientFromSky > 0.0001f)
    {
        float3 skyDiffuse =
            lerp(gSkyGroundColor, gSkyHorizonColor, saturate(n.y * 0.5f + 0.5f));
        skyDiffuse *= gSkyExposure * gSkyAmbientFromSky;
        diffuseAmbient = lerp(diffuseAmbient, skyDiffuse, saturate(gSkyAmbientFromSky));
    }

    float3 diffuse = kD * baseColor * diffuseAmbient;

    float3 r = reflect(-v, n);
    r.y = abs(r.y);

    float3 specEnv = SampleSkyEnvironment(r);
    specEnv *= max(0.0f, gSkyReflectionIntensity);

    float roughnessFade = 1.0f - saturate(roughness * 0.55f);
    float3 specular = specEnv * F * roughnessFade;

    return (diffuse + specular) * occlusion;
}

float3 ResolveEmissive(float2 uv)
{
    float3 emissive = gEmissiveFactor.rgb;
    if (gHasEmissiveTexture != 0)
    {
        emissive *= gEmissiveTex.Sample(gLinearWrap, uv).rgb;
    }
    return emissive * gEmissiveFactor.a;
}

void ResolvePbrInputs(float2 uv, out float metallic, out float roughness, out float occlusion)
{
    metallic = saturate(gMetallicFactor);
    roughness = clamp(gRoughnessFactor, 0.04f, 1.0f);
    occlusion = 1.0f;

    if (gHasMetallicRoughnessTexture != 0)
    {
        float4 mr = gMetallicRoughnessTex.Sample(gLinearWrap, uv);
        roughness = clamp(roughness * mr.g, 0.04f, 1.0f);
        metallic = saturate(metallic * mr.b);
    }

    if (gHasOcclusionTexture != 0)
    {
        float ao = gOcclusionTex.Sample(gLinearWrap, uv).r;
        occlusion = lerp(1.0f, ao, saturate(gOcclusionStrength));
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

float SampleDirectionalShadow(float3 worldPosWS, float3 geometricNormalWS)
{
    if (gShadowEnabled == 0 || gReceiveShadow == 0)
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
    float3 n = ResolveShadingNormal(input.normalWS, input.tangentWS, input.uv);
    float3 geometricNormal = normalize(input.normalWS);
    float3 l = normalize(-gDirectionalDir.xyz);
    float3 v = normalize(gCameraPos.xyz - input.worldPosWS);
#if !HIKARI_USE_COOK_TORRANCE_PBR
    float3 h = normalize(l + v);
#endif

    float ndotl = saturate(dot(n, l));

    float4 albedo = gBaseColor;
    if (gHasBaseColorTexture != 0)
    {
        albedo *= gBaseColorTex.Sample(gLinearWrap, input.uv);
    }
    if ((gMaterialFlags & MATERIAL_ALPHA_MASK) != 0 && albedo.a < gAlphaCutoff)
    {
        discard;
    }

    float metallic = 0.0f;
    float roughness = 1.0f;
    float occlusion = 1.0f;
    ResolvePbrInputs(input.uv, metallic, roughness, occlusion);
    float shadowFactor = SampleDirectionalShadow(input.worldPosWS, geometricNormal);
    float3 emissive = ((gMaterialFlags & MATERIAL_EMISSIVE) != 0) ? ResolveEmissive(input.uv) : 0.0f.xxx;

    float3 shadedColor = albedo.rgb;
    if ((gMaterialFlags & MATERIAL_UNLIT) == 0)
    {
#if HIKARI_USE_COOK_TORRANCE_PBR
        float3 direct =
            EvaluateDirectPbr(
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

        float3 ambient = EvaluateAmbientIblApprox(
            albedo.rgb,
            metallic,
            roughness,
            occlusion,
            n,
            v);

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

    float3 finalColor = shadedColor + emissive;
    finalColor = ApplyFog(finalColor, input.worldPosWS);
    return float4(finalColor, albedo.a);
}
