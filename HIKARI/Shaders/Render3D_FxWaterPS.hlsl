// Lightweight water debug pixel shader for procedural grid tests.
// It intentionally does not share Render3D_StaticFxPS dissolve parameters,
// because water_grid_test uses gFxUser0.z as wave scale.
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

    float dist = length(worldPosWS);
    float fogFactor = saturate((dist - gFogParams.y) / max(0.001f, gFogParams.z - gFogParams.y));
    fogFactor = saturate(fogFactor * max(0.0f, gFogColorDensity.a));
    return lerp(color, gFogColorDensity.rgb, fogFactor);
}

float4 main(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normalWS);
    float3 lightDir = normalize(-gDirectionalDir.xyz);
    float ndotl = saturate(dot(n, lightDir));

    float3 waterColor = gFxUser1.xyz;
    if (dot(waterColor, waterColor) < 1e-5f)
    {
        waterColor = float3(0.05f, 0.35f, 0.75f);
    }

    float rimStrength = gFxUser0.w;
    float3 viewDir = normalize(-input.worldPosWS);
    float rim = pow(1.0f - saturate(dot(n, viewDir)), 2.0f) * rimStrength;

    float3 ambient = gAmbientColor.rgb * max(gAmbientIntensity, 0.05f);
    float3 sun = gDirectionalColor.rgb * (gDirectionalIntensity * ndotl);
    float3 color = waterColor * (ambient + sun * 0.65f) + float3(0.25f, 0.7f, 1.0f) * rim;
    color = ApplyFog(color, input.worldPosWS);

    return float4(color, 0.72f);
}
