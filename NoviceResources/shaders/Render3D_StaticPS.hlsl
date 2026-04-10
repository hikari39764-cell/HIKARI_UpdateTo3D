cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4 gCameraPos;
};

cbuffer ObjectCB : register(b1)
{
    float4x4 gWorld;
    float4 gBaseColor;
    uint gHasBaseColorTexture;
    float3 gObjectPadding;
};

Texture2D gBaseColorTex : register(t0);
SamplerState gLinearWrap : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normalWS : NORMAL;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normalWS);
    float3 lightDir = normalize(float3(0.4f, 1.0f, -0.6f));
    float ndotl = saturate(dot(n, lightDir));
    float lit = 0.2f + 0.8f * ndotl;
    float4 albedo = gBaseColor;
    if (gHasBaseColorTexture != 0)
    {
        albedo *= gBaseColorTex.Sample(gLinearWrap, input.uv);
    }
    return float4(albedo.rgb * lit, 1.0f);
}
