static const uint MATERIAL_ALPHA_MASK = 1u << 1;
static const uint HIKARI_INVALID_MESHLET_SHADOW_MATERIAL_INDEX = 0xffffffffu;

#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshMaterialData.hlsli"

SamplerState gLinearWrap : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPosWS : TEXCOORD1;
    float3 normalWS : NORMAL;
    float4 tangentWS : TANGENT;
    float2 uv : TEXCOORD0;
    nointerpolation uint materialDataIndex : TEXCOORD2;
    nointerpolation uint receiveShadow : TEXCOORD3;
    nointerpolation uint objectDataIndex : TEXCOORD4;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD5;
    nointerpolation uint debugClusterId : TEXCOORD6;
    nointerpolation uint debugSurfaceId : TEXCOORD7;
    nointerpolation uint debugLodIndex : TEXCOORD8;
    nointerpolation uint debugDrawBucket : TEXCOORD9;
};

void main(PSInput input)
{
    if (input.materialDataIndex == HIKARI_INVALID_MESHLET_SHADOW_MATERIAL_INDEX)
    {
        return;
    }

    HikariMeshMaterialData materialData =
        HikariGetMeshMaterialData(input.materialDataIndex);
    if ((materialData.materialFlags & MATERIAL_ALPHA_MASK) == 0u)
    {
        return;
    }

    const float alpha =
        HikariSampleMaterialTexture(
            materialData.baseColorTextureDescriptorIndex,
            gLinearWrap,
            input.uv,
            float4(1.0f, 1.0f, 1.0f, 1.0f)).a;
    if (alpha < materialData.pbrParams.w)
    {
        discard;
    }
}
