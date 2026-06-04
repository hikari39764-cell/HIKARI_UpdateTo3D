#include "Include/HIKARI_MeshObjectData.hlsli"

static const uint MATERIAL_ALPHA_MASK = 1u << 1;

Texture2D gBaseColorTex : register(t0);
Texture2D gMetallicRoughnessTex : register(t4);
SamplerState gLinearWrap : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 albedo = gBaseColor;
    if (gHasBaseColorTexture != 0)
    {
        albedo *= gBaseColorTex.Sample(gLinearWrap, input.uv);
    }
    if ((gMaterialFlags & MATERIAL_ALPHA_MASK) != 0 && albedo.a < gAlphaCutoff)
    {
        discard;
    }

    float roughness = clamp(gRoughnessFactor, 0.04f, 1.0f);
    if (gHasMetallicRoughnessTexture != 0)
    {
        roughness = clamp(roughness * gMetallicRoughnessTex.Sample(gLinearWrap, input.uv).g, 0.04f, 1.0f);
    }

    float3 n = normalize(input.normalWS);
    return float4(n * 0.5f + 0.5f, roughness);
}
