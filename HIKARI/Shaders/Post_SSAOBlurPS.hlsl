cbuffer SsaoPassCB : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4 gCameraPos;
    float4 gScreenParams;
    float4 gAoParams0;
    float4 gAoParams1;
    float4 gBlurParams;
};

Texture2D gAoTex : register(t0);
Texture2D gSceneDepthTex : register(t1);
Texture2D gNormalRoughnessTex : register(t2);
SamplerState gPointClamp : register(s0);
SamplerState gLinearClamp : register(s1);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut o;
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2);
    o.uv = pos;
    o.pos = float4(pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

float3 DecodeNormal(float4 packed)
{
    return normalize(packed.xyz * 2.0f - 1.0f);
}

float PSMain(VSOut input) : SV_TARGET
{
    float2 texel = gScreenParams.zw * gBlurParams.xy;
    float centerDepth = gSceneDepthTex.SampleLevel(gPointClamp, input.uv, 0).r;
    float3 centerNormal = DecodeNormal(gNormalRoughnessTex.SampleLevel(gPointClamp, input.uv, 0));

    float sum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int i = -2; i <= 2; ++i)
    {
        float2 uv = input.uv + texel * float(i);
        float ao = gAoTex.SampleLevel(gLinearClamp, uv, 0).r;
        float depth = gSceneDepthTex.SampleLevel(gPointClamp, uv, 0).r;
        float3 normal = DecodeNormal(gNormalRoughnessTex.SampleLevel(gPointClamp, uv, 0));
        float normalWeight = saturate(dot(centerNormal, normal));
        normalWeight *= normalWeight;
        float depthWeight = exp(-abs(depth - centerDepth) * 64.0f);
        float kernelWeight = (i == 0) ? 0.34f : ((abs(i) == 1) ? 0.22f : 0.10f);
        float weight = kernelWeight * normalWeight * depthWeight;
        sum += ao * weight;
        weightSum += weight;
    }

    return sum / max(weightSum, 1e-4f);
}

// Depth-only AO 用の軽量 blur。
float PSMainDepthOnly(VSOut input) : SV_TARGET
{
    float2 texel = gScreenParams.zw * gBlurParams.xy;
    float centerDepth = gSceneDepthTex.SampleLevel(gPointClamp, input.uv, 0).r;

    float sum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int i = -1; i <= 1; ++i)
    {
        float2 uv = saturate(input.uv + texel * float(i));
        float ao = gAoTex.SampleLevel(gLinearClamp, uv, 0).r;
        float depth = gSceneDepthTex.SampleLevel(gPointClamp, uv, 0).r;
        float depthWeight = exp(-abs(depth - centerDepth) * 96.0f);
        float kernelWeight = (i == 0) ? 0.50f : 0.25f;
        float weight = kernelWeight * depthWeight;
        sum += ao * weight;
        weightSum += weight;
    }

    return sum / max(weightSum, 1e-4f);
}
