cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4 gCameraPos;
};

cbuffer ObjectCB : register(b1)
{
    float4x4 gWorld;
    float4x4 gNormalMatrix;
    float4 gBaseColor;
    uint gHasBaseColorTexture;
    uint gFxFlags;
    float2 gObjectPadding;
    float4 gFxUser0;
    float4 gFxUser1;
    float4 gFxUser2;
    float4 gFxUser3;
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
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float2 uv : TEXCOORD0;
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

    float3 localNormal =
        mul((float3x3)ResolveJointMatrix(input.joints.x), input.normal) * input.weights.x +
        mul((float3x3)ResolveJointMatrix(input.joints.y), input.normal) * input.weights.y +
        mul((float3x3)ResolveJointMatrix(input.joints.z), input.normal) * input.weights.z +
        mul((float3x3)ResolveJointMatrix(input.joints.w), input.normal) * input.weights.w;

    float4 worldPos = mul(gWorld, float4(localPos.xyz, 1.0f));
    output.position = mul(gViewProj, worldPos);
    output.worldPosWS = worldPos.xyz;
    output.normalWS = normalize(mul((float3x3)gNormalMatrix, normalize(localNormal)));
    output.uv = input.uv0;
    return output;
}
