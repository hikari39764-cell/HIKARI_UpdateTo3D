#define HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING 1
#include "Include/HIKARI_MeshObjectData.hlsli"

static const uint MATERIAL_ALPHA_MASK = 1u << 1;

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
    // Keep TEXCOORD slots aligned with ClusterVS/MeshletMS for PSO linkage.
    nointerpolation uint objectDataIndex : TEXCOORD4;
    nointerpolation uint surfaceGpuSceneIndex : TEXCOORD5;
    nointerpolation uint debugClusterId : TEXCOORD6;
    nointerpolation uint debugSurfaceId : TEXCOORD7;
    nointerpolation uint debugLodIndex : TEXCOORD8;
    nointerpolation uint debugDrawBucket : TEXCOORD9;
};

float4 main(PSInput input) : SV_TARGET
{
    HikariMeshMaterialData materialData = HikariGetMeshMaterialData(input.materialDataIndex);
    float4 albedo = materialData.baseColor;
    if (materialData.hasBaseColorTexture != 0)
    {
        albedo *= HikariSampleMaterialTexture(
            materialData.baseColorTextureDescriptorIndex,
            gLinearWrap,
            input.uv,
            float4(1.0f, 1.0f, 1.0f, 1.0f));
    }
    if ((materialData.materialFlags & MATERIAL_ALPHA_MASK) != 0 && albedo.a < materialData.pbrParams.w)
    {
        discard;
    }

    float roughness = clamp(materialData.pbrParams.y, 0.04f, 1.0f);
    if (materialData.hasMetallicRoughnessTexture != 0)
    {
        roughness = clamp(
            roughness * HikariSampleMaterialTexture(
                materialData.metallicRoughnessTextureDescriptorIndex,
                gLinearWrap,
                input.uv,
                float4(1.0f, 1.0f, 1.0f, 1.0f)).g,
            0.04f,
            1.0f);
    }

    float3 n = normalize(input.normalWS);
    return float4(n * 0.5f + 0.5f, roughness);
}
