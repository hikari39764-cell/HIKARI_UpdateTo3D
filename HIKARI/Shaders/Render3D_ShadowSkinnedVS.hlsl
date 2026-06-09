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

#define MAX_JOINTS 128

cbuffer JointPaletteCB : register(b3)
{
    float4x4 gJointMatrices[MAX_JOINTS];
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float4 color0 : COLOR0;
    uint4 joints : JOINTS0;
    float4 weights : WEIGHTS0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint materialFlags : MATERIALFLAGS;
    nointerpolation float alphaCutoff : ALPHACUTOFF;
};

float4x4 ResolveJointMatrix(uint jointIndex)
{
    return gJointMatrices[min(jointIndex, MAX_JOINTS - 1)];
}

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 localPos =
        mul(ResolveJointMatrix(input.joints.x), float4(input.position, 1.0f)) * input.weights.x +
        mul(ResolveJointMatrix(input.joints.y), float4(input.position, 1.0f)) * input.weights.y +
        mul(ResolveJointMatrix(input.joints.z), float4(input.position, 1.0f)) * input.weights.z +
        mul(ResolveJointMatrix(input.joints.w), float4(input.position, 1.0f)) * input.weights.w;

    float4 worldPos = mul(gWorld, float4(localPos.xyz, 1.0f));
    output.position = mul(gLightViewProj, worldPos);
    output.uv = input.uv0;
    output.materialFlags = gMaterialFlags;
    output.alphaCutoff = gAlphaCutoff;
    return output;
}
