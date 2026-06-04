#ifndef HIKARI_MESH_MATERIAL_DATA_INCLUDED
#define HIKARI_MESH_MATERIAL_DATA_INCLUDED

// Keep this layout in sync with C++ MaterialGpuData.
struct HikariMeshMaterialData
{
    float4 baseColor;
    float4 emissiveFactor;
    float4 pbrParams;
    uint materialFlags;
    uint hasBaseColorTexture;
    uint hasNormalTexture;
    uint hasEmissiveTexture;
    uint hasMetallicRoughnessTexture;
    uint hasOcclusionTexture;
    float normalScale;
    float materialPadding0;
    int baseColorTextureHandle;
    int normalTextureHandle;
    int emissiveTextureHandle;
    int metallicRoughnessTextureHandle;
    int occlusionTextureHandle;
    uint baseColorTextureDescriptorIndex;
    uint normalTextureDescriptorIndex;
    uint emissiveTextureDescriptorIndex;
    uint metallicRoughnessTextureDescriptorIndex;
    uint occlusionTextureDescriptorIndex;
    uint2 materialPadding1;
};

StructuredBuffer<HikariMeshMaterialData> gMaterialDataBuffer : register(t16);

cbuffer MaterialIndexCB : register(b7)
{
    uint gMaterialDataIndex;
    uint3 gMaterialDataPadding;
};

#define gMaterialData gMaterialDataBuffer[gMaterialDataIndex]
#define gBaseColor gMaterialData.baseColor
#define gMaterialFlags gMaterialData.materialFlags
#define gHasBaseColorTexture gMaterialData.hasBaseColorTexture
#define gHasNormalTexture gMaterialData.hasNormalTexture
#define gHasEmissiveTexture gMaterialData.hasEmissiveTexture
#define gHasMetallicRoughnessTexture gMaterialData.hasMetallicRoughnessTexture
#define gHasOcclusionTexture gMaterialData.hasOcclusionTexture
#define gNormalScale gMaterialData.normalScale
#define gAlphaCutoff gMaterialData.pbrParams.w
#define gEmissiveFactor gMaterialData.emissiveFactor
#define gMetallicFactor gMaterialData.pbrParams.x
#define gRoughnessFactor gMaterialData.pbrParams.y
#define gOcclusionStrength gMaterialData.pbrParams.z

#endif
