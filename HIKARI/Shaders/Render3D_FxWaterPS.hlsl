
#define gFxUser0 gFxUser[0]
#define gFxUser1 gFxUser[1]
#define gFxUser2 gFxUser[2]
#define gFxUser3 gFxUser[3]
#define gFxUser4 gFxUser[4]
#define gFxUser5 gFxUser[5]
#define gFxUser6 gFxUser[6]
#define gFxUser7 gFxUser[7]

#define gWaterDepthScale gFxUser4.x
#define gWaterDepthBias  gFxUser4.y
#define gWaterDepthPower gFxUser4.z
#define gWaterDepthBlend gFxUser4.w

cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4 gCameraPos;
    float4 gTimeParams;
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

Texture2D gShadowMap : register(t2) ;
TextureCube gSkyCube : register(t6);
Texture2D gSceneDepth : register(t7);
SamplerState gShadowSampler : register(s1);
SamplerState gSkySampler : register(s0);

#ifndef WATER_DEBUG_SCENE_DEPTH
#define WATER_DEBUG_SCENE_DEPTH 0
#endif

#ifndef WATER_DEBUG_DEPTH_DIFF
#define WATER_DEBUG_DEPTH_DIFF 0
#endif

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
};

float SampleSceneDepth(float4 svPosition)
{
    int2 pixel = int2(svPosition.xy);
    return gSceneDepth.Load(int3(pixel, 0)).r;
}

float ComputeRawWaterDepthDiff(float4 svPosition)
{
    float sceneDepth = SampleSceneDepth(svPosition);
    float waterDepth = svPosition.z;

    return max(0.0f, sceneDepth - waterDepth);
}

float ComputeWaterDepthFactor(float4 svPosition)
{
    float rawDiff = ComputeRawWaterDepthDiff(svPosition);

    float depthScale = gWaterDepthScale;
    if (depthScale <= 0.0001f)
    {
        depthScale = 80.0f;
    }

    float depthBias = gWaterDepthBias;

    float depthPower = gWaterDepthPower;
    if (depthPower <= 0.0001f)
    {
        depthPower = 1.0f;
    }

    float depthFactor = saturate(rawDiff * depthScale + depthBias);
    depthFactor = pow(depthFactor, depthPower);

    return depthFactor;
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

    return sum / 9.0f;
}

float SampleDirectionalShadow(float3 worldPosWS, float3 normalWS)
{
    if (gShadowEnabled == 0 || gReceiveShadow == 0)
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
    float visibility = SampleShadowPcf(uv, currentDepth);

    return lerp(1.0f - gShadowStrength, 1.0f, visibility);
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
        saturate(1.0f  - abs(dir.y)),
        max(0.01f,gSkyHorizonPower)
    );

    color = lerp(color, gSkyHorizonColor, horizon * 0.25f);

    float exposure = max(0.0f, gSkyExposure);
    color *= exposure;

    return color;

}

float3 SampleSkyEnvironment(float3 dir)
{
    float3 sky = EvaluateSkyApprox(dir);

    uint mode = (uint)(gSkyMode + 0.5f);

    if(mode == 2u && gSkyHasCubemap > 0.5f)
    {
        float3 cubeDir = normalize(RotateSkyYaw(dir, gSkyYaw));
        sky = gSkyCube.Sample(gSkySampler, cubeDir).rgb;
        sky *= max(0.0f, gSkyExposure);
    }

    return sky;

}

