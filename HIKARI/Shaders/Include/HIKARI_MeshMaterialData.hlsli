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

// instance ごとの material index から MaterialData を読む。
static const uint HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX = 0xffffffffu;

HikariMeshMaterialData HikariBuildDefaultMeshMaterialData()
{
    HikariMeshMaterialData data = (HikariMeshMaterialData)0;
    data.baseColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    data.pbrParams = float4(0.0f, 1.0f, 1.0f, 0.5f);
    data.normalScale = 1.0f;
    data.baseColorTextureHandle = -1;
    data.normalTextureHandle = -1;
    data.emissiveTextureHandle = -1;
    data.metallicRoughnessTextureHandle = -1;
    data.occlusionTextureHandle = -1;
    data.baseColorTextureDescriptorIndex = HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX;
    data.normalTextureDescriptorIndex = HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX;
    data.emissiveTextureDescriptorIndex = HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX;
    data.metallicRoughnessTextureDescriptorIndex = HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX;
    data.occlusionTextureDescriptorIndex = HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX;
    return data;
}

HikariMeshMaterialData HikariGetMeshMaterialData(uint materialDataIndex)
{
    if (materialDataIndex == HIKARI_INVALID_MESH_MATERIAL_DATA_INDEX)
    {
        return HikariBuildDefaultMeshMaterialData();
    }
    return gMaterialDataBuffer[materialDataIndex];
}

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
#define gBaseColorTextureDescriptorIndex gMaterialData.baseColorTextureDescriptorIndex
#define gNormalTextureDescriptorIndex gMaterialData.normalTextureDescriptorIndex
#define gEmissiveTextureDescriptorIndex gMaterialData.emissiveTextureDescriptorIndex
#define gMetallicRoughnessTextureDescriptorIndex gMaterialData.metallicRoughnessTextureDescriptorIndex
#define gOcclusionTextureDescriptorIndex gMaterialData.occlusionTextureDescriptorIndex

#if defined(HIKARI_MATERIAL_TEXTURE_POOL_SAMPLING)
static const uint HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX = 0xffffffffu;
static const uint HIKARI_MATERIAL_TEXTURE_POOL_COUNT = 3968u;

// 材質テクスチャは descriptor index で SRV プールから参照する。
Texture2D gMaterialTexturePool[HIKARI_MATERIAL_TEXTURE_POOL_COUNT] : register(t20);

bool HikariHasMaterialTexture(uint descriptorIndex)
{
    return descriptorIndex != HIKARI_INVALID_TEXTURE_DESCRIPTOR_INDEX &&
        descriptorIndex < HIKARI_MATERIAL_TEXTURE_POOL_COUNT;
}

float4 HikariSampleMaterialTexture(
    uint descriptorIndex,
    SamplerState samplerState,
    float2 uv,
    float4 fallbackValue)
{
    if (!HikariHasMaterialTexture(descriptorIndex))
    {
        return fallbackValue;
    }

    return gMaterialTexturePool[NonUniformResourceIndex(descriptorIndex)].Sample(samplerState, uv);
}
#endif

#endif
