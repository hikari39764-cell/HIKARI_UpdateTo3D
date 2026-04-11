cbuffer SkyCB : register(b0)
{
    float4x4 gWorldViewProj;
    float4 gTintExposure;
};

Texture2D gSkyTex : register(t0);
SamplerState gLinearWrap : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 sky = gSkyTex.Sample(gLinearWrap, input.uv).rgb;
    sky *= gTintExposure.rgb;
    sky *= gTintExposure.w;
    return float4(sky, 1.0f);
}
