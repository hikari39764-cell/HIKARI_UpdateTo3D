cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4 gCameraPos;
    // x=time seconds, y=unscaled dt, z=game dt, w=frame index.
    float4 gTimeParams;
};

// Keep this layout in sync with MeshRenderer::ObjectCB. The water VS uses
// gFxUser0 and gFxUser1, so the preceding fields must not drift.
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

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    const float waveSpeed = gFxUser0.x;
    const float waveHeight = gFxUser0.y;
    const float waveScale = max(gFxUser0.z, 0.001f);
    const float phase = gTimeParams.x * waveSpeed;

    float3 localPos = input.position;
    const float wave =
        sin(localPos.x * waveScale + phase) *
        cos(localPos.z * waveScale + phase) *
        waveHeight;
    localPos.y += wave;

    VSOutput output;
    float4 worldPos = mul(gWorld, float4(localPos, 1.0f));
    output.position = mul(gViewProj, worldPos);
    output.worldPosWS = worldPos.xyz;
    output.normalWS = normalize(mul((float3x3)gNormalMatrix, input.normal));
    output.tangentWS = float4(normalize(mul((float3x3)gNormalMatrix, input.tangent.xyz)), input.tangent.w);
    output.uv = input.uv;
    return output;
}
