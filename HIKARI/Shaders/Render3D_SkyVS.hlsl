cbuffer SkyCB : register(b0)
{
    float4x4 gWorldViewProj;
    float4 gTintExposure;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
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
    output.position = mul(gWorldViewProj, float4(input.position, 1.0f));
    output.uv = input.uv;
    return output;
}
