cbuffer TemporalMotionVectorCB : register(b0)
{
    float4x4 gInvViewProj;
    float4x4 gViewProj;
    float4x4 gPrevViewProj;
    float4 gScreenParams;  // xy: render size, zw: inverse render size
    float4 gHistoryParams; // x: history valid, y: depth valid
};

Texture2D<float> gSceneDepth : register(t0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut output;
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = pos;
    output.position =
        float4(pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float3 ReconstructWorld(float2 uv, float depth)
{
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float4 world = mul(gInvViewProj, float4(ndc, depth, 1.0f));
    return world.xyz / max(abs(world.w), 1e-5f);
}

float2 ProjectUv(float4x4 viewProj, float3 worldPos)
{
    float4 clip = mul(viewProj, float4(worldPos, 1.0f));
    float3 ndc = clip.xyz / max(abs(clip.w), 1e-5f);
    return float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
}

float2 PSMain(VSOut input) : SV_TARGET
{
    if (gHistoryParams.x < 0.5f || gHistoryParams.y < 0.5f)
    {
        return float2(0.0f, 0.0f);
    }

    uint2 pixel = min(
        uint2(input.position.xy),
        max(uint2(gScreenParams.xy), uint2(1u, 1u)) - 1u);
    float depth = gSceneDepth.Load(int3(pixel, 0)).r;
    if (depth >= 0.99999f)
    {
        return float2(0.0f, 0.0f);
    }

    float3 worldPos = ReconstructWorld(input.uv, depth);
    float2 currentUv = ProjectUv(gViewProj, worldPos);
    float2 previousUv = ProjectUv(gPrevViewProj, worldPos);
    return (currentUv - previousUv) * gScreenParams.xy;
}
