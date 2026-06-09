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

cbuffer ShadowObjectDataControlCB : register(b2)
{
    uint gShadowObjectDataBaseIndex;
    uint gUseShadowObjectData;
    uint2 gShadowObjectDataPadding;
};

struct ShadowPacketObjectData
{
    float4x4 world;
    uint materialFlags;
    float alphaCutoff;
    float2 padding;
};

StructuredBuffer<ShadowPacketObjectData> gShadowObjectDataBuffer : register(t1);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
    uint instanceId : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint materialFlags : MATERIALFLAGS;
    nointerpolation float alphaCutoff : ALPHACUTOFF;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4x4 world = gWorld;
    uint materialFlags = gMaterialFlags;
    float alphaCutoff = gAlphaCutoff;

    if (gUseShadowObjectData != 0)
    {
        ShadowPacketObjectData objectData =
            gShadowObjectDataBuffer[gShadowObjectDataBaseIndex + input.instanceId];
        world = objectData.world;
        materialFlags = objectData.materialFlags;
        alphaCutoff = objectData.alphaCutoff;
    }

    float4 worldPos = mul(world, float4(input.position, 1.0f));
    output.position = mul(gLightViewProj, worldPos);
    output.uv = input.uv;
    output.materialFlags = materialFlags;
    output.alphaCutoff = alphaCutoff;
    return output;
}
