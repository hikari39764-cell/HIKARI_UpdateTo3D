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
    float4 gFxUser0;
    float4 gFxUser1;
    float4 gFxUser2;
    float4 gFxUser3;
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
SamplerState gShadowSampler : register(s1);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
};

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

float3 CheapSkyColor(float3 dir)
{
    float up = saturate(dir.y * 0.5f + 0.5f);

    float3 horizonColor = float3(0.35f, 0.55f, 0.75f);
    float3 skyColor = float3(0.04f, 0.16f, 0.35f);

    return lerp(horizonColor, skyColor, up);
}

float4 main(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normalWS);
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
    float3 baseWater = lerp(waterColor, shallowColor, shallowMix * 0.35f);

    float shadowFactor = SampleDirectionalShadow(input.worldPosWS, n);

    float3 ambient = gAmbientColor.rgb * max(gAmbientIntensity, 0.05f);
    float3 sun = gDirectionalColor.rgb * gDirectionalIntensity * ndotl * shadowFactor;

    float specular = pow(saturate(dot(n, h)), 96.0f) * specularStrength;
    float3 specularColor = gDirectionalColor.rgb * gDirectionalIntensity * specular * shadowFactor;

    float reflectionShadow = lerp(0.05f, 1.0f, shadowFactor);
    float3 reflection = CheapSkyColor(reflectDir) * reflectionShadow;


    float3 color = baseWater * (ambient + sun * 0.55f);
    color = lerp(color, reflection, fresnel);
    color += specularColor;

    float rim = pow(1.0f - saturate(dot(n, v)), 2.0f) * rimStrength;
    color += float3(0.25f, 0.75f, 1.0f) * rim;

    if (gDebugView == 1)
    {
        return float4(n * 0.5f + 0.5f, 1.0f);
    }

    color = ApplyFog(color, input.worldPosWS);

    return float4(color, 0.72f);
}
