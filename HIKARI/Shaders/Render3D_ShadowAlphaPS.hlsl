cbuffer ShadowObjectCB : register(b1)
{
    float4x4 gWorld;
    uint gMaterialFlags;
    float gAlphaCutoff;
    float2 gShadowObjectPadding;
};

static const uint MATERIAL_ALPHA_MASK = 1u << 1;

Texture2D gBaseColorTex : register(t0);
SamplerState gLinearWrap : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

void main(PSInput input)
{
    if ((gMaterialFlags & MATERIAL_ALPHA_MASK) != 0)
    {
        float alpha = gBaseColorTex.Sample(gLinearWrap, input.uv).a;
        if (alpha < gAlphaCutoff)
        {
            discard;
        }
    }
}
