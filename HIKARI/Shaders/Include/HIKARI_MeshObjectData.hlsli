#ifndef HIKARI_MESH_OBJECT_DATA_INCLUDED
#define HIKARI_MESH_OBJECT_DATA_INCLUDED

#include "Include/HIKARI_MeshMaterialData.hlsli"

// Keep this layout in sync with C++ ObjectGpuData.
struct HikariMeshObjectData
{
    float4x4 world;
    float4x4 normalMatrix;
    float4 baseColor;
    uint hasBaseColorTexture;
    uint fxFlags;
    uint materialFlags;
    float alphaCutoff;
    float4 emissiveFactor;
    uint hasNormalTexture;
    float normalScale;
    float2 normalPadding;
    uint receiveShadow;
    float3 shadowObjectPadding;
    uint hasEmissiveTexture;
    float3 emissivePadding;
    float metallicFactor;
    float roughnessFactor;
    uint hasMetallicRoughnessTexture;
    uint hasOcclusionTexture;
    float occlusionStrength;
    uint materialDataIndex;
    float2 pbrPadding;
    float4 fxUser[8];
};

StructuredBuffer<HikariMeshObjectData> gObjectDataBuffer : register(t15);

cbuffer ObjectIndexCB : register(b6)
{
    uint gObjectDataIndex;
    uint3 gObjectDataPadding;
};

// instance 描画では base index + SV_InstanceID で object data を読む。
HikariMeshObjectData HikariGetMeshObjectData(uint objectDataIndex)
{
    return gObjectDataBuffer[objectDataIndex];
}

#define gObjectData gObjectDataBuffer[gObjectDataIndex]
#define gWorld gObjectData.world
#define gNormalMatrix gObjectData.normalMatrix
#define gFxFlags gObjectData.fxFlags
#define gNormalPadding gObjectData.normalPadding
#define gReceiveShadow gObjectData.receiveShadow
#define gShadowObjectPadding gObjectData.shadowObjectPadding
#define gEmissivePadding gObjectData.emissivePadding
#define gPbrPadding gObjectData.pbrPadding
#define gFxUser gObjectData.fxUser

#endif
