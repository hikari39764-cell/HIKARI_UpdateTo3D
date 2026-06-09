static const uint MATERIAL_ALPHA_MASK = 1u << 1;

Texture2D gBaseColorTex : register(t0);
SamplerState gLinearWrap : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint materialFlags : MATERIALFLAGS;
    nointerpolation float alphaCutoff : ALPHACUTOFF;
};

void main(PSInput input)
{
    if ((input.materialFlags & MATERIAL_ALPHA_MASK) != 0)
    {
        float alpha = gBaseColorTex.Sample(gLinearWrap, input.uv).a;
        if (alpha < input.alphaCutoff)
        {
            discard;
        }
    }
}