float4 main(PSInput input) : SV_TARGET
{
#if WATER_DEBUG_SCENE_DEPTH
    float sceneDepth = SampleSceneDepth(input.position);
    float vi = saturate((1.0f - sceneDepth) * 80.0f);
    return float4(vi.xxx, 1.0f);
#endif

#if WATER_DEBUG_DEPTH_DIFF
    float depthFactor = ComputeWaterDepthFactor(input.position);
    return float4(depthFactor.xxx, 1.0f);
#endif

    float3 n = normalize(input.normalWS);

    float distToCamera = length(gCameraPos.xyz - input.worldPosWS);

    float farNormalFade = saturate((distToCamera - 40.0f) / 140.0f);

    float3 flatNormal = float3(0.0f, 1.0f, 0.0f);

    n = normalize(lerp(n, flatNormal, farNormalFade * 0.85f));

    float3 v = normalize(gCameraPos.xyz - input.worldPosWS);

    // Keep same direction convention as Render3D_StaticPS.
    float3 l = normalize(gDirectionalDir.xyz);
    float3 h = normalize(l + v);

    float3 waterColor = gFxUser1.xyz;
    if (dot(waterColor, waterColor) < 1e-5f)
    {
        waterColor = float3(0.05f, 0.35f, 0.75f);
    }

    float rimStrength = gFxUser0.w;

    float fresnelPower = gFxUser2.x;
    if (fresnelPower <= 0.0001f)
    {
        fresnelPower = 5.0f;
    }

    float reflectionStrength = gFxUser2.y;
    if (reflectionStrength <= 0.0001f)
    {
        reflectionStrength = 0.85f;
    }

    float specularStrength = gFxUser2.z;
    if (specularStrength <= 0.0001f)
    {
        specularStrength = 1.2f;
    }

    float3 shallowColor = gFxUser3.rgb;
    if (dot(shallowColor, shallowColor) < 1e-5f)
    {
        shallowColor = float3(0.12f, 0.55f, 0.68f);
    }

    float ndotl = saturate(dot(n, l));

    float fresnel = pow(1.0f - saturate(dot(n, v)), fresnelPower);
    fresnel = saturate(fresnel * reflectionStrength);


    float3 reflectDir = reflect(-v, n);
    reflectDir.y = abs(reflectDir.y);


    float shallowMix = saturate(n.y);
    float oldNormalShallow = shallowMix * 0.20f;

    float depthFactor = ComputeWaterDepthFactor(input.position);

    float depthBlend = gWaterDepthBlend;
    if (depthBlend <= 0.0001f)
    {
        depthBlend = 1.0f;
    }

    float3 depthWaterColor = lerp(shallowColor, waterColor, depthFactor);
    float3 normalWaterColor = lerp(waterColor, shallowColor, oldNormalShallow);
    float3 baseWater = lerp(normalWaterColor, depthWaterColor, saturate(depthBlend));

    float shadowFactor = SampleDirectionalShadow(input.worldPosWS, n);

    float3 ambient = gAmbientColor.rgb * max(gAmbientIntensity, 0.05f);
    float3 sun = gDirectionalColor.rgb * gDirectionalIntensity * ndotl * shadowFactor;

    float specular = pow(saturate(dot(n, h)), 96.0f) * specularStrength;
    specular *= (1.0f - farNormalFade * 0.75f);
    float3 specularColor = gDirectionalColor.rgb * gDirectionalIntensity * specular * shadowFactor;

    float reflectionShadow = lerp(0.05f, 1.0f, shadowFactor);

    float3 reflection = SampleSkyEnvironment(reflectDir);

    float horizonFade = saturate((distToCamera - 80.0f) / 160.0f);
    float3 horizonDir = normalize(float3(v.x,0.05f,v.z));
    float3 horizonSky = EvaluateSkyApprox(horizonDir);

    reflection = lerp(reflection, horizonSky, horizonFade * 0.65f);
    reflection *= max(0.0f, gSkyReflectionIntensity);
    reflection *= reflectionShadow;
    
    float3 color = baseWater * (ambient + sun * 0.55f);
    float reflectionMix = saturate(fresnel * 0.75f);
    color = lerp(color, reflection, reflectionMix);
    color += specularColor;

    float rim = pow(1.0f - saturate(dot(n, v)), 2.0f) * rimStrength;
    color += float3(0.25f, 0.75f, 1.0f) * rim;

    if (gDebugView == 1)
    {
        return float4(n * 0.5f + 0.5f, 1.0f);
    }

    color = ApplyFog(color, input.worldPosWS);

    return float4(color, 1.0f);
}
