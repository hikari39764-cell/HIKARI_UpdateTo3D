cbuffer ShadowCameraCB : register(b0)
{
    float4x4 gLightViewProj;
};

cbuffer ShadowObjectCB : register(b1)
{
    float4x4 gWorld;
    uint gMaterialFlags;
    float gAlphaCutoff;
    float2 gShadowObjectPadding;
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
    float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 worldPos = mul(gWorld, float4(input.position, 1.0f));
    output.position = mul(gLightViewProj, worldPos);
    output.uv = input.uv;
    return output;
}
