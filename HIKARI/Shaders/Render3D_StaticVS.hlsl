cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4 gCameraPos;
};

cbuffer ObjectCB : register(b1)
{
    float4x4 gWorld;
    float4x4 gNormalMatrix;
    float4 gBaseColorFactor;
    float4 gEmissiveFactor;
    float gNormalScale;
    float gOcclusionStrength;
    float gMetallicFactor;
    float gRoughnessFactor;
    uint gHasBaseColorTexture;
    uint gHasNormalTexture;
    uint gHasOrmTexture;
    uint gHasEmissiveTexture;
    uint gAlphaMode;
    float gAlphaCutoff;
    uint gFxFlags;
    float gObjectPadding;
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
    float3 normalWS : TEXCOORD2;
    float3 tangentWS : TEXCOORD3;
    float3 bitangentWS : TEXCOORD4;
    float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 worldPos = mul(gWorld, float4(input.position, 1.0f));
    output.position = mul(gViewProj, worldPos);
    output.worldPosWS = worldPos.xyz;
    output.normalWS = normalize(mul((float3x3)gNormalMatrix, input.normal));
    output.tangentWS = normalize(mul((float3x3)gNormalMatrix, input.tangent.xyz));
    output.bitangentWS = normalize(cross(output.normalWS, output.tangentWS) * input.tangent.w);
    output.uv = input.uv;
    return output;
}
