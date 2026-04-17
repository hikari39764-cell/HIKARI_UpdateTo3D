Texture2D gTex : register(t0);
SamplerState gSamp : register(s0);

cbuffer CommonParams : register(b0)
{
    float gTime;
    float gDeltaTime;
    float gCombo;
    float gIntensity;

    float gResolutionX;
    float gResolutionY;
    float gPad0;
    float gPad1;

    float4 gUser[16];
};

struct PS_IN {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PS_IN i) : SV_TARGET
{
    float4 color = gTex.Sample(gSamp, i.uv);

    float3 tint = max(gUser[0].rgb, 0.0.xxx);
    float blend = saturate(gUser[0].a);
    float3 tinted = color.rgb * tint;
    color.rgb = lerp(color.rgb, tinted, blend);

    return color;
}
