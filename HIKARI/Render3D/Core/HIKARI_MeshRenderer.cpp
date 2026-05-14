#include "HIKARI_MeshRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <wrl/client.h>
#include "HIKARI_DxTexture.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Render3D/HIKARI_Material.h"
#include "HIKARI_Services.h"
#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Vfx/Common/HIKARI_FxTypes.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace HIKARI::MESHRENDERER {

    using Microsoft::WRL::ComPtr;

    namespace {
        struct CameraCB {
            MATH::Mat4 viewProj{};
            MATH::Vec4 cameraPos{};
            MATH::Vec4 timeParams{};
        };

        struct ObjectCB {
            MATH::Mat4 world{};
            MATH::Mat4 normalMatrix{};
            MATH::Vec4 baseColor{};
            uint32_t hasBaseColorTexture = 0;
            uint32_t fxFlags = 0;
            uint32_t materialFlags = 0;
            float alphaCutoff = 0.5f;
            MATH::Vec4 emissiveFactor{};
            uint32_t hasNormalTexture = 0;
            float normalScale = 1.0f;
            float normalPadding[2]{};
            uint32_t receiveShadow = 1;
            float shadowObjectPadding[3]{};
            uint32_t hasEmissiveTexture = 0;
            float emissivePadding[3]{};
            float metallicFactor = 0.0f;
            float roughnessFactor = 1.0f;
            uint32_t hasMetallicRoughnessTexture = 0;
            uint32_t hasOcclusionTexture = 0;
            float occlusionStrength = 1.0f;
            float pbrPadding[3]{};
            MATH::Vec4 fxUser0{};
            MATH::Vec4 fxUser1{};
            MATH::Vec4 fxUser2{};
            MATH::Vec4 fxUser3{};
        };

        struct LightCB {
            MATH::Vec4 directionalDir{};
            MATH::Vec4 directionalColor{};
            MATH::Vec4 ambientColor{};
            MATH::Vec4 specularParams{};
            MATH::Vec4 pointLightPosRange[8]{};
            MATH::Vec4 pointLightColorIntensity[8]{};
            float directionalIntensity = 1.0f;
            float ambientIntensity = 0.25f;
            uint32_t pointLightCount = 0;
            float lightPadding = 0.0f;
            MATH::Vec4 fogColorDensity{};
            MATH::Vec4 fogParams{};
            uint32_t debugView = 0;
            float debugPadding[3]{};
        };

        struct SkyEnvironmentCB {
            MATH::Vec4 skyZenithExposure{};
            MATH::Vec4 skyHorizonReflection{};
            MATH::Vec4 skyGroundAmbient{};
            MATH::Vec4 skyParams{};
        };

        struct ShadowCB {
            MATH::Mat4 lightViewProj{};
            uint32_t enabled = 0;
            float depthBias = 0.001f;
            float normalBias = 0.02f;
            float strength = 0.75f;
            uint32_t pcfEnabled = 1;
            float pcfRadius = 1.0f;
            float texelSizeX = 1.0f / 2048.0f;
            float texelSizeY = 1.0f / 2048.0f;
        };

        constexpr size_t kMaxJointPaletteMatrices = 128u;
        constexpr UINT kMaxObjectCount = 2048u;

        constexpr UINT AlignConstantBufferSize(size_t size) {
            return static_cast<UINT>((size + 255u) & ~255u);
        }

        struct JointPaletteCB {
            MATH::Mat4 jointMatrices[kMaxJointPaletteMatrices]{};
        };

        struct DrawItem {
            const ModelAsset* asset = nullptr;
            Transform3D transform{};
            std::vector<MATH::Mat4> jointPalette{};
            std::string materialFxProfileId{};
            uint32_t postGroupMask = 0;
            VFX::VariantKey variant{};
            std::array<MATH::Vec4, 4> fxValues{};
            uint32_t fxFlags = 0;
            std::array<DirectX::XMFLOAT4, 4> materialFxParamValues{};
            bool materialFxValuesInitialized = false;
            bool hasResolvedMaterialFxProfile = false;
            MaterialFxProfile resolvedMaterialFxProfile{};
            bool receiveShadow = true;
            MeshRenderDebugMode renderDebugMode = MeshRenderDebugMode::Normal;
        };

        struct VariantKeyHasher {
            size_t operator()(const VFX::VariantKey& key) const noexcept {
                size_t seed = std::hash<std::string>{}(key.shaderId);
                seed ^= std::hash<std::string>{}(key.vertexShaderId) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= std::hash<std::string>{}(key.pixelShaderId) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= static_cast<size_t>(key.featureBits) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= static_cast<size_t>(key.composite) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= static_cast<size_t>(key.depthTest) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= static_cast<size_t>(key.depthWrite) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                seed ^= static_cast<size_t>(key.doubleSided) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                return seed;
            }
        };

        struct State {
            bool initialized = false;
            ComPtr<ID3D12RootSignature> rootSig;
            ComPtr<ID3D12RootSignature> skinnedRootSig;
            ComPtr<ID3D12PipelineState> pso;
            ComPtr<ID3D12PipelineState> skinnedPso;
            ComPtr<ID3DBlob> vsBlob;
            ComPtr<ID3DBlob> skinnedVsBlob;
            ComPtr<ID3DBlob> psBlob;
            ComPtr<ID3D12Resource> cameraCB;
            ComPtr<ID3D12Resource> objectCB;
            ComPtr<ID3D12Resource> lightCB;
            ComPtr<ID3D12Resource> shadowCB;
            ComPtr<ID3D12Resource> skyEnvironmentCB;
            ComPtr<ID3D12Resource> jointPaletteCB;
            CameraCB* cameraMapped = nullptr;
            ObjectCB* objectMapped = nullptr;
            LightCB* lightMapped = nullptr;
            ShadowCB* shadowMapped = nullptr;
            SkyEnvironmentCB* skyEnvironmentMapped = nullptr;
            JointPaletteCB* jointPaletteMapped = nullptr;
            std::vector<DrawItem> drawItems;
            MeshRendererDebugStats debugStats;
            int fallbackTextureHandle = -1;
            int fallbackNormalTextureHandle = -1;
            int fallbackBlackTextureHandle = -1;
            std::unordered_map<VFX::VariantKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, VariantKeyHasher> variantPsoCache;
            std::unordered_map<VFX::VariantKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, VariantKeyHasher> skinnedVariantPsoCache;
            std::unordered_map<VFX::VariantKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, VariantKeyHasher> wireVariantPsoCache;
            std::unordered_map<VFX::VariantKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, VariantKeyHasher> skinnedWireVariantPsoCache;
            std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> psBlobCache;
            std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> vsBlobCache;
            std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>> primitiveMeshCache;
            std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>> primitiveSkinnedMeshCache;
            std::unordered_map<std::string, int> materialTextureCache;
            float elapsedTimeSec = 0.0f;
        };

        State g;

        bool CreateBuffers(ID3D12Device* device) {
            const UINT cameraBytes = AlignConstantBufferSize(sizeof(CameraCB));
            const UINT objectBytes = AlignConstantBufferSize(sizeof(ObjectCB)) * kMaxObjectCount;
            const UINT lightBytes = AlignConstantBufferSize(sizeof(LightCB));
            const UINT shadowBytes = AlignConstantBufferSize(sizeof(ShadowCB));
            const UINT skyEnvironmentBytes = AlignConstantBufferSize(sizeof(SkyEnvironmentCB));
            const UINT jointPaletteStride = AlignConstantBufferSize(sizeof(JointPaletteCB));
            const UINT jointPaletteBytes = jointPaletteStride * kMaxObjectCount;

            auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto cameraDesc = CD3DX12_RESOURCE_DESC::Buffer(cameraBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &cameraDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.cameraCB.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.cameraCB->Map(0, nullptr, reinterpret_cast<void**>(&g.cameraMapped)))) {
                return false;
            }

            auto objectDesc = CD3DX12_RESOURCE_DESC::Buffer(objectBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &objectDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.objectCB.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.objectCB->Map(0, nullptr, reinterpret_cast<void**>(&g.objectMapped)))) {
                return false;
            }
            auto lightDesc = CD3DX12_RESOURCE_DESC::Buffer(lightBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &lightDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.lightCB.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.lightCB->Map(0, nullptr, reinterpret_cast<void**>(&g.lightMapped)))) {
                return false;
            }
            auto shadowDesc = CD3DX12_RESOURCE_DESC::Buffer(shadowBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &shadowDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.shadowCB.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.shadowCB->Map(0, nullptr, reinterpret_cast<void**>(&g.shadowMapped)))) {
                return false;
            }
            auto skyEnvironmentDesc = CD3DX12_RESOURCE_DESC::Buffer(skyEnvironmentBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &skyEnvironmentDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.skyEnvironmentCB.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.skyEnvironmentCB->Map(0, nullptr, reinterpret_cast<void**>(&g.skyEnvironmentMapped)))) {
                return false;
            }
            auto jointPaletteDesc = CD3DX12_RESOURCE_DESC::Buffer(jointPaletteBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &jointPaletteDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.jointPaletteCB.GetAddressOf())))) {
                return false;
            }
            if (FAILED(g.jointPaletteCB->Map(0, nullptr, reinterpret_cast<void**>(&g.jointPaletteMapped)))) {
                return false;
            }
            return true;
        }

        bool CreatePipeline(ID3D12Device* device) {
            UINT flags = 0;
#if defined(_DEBUG)
            flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
            ComPtr<ID3DBlob> err;
            if (FAILED(D3DCompileFromFile(L"HIKARI/Shaders/Render3D_StaticVS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", flags, 0, g.vsBlob.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }
            err.Reset();
            if (FAILED(D3DCompileFromFile(L"HIKARI/Shaders/Render3D_SkinnedVS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", flags, 0, g.skinnedVsBlob.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }
            err.Reset();
            if (FAILED(D3DCompileFromFile(L"HIKARI/Shaders/Render3D_StaticPS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", flags, 0, g.psBlob.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }

            D3D12_DESCRIPTOR_RANGE textureRange{};
            textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            textureRange.NumDescriptors = 1;
            textureRange.BaseShaderRegister = 0;//t0
            textureRange.RegisterSpace = 0;
            textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_DESCRIPTOR_RANGE normalTextureRange{};
            normalTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            normalTextureRange.NumDescriptors = 1;
            normalTextureRange.BaseShaderRegister = 1;//t1
            normalTextureRange.RegisterSpace = 0;
            normalTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_DESCRIPTOR_RANGE shadowTextureRange{};
            shadowTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            shadowTextureRange.NumDescriptors = 1;
            shadowTextureRange.BaseShaderRegister = 2;//t2
            shadowTextureRange.RegisterSpace = 0;
            shadowTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_DESCRIPTOR_RANGE emissiveTextureRange{};
            emissiveTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            emissiveTextureRange.NumDescriptors = 1;
            emissiveTextureRange.BaseShaderRegister = 3;//t3
            emissiveTextureRange.RegisterSpace = 0;
            emissiveTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_DESCRIPTOR_RANGE metallicRoughnessTextureRange{};
            metallicRoughnessTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            metallicRoughnessTextureRange.NumDescriptors = 1;
            metallicRoughnessTextureRange.BaseShaderRegister = 4;//t4
            metallicRoughnessTextureRange.RegisterSpace = 0;
            metallicRoughnessTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_DESCRIPTOR_RANGE occlusionTextureRange{};
            occlusionTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            occlusionTextureRange.NumDescriptors = 1;
            occlusionTextureRange.BaseShaderRegister = 5;//t5
            occlusionTextureRange.RegisterSpace = 0;
            occlusionTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

			D3D12_DESCRIPTOR_RANGE skyCubeTextureRange{};
			skyCubeTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            skyCubeTextureRange.NumDescriptors = 1;
			skyCubeTextureRange.BaseShaderRegister = 6;//t6
			skyCubeTextureRange.RegisterSpace = 0;
			skyCubeTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER params[12]{};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[0].Descriptor.ShaderRegister = 0;
            params[0].Descriptor.RegisterSpace = 0;

            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[1].Descriptor.ShaderRegister = 1;
            params[1].Descriptor.RegisterSpace = 0;

            params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[2].Descriptor.ShaderRegister = 2;
            params[2].Descriptor.RegisterSpace = 0;

            params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[3].DescriptorTable.NumDescriptorRanges = 1;
            params[3].DescriptorTable.pDescriptorRanges = &textureRange;

            params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[4].DescriptorTable.NumDescriptorRanges = 1;
            params[4].DescriptorTable.pDescriptorRanges = &normalTextureRange;

            params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[5].DescriptorTable.NumDescriptorRanges = 1;
            params[5].DescriptorTable.pDescriptorRanges = &shadowTextureRange;

            params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[6].Descriptor.ShaderRegister = 4;
            params[6].Descriptor.RegisterSpace = 0;

            params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[7].DescriptorTable.NumDescriptorRanges = 1;
            params[7].DescriptorTable.pDescriptorRanges = &emissiveTextureRange;

            params[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[8].DescriptorTable.NumDescriptorRanges = 1;
            params[8].DescriptorTable.pDescriptorRanges = &metallicRoughnessTextureRange;

            params[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[9].DescriptorTable.NumDescriptorRanges = 1;
            params[9].DescriptorTable.pDescriptorRanges = &occlusionTextureRange;

            params[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[10].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[10].Descriptor.ShaderRegister = 5;
            params[10].Descriptor.RegisterSpace = 0;

            params[11].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[11].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[11].DescriptorTable.NumDescriptorRanges = 1;
            params[11].DescriptorTable.pDescriptorRanges = &skyCubeTextureRange;

            D3D12_STATIC_SAMPLER_DESC linearWrapSampler{};
            linearWrapSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            linearWrapSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            linearWrapSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            linearWrapSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            linearWrapSampler.ShaderRegister = 0;
            linearWrapSampler.RegisterSpace = 0;
            linearWrapSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            linearWrapSampler.MaxLOD = D3D12_FLOAT32_MAX;

            D3D12_STATIC_SAMPLER_DESC shadowSampler{};
            shadowSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            shadowSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            shadowSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            shadowSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            shadowSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
            shadowSampler.ShaderRegister = 1;
            shadowSampler.RegisterSpace = 0;
            shadowSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            shadowSampler.MaxLOD = D3D12_FLOAT32_MAX;

            const D3D12_STATIC_SAMPLER_DESC staticSamplers[] = {
                linearWrapSampler,
                shadowSampler
            };

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = static_cast<UINT>(std::size(params));
            rsDesc.pParameters = params;
            rsDesc.NumStaticSamplers = static_cast<UINT>(std::size(staticSamplers));
            rsDesc.pStaticSamplers = staticSamplers;
            rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ComPtr<ID3DBlob> sigBlob;
            ComPtr<ID3DBlob> errBlob;
            if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, sigBlob.GetAddressOf(), errBlob.GetAddressOf()))) {
                if (errBlob) OutputDebugStringA(static_cast<const char*>(errBlob->GetBufferPointer()));
                return false;
            }

            if (FAILED(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(g.rootSig.GetAddressOf())))) {
                return false;
            }

            D3D12_ROOT_PARAMETER skinnedParams[13]{};
            for (size_t i = 0; i < std::size(params); ++i) {
                skinnedParams[i] = params[i];
            }
            skinnedParams[12].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            skinnedParams[12].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            skinnedParams[12].Descriptor.ShaderRegister = 3;
            skinnedParams[12].Descriptor.RegisterSpace = 0;

            D3D12_ROOT_SIGNATURE_DESC skinnedRsDesc = rsDesc;
            skinnedRsDesc.NumParameters = static_cast<UINT>(std::size(skinnedParams));
            skinnedRsDesc.pParameters = skinnedParams;

            sigBlob.Reset();
            errBlob.Reset();
            if (FAILED(D3D12SerializeRootSignature(&skinnedRsDesc, D3D_ROOT_SIGNATURE_VERSION_1, sigBlob.GetAddressOf(), errBlob.GetAddressOf()))) {
                if (errBlob) OutputDebugStringA(static_cast<const char*>(errBlob->GetBufferPointer()));
                return false;
            }
            if (FAILED(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(g.skinnedRootSig.GetAddressOf())))) {
                return false;
            }

            const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, normal)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, tangent)),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, static_cast<UINT>(offsetof(VertexStatic3D, u)),        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = g.rootSig.Get();
            psoDesc.VS = { g.vsBlob->GetBufferPointer(), g.vsBlob->GetBufferSize() };
            psoDesc.PS = { g.psBlob->GetBufferPointer(), g.psBlob->GetBufferSize() };
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = TRUE;
            psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            psoDesc.InputLayout = { inputElements, static_cast<UINT>(std::size(inputElements)) };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = 1;

            if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(g.pso.GetAddressOf())))) {
                return false;
            }

            const D3D12_INPUT_ELEMENT_DESC skinnedInputElements[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, normal)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, tangent)),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, uv0)),      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, uv1)),      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, color0)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "JOINTS",   0, DXGI_FORMAT_R16G16B16A16_UINT,  0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, joints)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "WEIGHTS",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, weights)),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedPsoDesc = psoDesc;
            skinnedPsoDesc.pRootSignature = g.skinnedRootSig.Get();
            skinnedPsoDesc.VS = { g.skinnedVsBlob->GetBufferPointer(), g.skinnedVsBlob->GetBufferSize() };
            skinnedPsoDesc.InputLayout = { skinnedInputElements, static_cast<UINT>(std::size(skinnedInputElements)) };
            return SUCCEEDED(device->CreateGraphicsPipelineState(&skinnedPsoDesc, IID_PPV_ARGS(g.skinnedPso.GetAddressOf())));
        }

        std::wstring ResolveShaderPath(const std::string& shaderId, const wchar_t* defaultFile) {
            if (shaderId.empty() || shaderId == "PBR" || shaderId == "StaticLit") {
                return defaultFile;
            }
            if (shaderId == "StaticFx" || shaderId == "MaterialFx") {
                return L"HIKARI/Shaders/Render3D_StaticFxPS.hlsl";
            }
            std::wstring path = L"HIKARI/Shaders/";
            path += std::wstring(shaderId.begin(), shaderId.end());
            path += L".hlsl";
            return path;
        }

        bool LoadPixelShaderBlob(const std::string& shaderProfileId, ID3DBlob** outBlob) {
            const std::string cacheKey = shaderProfileId.empty() ? "StaticLit" : shaderProfileId;
            auto it = g.psBlobCache.find(cacheKey);
            if (it != g.psBlobCache.end()) {
                *outBlob = it->second.Get();
                return true;
            }

            UINT flags = 0;
#if defined(_DEBUG)
            flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
            ComPtr<ID3DBlob> blob;
            ComPtr<ID3DBlob> err;
            const std::wstring path = ResolveShaderPath(cacheKey, L"HIKARI/Shaders/Render3D_StaticPS.hlsl");
            if (FAILED(D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", flags, 0, blob.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                DEBUGLOG::PushRenderError(std::string("[MeshRenderer][MaterialFx][WARN] Pixel shader compile failed. shaderId=") + cacheKey + " fallback used");
                return false;
            }
            auto [insertIt, _] = g.psBlobCache.emplace(cacheKey, blob);
            *outBlob = insertIt->second.Get();
            return true;
        }

        bool LoadVertexShaderBlob(const std::string& vertexShaderId, ID3DBlob** outBlob) {
            const std::string cacheKey = vertexShaderId.empty() ? "Render3D_StaticVS" : vertexShaderId;
            auto it = g.vsBlobCache.find(cacheKey);
            if (it != g.vsBlobCache.end()) {
                *outBlob = it->second.Get();
                return true;
            }

            UINT flags = 0;
#if defined(_DEBUG)
            flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
            ComPtr<ID3DBlob> blob;
            ComPtr<ID3DBlob> err;
            const std::wstring path = ResolveShaderPath(cacheKey, L"HIKARI/Shaders/Render3D_StaticVS.hlsl");
            if (FAILED(D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", flags, 0, blob.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                DEBUGLOG::PushRenderError(std::string("[MeshRenderer][MaterialFx][WARN] Vertex shader compile failed. shaderId=") + cacheKey + " fallback used");
                return false;
            }
            auto [insertIt, _] = g.vsBlobCache.emplace(cacheKey, blob);
            *outBlob = insertIt->second.Get();
            return true;
        }

        bool CreateVariantPipeline(ID3D12Device* device, const VFX::VariantKey& key, bool wireframe, ID3D12PipelineState** outPso) {
            ID3DBlob* vsBlob = nullptr;
            if (!LoadVertexShaderBlob(key.vertexShaderId, &vsBlob) || vsBlob == nullptr) {
                vsBlob = g.vsBlob.Get();
            }
            ID3DBlob* psBlob = nullptr;
            const std::string psId = !key.pixelShaderId.empty() ? key.pixelShaderId : key.shaderId;
            if (!LoadPixelShaderBlob(psId, &psBlob) || psBlob == nullptr) {
                return false;
            }

            const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, normal)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, tangent)),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, static_cast<UINT>(offsetof(VertexStatic3D, u)),        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = g.rootSig.Get();
            psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
            psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            if (key.composite == VFX::CompositeMode::Additive) {
                D3D12_RENDER_TARGET_BLEND_DESC& rt0 = psoDesc.BlendState.RenderTarget[0];
                rt0.BlendEnable = TRUE;
                rt0.SrcBlend = D3D12_BLEND_ONE;
                rt0.DestBlend = D3D12_BLEND_ONE;
                rt0.BlendOp = D3D12_BLEND_OP_ADD;
                rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
                rt0.DestBlendAlpha = D3D12_BLEND_ONE;
                rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            }
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.FillMode = wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = key.doubleSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = key.depthTest ? TRUE : FALSE;
            psoDesc.DepthStencilState.DepthWriteMask = key.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            psoDesc.InputLayout = { inputElements, static_cast<UINT>(std::size(inputElements)) };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = 1;
            return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(outPso)));
        }

        bool CreateSkinnedVariantPipeline(ID3D12Device* device, const VFX::VariantKey& key, bool wireframe, ID3D12PipelineState** outPso) {
            ID3DBlob* psBlob = nullptr;
            const std::string psId = !key.pixelShaderId.empty() ? key.pixelShaderId : key.shaderId;
            if (!LoadPixelShaderBlob(psId, &psBlob) || psBlob == nullptr || g.skinnedVsBlob == nullptr) {
                return false;
            }
            if (!key.vertexShaderId.empty()) {
                DEBUGLOG::PushRenderError(std::string("[MeshRenderer][MaterialFx][WARN] Custom vertexShaderId is ignored for skinned mesh. vertexShaderId=") + key.vertexShaderId);
            }

            const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, normal)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, tangent)),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, uv0)),      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, uv1)),      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, color0)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "JOINTS",   0, DXGI_FORMAT_R16G16B16A16_UINT,  0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, joints)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "WEIGHTS",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, weights)),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = g.skinnedRootSig.Get();
            psoDesc.VS = { g.skinnedVsBlob->GetBufferPointer(), g.skinnedVsBlob->GetBufferSize() };
            psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            if (key.composite == VFX::CompositeMode::Additive) {
                D3D12_RENDER_TARGET_BLEND_DESC& rt0 = psoDesc.BlendState.RenderTarget[0];
                rt0.BlendEnable = TRUE;
                rt0.SrcBlend = D3D12_BLEND_ONE;
                rt0.DestBlend = D3D12_BLEND_ONE;
                rt0.BlendOp = D3D12_BLEND_OP_ADD;
                rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
                rt0.DestBlendAlpha = D3D12_BLEND_ONE;
                rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            }
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.FillMode = wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = key.doubleSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_NONE;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = key.depthTest ? TRUE : FALSE;
            psoDesc.DepthStencilState.DepthWriteMask = key.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            psoDesc.InputLayout = { inputElements, static_cast<UINT>(std::size(inputElements)) };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = 1;
            return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(outPso)));
        }

        ID3D12PipelineState* GetOrCreateVariantPso(const VFX::VariantKey& key, bool skinned, bool wireframe) {
            auto& cache = skinned
                ? (wireframe ? g.skinnedWireVariantPsoCache : g.skinnedVariantPsoCache)
                : (wireframe ? g.wireVariantPsoCache : g.variantPsoCache);

            auto found = cache.find(key);
            if (found == cache.end()) {
                ++g.debugStats.psoCacheMissCount;
                ComPtr<ID3D12PipelineState> variantPso;
                const bool created = skinned
                    ? CreateSkinnedVariantPipeline(SERVICES::gCtx.device, key, wireframe, variantPso.GetAddressOf())
                    : CreateVariantPipeline(SERVICES::gCtx.device, key, wireframe, variantPso.GetAddressOf());
                if (!created) {
                    variantPso = skinned ? (g.skinnedPso ? g.skinnedPso : g.pso) : g.pso;
                }
                found = cache.emplace(key, std::move(variantPso)).first;
            } else {
                ++g.debugStats.psoCacheHitCount;
            }

            return found->second.Get();
        }

        void ApplyProfileToVariant(const MaterialFxProfile& profile, VFX::VariantKey& variant) {
            if (!profile.shaderProfileId.empty()) {
                variant.shaderId = profile.shaderProfileId;
            }
            if (!profile.vertexShaderId.empty()) {
                variant.vertexShaderId = profile.vertexShaderId;
            }
            if (!profile.pixelShaderId.empty()) {
                variant.pixelShaderId = profile.pixelShaderId;
            }
            variant.featureBits = profile.featureBits;
            variant.composite = profile.composite;
            variant.depthTest = profile.depthTest;
            variant.depthWrite = profile.depthWrite;
            variant.doubleSided = profile.doubleSided;
        }

        void ApplyMaterialFxOverride(DrawItem& item) {
            item.hasResolvedMaterialFxProfile = false;
            item.resolvedMaterialFxProfile = {};

            if (item.materialFxProfileId.empty()) {
                return;
            }

            MaterialFxProfile profile{};
            if (!MaterialFxProfile::LoadById(item.materialFxProfileId, profile)) {
                return;
            }

            item.hasResolvedMaterialFxProfile = true;
            item.resolvedMaterialFxProfile = std::move(profile);
            ApplyProfileToVariant(item.resolvedMaterialFxProfile, item.variant);
        }

        void ResolveDrawVariant(DrawItem& item) {
            if (!item.asset) {
                return;
            }
            const Material* material = item.asset->GetMaterial();
            if (material) {
                item.variant.shaderId = material->GetShaderProfileId();
                item.variant.featureBits = material->GetFeatureBits();
            }
            item.variant.composite = VFX::CompositeMode::Alpha;
            item.variant.depthTest = true;
            item.variant.depthWrite = true;
            item.variant.doubleSided = false;

            ApplyMaterialFxOverride(item);

            item.fxValues = {};
            item.fxFlags = 0;
            if (!item.materialFxValuesInitialized) {
                return;
            }
            for (size_t i = 0; i < item.fxValues.size(); ++i) {
                const DirectX::XMFLOAT4& value = item.materialFxParamValues[i];
                item.fxValues[i] = { value.x, value.y, value.z, value.w };
            }
            item.fxFlags = item.variant.featureBits;
        }

        VFX::VariantKey ResolvePrimitiveVariant(const DrawItem& item, const MaterialAsset* materialAsset) {
            VFX::VariantKey variant = item.variant;

            if (materialAsset != nullptr) {
                // glTF material provides the primitive default.
                variant.shaderId = materialAsset->shaderProfileId;
                variant.featureBits = materialAsset->featureBits;
                variant.doubleSided = materialAsset->doubleSided;
            }

            // Object-level MaterialFx should override primitive material defaults.
            if (item.hasResolvedMaterialFxProfile) {
                ApplyProfileToVariant(item.resolvedMaterialFxProfile, variant);
            }

            return variant;
        }

        bool EnsureInitialized() {
            if (g.initialized) {
                return true;
            }
            auto* device = SERVICES::gCtx.device;
            if (!device) {
                return false;
            }

            if (!CreateBuffers(device)) {
                return false;
            }
            if (!CreatePipeline(device)) {
                return false;
            }
            g.fallbackTextureHandle = DXTEX::DxTextureManager::LoadTexture("mesh_renderer/fallback_white", "HIKARI/white1x1.png");
            g.fallbackNormalTextureHandle = DXTEX::DxTextureManager::LoadTexture("mesh_renderer/fallback_normal", "HIKARI/normal_flat_1x1.png");
            if (g.fallbackNormalTextureHandle < 0) {
                g.fallbackNormalTextureHandle = g.fallbackTextureHandle;
            }
            g.fallbackBlackTextureHandle = g.fallbackTextureHandle;
            g.psBlobCache["StaticLit"] = g.psBlob;

            g.initialized = true;
            return true;
        }

        const MaterialAsset* GetPrimitiveMaterial(const ModelAsset& asset, uint32_t materialIndex) {
            if (materialIndex >= asset.materials.size()) {
                return nullptr;
            }
            return &asset.materials[materialIndex];
        }

        int ResolvePrimitiveTextureHandle(const ModelAsset& asset, const MaterialAsset* materialAsset) {
            if (materialAsset == nullptr) {
                return g.fallbackTextureHandle;
            }

            const int textureIndex = materialAsset->baseColorTexture.textureIndex;
            if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
                return g.fallbackTextureHandle;
            }

            const std::string& texturePath = asset.textures[static_cast<size_t>(textureIndex)].sourcePath;
            if (texturePath.empty()) {
                return g.fallbackTextureHandle;
            }

            auto found = g.materialTextureCache.find(texturePath);
            if (found != g.materialTextureCache.end()) {
                ++g.debugStats.materialTextureCacheHitCount;
                return found->second;
            }

            ++g.debugStats.materialTextureCacheMissCount;
            const int handle = DXTEX::DxTextureManager::LoadTexture("model_material/" + texturePath, texturePath);
            g.materialTextureCache[texturePath] = handle;
            return handle >= 0 ? handle : g.fallbackTextureHandle;
        }

        int ResolvePrimitiveNormalTextureHandle(const ModelAsset& asset, const MaterialAsset* materialAsset) {
            if (materialAsset == nullptr) {
                return g.fallbackNormalTextureHandle;
            }

            const int textureIndex = materialAsset->normalTexture.textureIndex;
            if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
                ++g.debugStats.normalMapFallbackCount;
                return g.fallbackNormalTextureHandle;
            }

            const std::string& texturePath = asset.textures[static_cast<size_t>(textureIndex)].sourcePath;
            if (texturePath.empty()) {
                ++g.debugStats.normalMapFallbackCount;
                return g.fallbackNormalTextureHandle;
            }

            const std::string cacheKey = "normal:" + texturePath;
            auto found = g.materialTextureCache.find(cacheKey);
            if (found != g.materialTextureCache.end()) {
                ++g.debugStats.normalTextureCacheHitCount;
                return found->second >= 0 ? found->second : g.fallbackNormalTextureHandle;
            }

            ++g.debugStats.normalTextureCacheMissCount;
            const int handle = DXTEX::DxTextureManager::LoadTexture("model_material/normal/" + texturePath, texturePath);
            g.materialTextureCache[cacheKey] = handle;
            return handle >= 0 ? handle : g.fallbackNormalTextureHandle;
        }

        int ResolvePrimitiveEmissiveTextureHandle(const ModelAsset& asset, const MaterialAsset* materialAsset) {
            if (materialAsset == nullptr) {
                return g.fallbackBlackTextureHandle;
            }

            const int textureIndex = materialAsset->emissiveTexture.textureIndex;
            if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
                ++g.debugStats.emissiveMapFallbackCount;
                return g.fallbackBlackTextureHandle;
            }

            const std::string& texturePath = asset.textures[static_cast<size_t>(textureIndex)].sourcePath;
            if (texturePath.empty()) {
                ++g.debugStats.emissiveMapFallbackCount;
                return g.fallbackBlackTextureHandle;
            }

            const std::string cacheKey = "emissive:" + texturePath;
            auto found = g.materialTextureCache.find(cacheKey);
            if (found != g.materialTextureCache.end()) {
                ++g.debugStats.emissiveTextureCacheHitCount;
                return found->second >= 0 ? found->second : g.fallbackBlackTextureHandle;
            }

            ++g.debugStats.emissiveTextureCacheMissCount;
            const int handle = DXTEX::DxTextureManager::LoadTexture("model_material/emissive/" + texturePath, texturePath);
            g.materialTextureCache[cacheKey] = handle;
            return handle >= 0 ? handle : g.fallbackBlackTextureHandle;
        }

        int ResolvePrimitiveMetallicRoughnessTextureHandle(const ModelAsset& asset, const MaterialAsset* materialAsset) {
            if (materialAsset == nullptr) {
                ++g.debugStats.metallicRoughnessFallbackCount;
                return g.fallbackTextureHandle;
            }

            const int textureIndex = materialAsset->metallicRoughnessTexture.textureIndex;
            if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
                ++g.debugStats.metallicRoughnessFallbackCount;
                return g.fallbackTextureHandle;
            }

            const std::string& texturePath = asset.textures[static_cast<size_t>(textureIndex)].sourcePath;
            if (texturePath.empty()) {
                ++g.debugStats.metallicRoughnessFallbackCount;
                return g.fallbackTextureHandle;
            }

            const std::string cacheKey = "metallicRoughness:" + texturePath;
            auto found = g.materialTextureCache.find(cacheKey);
            if (found != g.materialTextureCache.end()) {
                ++g.debugStats.metallicRoughnessTextureCacheHitCount;
                return found->second >= 0 ? found->second : g.fallbackTextureHandle;
            }

            ++g.debugStats.metallicRoughnessTextureCacheMissCount;
            const int handle = DXTEX::DxTextureManager::LoadTexture("model_material/metallic_roughness/" + texturePath, texturePath);
            g.materialTextureCache[cacheKey] = handle;
            if (handle < 0) {
                DEBUGLOG::PushRenderError(std::string("[MeshRenderer][PBRTexture][WARN] metallicRoughness texture failed. material=") +
                    materialAsset->name + " sourcePath=" + texturePath + " fallback used");
            }
            return handle >= 0 ? handle : g.fallbackTextureHandle;
        }

        int ResolvePrimitiveOcclusionTextureHandle(const ModelAsset& asset, const MaterialAsset* materialAsset) {
            if (materialAsset == nullptr) {
                ++g.debugStats.occlusionFallbackCount;
                return g.fallbackTextureHandle;
            }

            const int textureIndex = materialAsset->occlusionTexture.textureIndex;
            if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
                ++g.debugStats.occlusionFallbackCount;
                return g.fallbackTextureHandle;
            }

            const std::string& texturePath = asset.textures[static_cast<size_t>(textureIndex)].sourcePath;
            if (texturePath.empty()) {
                ++g.debugStats.occlusionFallbackCount;
                return g.fallbackTextureHandle;
            }

            const std::string cacheKey = "occlusion:" + texturePath;
            auto found = g.materialTextureCache.find(cacheKey);
            if (found != g.materialTextureCache.end()) {
                ++g.debugStats.occlusionTextureCacheHitCount;
                return found->second >= 0 ? found->second : g.fallbackTextureHandle;
            }

            ++g.debugStats.occlusionTextureCacheMissCount;
            const int handle = DXTEX::DxTextureManager::LoadTexture("model_material/occlusion/" + texturePath, texturePath);
            g.materialTextureCache[cacheKey] = handle;
            if (handle < 0) {
                DEBUGLOG::PushRenderError(std::string("[MeshRenderer][PBRTexture][WARN] occlusion texture failed. material=") +
                    materialAsset->name + " sourcePath=" + texturePath + " fallback used");
            }
            return handle >= 0 ? handle : g.fallbackTextureHandle;
        }

        MATH::Vec4 SanitizeTangent(const MATH::Vec4& tangent) {
            const float lenSq =
                tangent.x * tangent.x +
                tangent.y * tangent.y +
                tangent.z * tangent.z;
            if (lenSq <= 1e-8f) {
                return { 1.0f, 0.0f, 0.0f, 1.0f };
            }
            return tangent;
        }

        Mesh* GetOrCreatePrimitiveMesh(const MeshPrimitive& primitive) {
            auto found = g.primitiveMeshCache.find(&primitive);
            if (found != g.primitiveMeshCache.end()) {
                ++g.debugStats.primitiveMeshCacheHitCount;
                return found->second.get();
            }

            if (primitive.layout != VertexLayoutKind::StaticPNTT || primitive.staticVertices.empty() || primitive.indices.empty()) {
                return nullptr;
            }

            ++g.debugStats.primitiveMeshCacheMissCount;
            std::vector<VertexStatic3D> vertices;
            vertices.reserve(primitive.staticVertices.size());
            for (const Vertex3D& src : primitive.staticVertices) {
                VertexStatic3D dst{};
                dst.position = src.position;
                dst.normal = src.normal;
                dst.tangent = SanitizeTangent(src.tangent);
                dst.u = src.uv0.x;
                dst.v = src.uv0.y;
                vertices.push_back(dst);
            }

            auto mesh = std::make_unique<Mesh>();
            if (!mesh->CreateStatic(SERVICES::gCtx.device, vertices, primitive.indices)) {
                return nullptr;
            }

            Mesh* raw = mesh.get();
            g.primitiveMeshCache.emplace(&primitive, std::move(mesh));
            return raw;
        }

        Mesh* GetOrCreateSkinnedPrimitiveMesh(const MeshPrimitive& primitive) {
            auto found = g.primitiveSkinnedMeshCache.find(&primitive);
            if (found != g.primitiveSkinnedMeshCache.end()) {
                ++g.debugStats.primitiveSkinnedMeshCacheHitCount;
                return found->second.get();
            }

            if (primitive.skinnedVertices.empty() || primitive.indices.empty()) {
                return nullptr;
            }

            ++g.debugStats.primitiveSkinnedMeshCacheMissCount;
            std::vector<VertexSkinnedGpu3D> vertices;
            vertices.reserve(primitive.skinnedVertices.size());
            for (const SkinnedVertex3D& src : primitive.skinnedVertices) {
                VertexSkinnedGpu3D dst{};
                dst.position = src.position;
                dst.normal = src.normal;
                dst.tangent = src.tangent;
                dst.uv0 = src.uv0;
                dst.uv1 = src.uv1;
                dst.color0 = src.color0;
                for (size_t i = 0; i < 4; ++i) {
                    dst.joints[i] = src.joints[i];
                    dst.weights[i] = src.weights[i];
                }
                vertices.push_back(dst);
            }

            auto mesh = std::make_unique<Mesh>();
            if (!mesh->CreateSkinned(SERVICES::gCtx.device, vertices, primitive.indices)) {
                return nullptr;
            }

            Mesh* raw = mesh.get();
            g.primitiveSkinnedMeshCache.emplace(&primitive, std::move(mesh));
            return raw;
        }

        size_t UploadJointPalette(size_t objectIndex, const std::vector<MATH::Mat4>& jointPalette) {
            if (g.jointPaletteMapped == nullptr || g.jointPaletteCB == nullptr) {
                return 0;
            }

            constexpr UINT kJointPaletteStride = AlignConstantBufferSize(sizeof(JointPaletteCB));
            uint8_t* dst = reinterpret_cast<uint8_t*>(g.jointPaletteMapped) + static_cast<size_t>(kJointPaletteStride) * objectIndex;
            JointPaletteCB paletteCb{};
            for (MATH::Mat4& jointMatrix : paletteCb.jointMatrices) {
                jointMatrix = MATH::Mat4::Identity();
            }

            const size_t uploadedCount = std::min(jointPalette.size(), kMaxJointPaletteMatrices);
            for (size_t i = 0; i < uploadedCount; ++i) {
                paletteCb.jointMatrices[i] = jointPalette[i];
            }
            std::memcpy(dst, &paletteCb, sizeof(JointPaletteCB));
            return uploadedCount;
        }

        MATH::Mat4 BuildNormalMatrixFromWorld(const MATH::Mat4& world) {
            const float a00 = world.m[0][0];
            const float a01 = world.m[1][0];
            const float a02 = world.m[2][0];
            const float a10 = world.m[0][1];
            const float a11 = world.m[1][1];
            const float a12 = world.m[2][1];
            const float a20 = world.m[0][2];
            const float a21 = world.m[1][2];
            const float a22 = world.m[2][2];

            const float det =
                a00 * (a11 * a22 - a12 * a21) -
                a01 * (a10 * a22 - a12 * a20) +
                a02 * (a10 * a21 - a11 * a20);
            if (std::abs(det) <= 1e-6f) {
                return MATH::Mat4::Identity();
            }

            const float invDet = 1.0f / det;
            const float inv00 = (a11 * a22 - a12 * a21) * invDet;
            const float inv01 = (a02 * a21 - a01 * a22) * invDet;
            const float inv02 = (a01 * a12 - a02 * a11) * invDet;
            const float inv10 = (a12 * a20 - a10 * a22) * invDet;
            const float inv11 = (a00 * a22 - a02 * a20) * invDet;
            const float inv12 = (a02 * a10 - a00 * a12) * invDet;
            const float inv20 = (a10 * a21 - a11 * a20) * invDet;
            const float inv21 = (a01 * a20 - a00 * a21) * invDet;
            const float inv22 = (a00 * a11 - a01 * a10) * invDet;

            MATH::Mat4 normalMatrix = MATH::Mat4::Identity();
            normalMatrix.m[0][0] = inv00;
            normalMatrix.m[0][1] = inv01;
            normalMatrix.m[0][2] = inv02;
            normalMatrix.m[1][0] = inv10;
            normalMatrix.m[1][1] = inv11;
            normalMatrix.m[1][2] = inv12;
            normalMatrix.m[2][0] = inv20;
            normalMatrix.m[2][1] = inv21;
            normalMatrix.m[2][2] = inv22;
            return normalMatrix;
        }

        MATH::Mat4 BuildNormalMatrix(const Transform3D& transform) {
            if (transform.useExplicitMatrix) {
                return BuildNormalMatrixFromWorld(transform.GetWorldMatrix());
            }

            MATH::Mat4 normalMatrix = MATH::Mat4::Rotate(MATH::NormalizeQ(transform.rotation));
            const MATH::Vec3 s = transform.scale;
            const float invScaleX = (std::abs(s.x) > 1e-6f) ? (1.0f / s.x) : 0.0f;
            const float invScaleY = (std::abs(s.y) > 1e-6f) ? (1.0f / s.y) : 0.0f;
            const float invScaleZ = (std::abs(s.z) > 1e-6f) ? (1.0f / s.z) : 0.0f;
            normalMatrix.m[0][0] *= invScaleX; normalMatrix.m[0][1] *= invScaleX; normalMatrix.m[0][2] *= invScaleX;
            normalMatrix.m[1][0] *= invScaleY; normalMatrix.m[1][1] *= invScaleY; normalMatrix.m[1][2] *= invScaleY;
            normalMatrix.m[2][0] *= invScaleZ; normalMatrix.m[2][1] *= invScaleZ; normalMatrix.m[2][2] *= invScaleZ;
            return normalMatrix;
        }

        void FillFxValues(ObjectCB& obj, const DrawItem& item) {
            obj.fxFlags = item.fxFlags;
            obj.fxUser0 = item.fxValues[0];
            obj.fxUser1 = item.fxValues[1];
            obj.fxUser2 = item.fxValues[2];
            obj.fxUser3 = item.fxValues[3];
        }

        void FillMaterialValues(ObjectCB& obj, const MaterialAsset* materialAsset, int normalTextureHandle, int emissiveTextureHandle, int metallicRoughnessTextureHandle, int occlusionTextureHandle) {
            obj.baseColor = materialAsset ? materialAsset->baseColorFactor : MATH::Vec4{ 1, 1, 1, 1 };
            obj.materialFlags = 0;
            obj.alphaCutoff = materialAsset ? materialAsset->alphaCutoff : 0.5f;
            obj.emissiveFactor = { 0.0f, 0.0f, 0.0f, 1.0f };
            obj.hasNormalTexture = 0;
            obj.hasEmissiveTexture = 0;
            obj.normalScale = materialAsset ? materialAsset->normalTexture.scale : 1.0f;
            obj.metallicFactor = materialAsset ? materialAsset->metallicFactor : 0.0f;
            obj.roughnessFactor = materialAsset ? materialAsset->roughnessFactor : 1.0f;
            obj.hasMetallicRoughnessTexture = 0;
            obj.hasOcclusionTexture = 0;
            obj.occlusionStrength = materialAsset ? materialAsset->occlusionTexture.strength : 1.0f;

            if (materialAsset == nullptr) {
                return;
            }

            if (normalTextureHandle >= 0 && normalTextureHandle != g.fallbackNormalTextureHandle) {
                obj.hasNormalTexture = 1;
                ++g.debugStats.normalMappedPrimitiveCount;
            }
            if (emissiveTextureHandle >= 0 && emissiveTextureHandle != g.fallbackBlackTextureHandle) {
                obj.hasEmissiveTexture = 1;
                ++g.debugStats.emissiveMappedPrimitiveCount;
            }
            if (metallicRoughnessTextureHandle >= 0 && metallicRoughnessTextureHandle != g.fallbackTextureHandle) {
                obj.hasMetallicRoughnessTexture = 1;
                ++g.debugStats.metallicRoughnessMappedPrimitiveCount;
            }
            if (occlusionTextureHandle >= 0 && occlusionTextureHandle != g.fallbackTextureHandle) {
                obj.hasOcclusionTexture = 1;
                ++g.debugStats.occlusionMappedPrimitiveCount;
            }

            if (materialAsset->alphaMode == AlphaMode::Mask) {
                obj.materialFlags |= MATERIAL_FEATURES::AlphaMask;
            }
            const bool isUnlit = (materialAsset->featureBits & MATERIAL_FEATURES::Unlit) != 0;
            if (isUnlit) {
                obj.materialFlags |= MATERIAL_FEATURES::Unlit;
                ++g.debugStats.unlitPrimitiveCount;
            }
            else {
                ++g.debugStats.pbrPrimitiveCount;
            }
            if ((materialAsset->featureBits & MATERIAL_FEATURES::Emissive) != 0) {
                obj.materialFlags |= MATERIAL_FEATURES::Emissive;
            }
            obj.emissiveFactor = {
                materialAsset->emissiveFactor.x,
                materialAsset->emissiveFactor.y,
                materialAsset->emissiveFactor.z,
                materialAsset->emissiveStrength
            };
        }

        void FillLightCB(const SceneEnvironment& environment, LightCB& out) {
            out = {};
            const SKYRENDERER::SkyEnvironmentData& skyData = SKYRENDERER::GetEnvironmentData();

            MATH::Vec3 dir = MATH::Normalize(environment.directional.direction);
            if (MATH::Length(dir) <= 1e-6f) {
                dir = { 0.0f, -1.0f, 0.0f };
            }

            out.directionalDir = { dir.x, dir.y, dir.z, 0.0f };
            out.directionalColor = { environment.directional.color.x, environment.directional.color.y, environment.directional.color.z, 1.0f };
            out.directionalIntensity = environment.directional.enabled ? std::max(0.0f, environment.directional.intensity) : 0.0f;

            MATH::Vec3 ambientColor = environment.ambient.color;
            if (environment.ambient.useSkyColor && skyData.valid) {
                const MATH::Vec3 skyAmbient = {
                    (skyData.horizonColor.x * 0.65f + skyData.groundColor.x * 0.35f) * skyData.exposure * skyData.ambientFromSky,
                    (skyData.horizonColor.y * 0.65f + skyData.groundColor.y * 0.35f) * skyData.exposure * skyData.ambientFromSky,
                    (skyData.horizonColor.z * 0.65f + skyData.groundColor.z * 0.35f) * skyData.exposure * skyData.ambientFromSky
                };
                const float blend = std::clamp(environment.ambient.skyBlend, 0.0f, 1.0f);
                ambientColor = {
                    ambientColor.x * (1.0f - blend) + skyAmbient.x * blend,
                    ambientColor.y * (1.0f - blend) + skyAmbient.y * blend,
                    ambientColor.z * (1.0f - blend) + skyAmbient.z * blend
                };
            }

            MATH::Vec3 fogColor = environment.fog.color;
            if (environment.fog.useSkyHorizonColor && skyData.valid) {
                fogColor = {
                    skyData.horizonColor.x * skyData.exposure,
                    skyData.horizonColor.y * skyData.exposure,
                    skyData.horizonColor.z * skyData.exposure
                };
            }

            out.ambientColor = { ambientColor.x, ambientColor.y, ambientColor.z, 1.0f };
            out.ambientIntensity = std::max(0.0f, environment.ambient.intensity);
            out.specularParams = {
                std::max(0.0f, environment.specularIntensity),
                std::max(1.0f, environment.specularPower),
                0.0f,
                0.0f
            };
            out.fogColorDensity = {
                fogColor.x,
                fogColor.y,
                fogColor.z,
                std::max(0.0f, environment.fog.density)
            };
            out.fogParams = {
                environment.fog.enabled ? 1.0f : 0.0f,
                std::max(0.0f, environment.fog.startDistance),
                std::max(0.1f, environment.fog.endDistance),
                std::max(0.0f, environment.fog.heightFalloff)
            };
            out.debugView = static_cast<uint32_t>(environment.debugView);

            constexpr uint32_t kMaxPointLights = 8;
            uint32_t uploadedCount = 0;
            uint32_t uploadableCount = 0;
            for (const PointLight& pointLight : environment.pointLights) {
                if (!pointLight.enabled || pointLight.range <= 0.0f) {
                    continue;
                }

                ++uploadableCount;
                if (uploadedCount >= kMaxPointLights) {
                    continue;
                }

                out.pointLightPosRange[uploadedCount] = {
                    pointLight.position.x,
                    pointLight.position.y,
                    pointLight.position.z,
                    pointLight.range
                };
                out.pointLightColorIntensity[uploadedCount] = {
                    pointLight.color.x,
                    pointLight.color.y,
                    pointLight.color.z,
                    std::max(0.0f, pointLight.intensity)
                };
                ++uploadedCount;
            }
            out.pointLightCount = uploadedCount;

            g.debugStats.directionalEnabled = environment.directional.enabled;
            g.debugStats.directionalIntensity = out.directionalIntensity;
            g.debugStats.ambientIntensity = out.ambientIntensity;
            g.debugStats.pointLightTotalCount = environment.pointLights.size();
            g.debugStats.pointLightUploadedCount = uploadedCount;
            g.debugStats.pointLightClampedCount = (uploadableCount > uploadedCount) ? (uploadableCount - uploadedCount) : 0u;
            g.debugStats.specularIntensity = out.specularParams.x;
            g.debugStats.specularPower = out.specularParams.y;
        }

        void FillSkyEnvironmentCB(SkyEnvironmentCB& out) {
            out = {};
            const SKYRENDERER::SkyEnvironmentData& skyData = SKYRENDERER::GetEnvironmentData();
            if (!skyData.valid) {
                return;
            }

            out.skyZenithExposure = {
                skyData.zenithColor.x,
                skyData.zenithColor.y,
                skyData.zenithColor.z,
                skyData.exposure
            };
            out.skyHorizonReflection = {
                skyData.horizonColor.x,
                skyData.horizonColor.y,
                skyData.horizonColor.z,
                skyData.reflectionIntensity
            };
            out.skyGroundAmbient = {
                skyData.groundColor.x,
                skyData.groundColor.y,
                skyData.groundColor.z,
                skyData.ambientFromSky
            };
            out.skyParams = {
                static_cast<float>(skyData.mode),
                skyData.hasCubemap ? 1.0f : 0.0f,
                skyData.horizonPower,
                skyData.yaw
            };
        }

        D3D12_GPU_DESCRIPTOR_HANDLE ResolveSkyCubeSrv() {
            const SKYRENDERER::SkyEnvironmentData& skyData = SKYRENDERER::GetEnvironmentData();
            if (skyData.valid && skyData.hasCubemap && skyData.cubemapSrv.ptr != 0) {
                return skyData.cubemapSrv;
            }

            return DXTEX::DxTextureManager::GetSrvGpuHandle(g.fallbackTextureHandle);
        }

        void FillShadowCB(const SceneEnvironment& environment, ShadowCB& out) {
            out = {};
            out.lightViewProj = SHADOW::GetDirectionalLightViewProj();
            out.enabled = (SHADOW::IsDirectionalShadowEnabled() && environment.directionalShadow.enabled) ? 1u : 0u;
            out.depthBias = std::max(0.0f, environment.directionalShadow.depthBias);
            out.normalBias = std::max(0.0f, environment.directionalShadow.normalBias);
            out.strength = std::clamp(environment.directionalShadow.strength, 0.0f, 1.0f);
            out.pcfEnabled = environment.directionalShadow.pcfEnabled ? 1u : 0u;
            out.pcfRadius = std::clamp(environment.directionalShadow.pcfRadius, 0.0f, 4.0f);
            const float resolution = static_cast<float>(std::max(1u, SHADOW::GetShadowResolution()));
            out.texelSizeX = 1.0f / resolution;
            out.texelSizeY = 1.0f / resolution;
        }
    }

    void Reset() {
        g.drawItems.clear();
        g.debugStats = {};
    }

    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4(&materialFxParamValues)[4], bool materialFxValuesInitialized, bool receiveShadow, MeshRenderDebugMode renderDebugMode) {
        DrawItem item{};
        item.asset = &asset;
        item.transform = transform;
        item.materialFxProfileId = materialFxProfileId;
        item.postGroupMask = postGroupMask;
        for (size_t i = 0; i < item.materialFxParamValues.size(); ++i) {
            item.materialFxParamValues[i] = materialFxParamValues[i];
        }
        item.materialFxValuesInitialized = materialFxValuesInitialized;
        item.receiveShadow = receiveShadow;
        item.renderDebugMode = renderDebugMode;
        ResolveDrawVariant(item);
        ++g.debugStats.staticDrawItemCount;
        if (renderDebugMode != MeshRenderDebugMode::Normal) {
            ++g.debugStats.wireDrawItemCount;
        }
        g.drawItems.push_back(std::move(item));
    }

    void SubmitSkinnedMesh(const ModelAsset& asset, const Transform3D& transform, const std::vector<MATH::Mat4>& jointPalette, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4(&materialFxParamValues)[4], bool materialFxValuesInitialized, bool receiveShadow, MeshRenderDebugMode renderDebugMode) {
        DrawItem item{};
        item.asset = &asset;
        item.transform = transform;
        item.jointPalette = jointPalette;
        item.materialFxProfileId = materialFxProfileId;
        item.postGroupMask = postGroupMask;
        for (size_t i = 0; i < item.materialFxParamValues.size(); ++i) {
            item.materialFxParamValues[i] = materialFxParamValues[i];
        }
        item.materialFxValuesInitialized = materialFxValuesInitialized;
        item.receiveShadow = receiveShadow;
        item.renderDebugMode = renderDebugMode;
        ResolveDrawVariant(item);
        ++g.debugStats.skinnedDrawItemCount;
        if (renderDebugMode != MeshRenderDebugMode::Normal) {
            ++g.debugStats.wireDrawItemCount;
        }
        g.drawItems.push_back(std::move(item));
    }

    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment) {
        if (g.drawItems.empty()) {
            return;
        }
        if (!EnsureInitialized()) {
            return;
        }

        auto* cmd = SERVICES::gCtx.cmdList;
        if (!cmd) {
            return;
        }

        g.cameraMapped->viewProj = camera.GetViewProj();
        const MATH::Vec3 cameraPos = camera.GetPosition();
        g.cameraMapped->cameraPos = { cameraPos.x, cameraPos.y, cameraPos.z, 1.0f };
        const FrameContext& frame = TIME::GetFrameContext();
        g.elapsedTimeSec += std::max(0.0f, frame.unscaledDt);
        g.cameraMapped->timeParams = { g.elapsedTimeSec, frame.unscaledDt, frame.gameDt, static_cast<float>(frame.frameIndex) };

        FillLightCB(environment, *g.lightMapped);
        FillShadowCB(environment, *g.shadowMapped);
        FillSkyEnvironmentCB(*g.skyEnvironmentMapped);

        cmd->SetGraphicsRootSignature(g.rootSig.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        cmd->SetGraphicsRootConstantBufferView(0, g.cameraCB->GetGPUVirtualAddress());
        cmd->SetGraphicsRootConstantBufferView(2, g.lightCB->GetGPUVirtualAddress());
        cmd->SetGraphicsRootConstantBufferView(6, g.shadowCB->GetGPUVirtualAddress());
        cmd->SetGraphicsRootConstantBufferView(10, g.skyEnvironmentCB->GetGPUVirtualAddress());
        ID3D12DescriptorHeap* srvHeap = DXTEX::DxTextureManager::GetSrvHeap();
        if (srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            cmd->SetDescriptorHeaps(1, heaps);
        }

        constexpr UINT kObjectStride = AlignConstantBufferSize(sizeof(ObjectCB));
        if (g.objectMapped == nullptr || g.objectCB == nullptr) {
            return;
        }

        size_t objectIndex = 0;
        for (const DrawItem& item : g.drawItems) {
            if (objectIndex >= kMaxObjectCount || item.asset == nullptr) {
                break;
            }

            const bool hasStructuredGltfMeshes = !item.asset->meshes.empty();
            if (hasStructuredGltfMeshes) {
                const MATH::Mat4 world = item.transform.GetWorldMatrix();
                const MATH::Mat4 normalMatrix = BuildNormalMatrix(item.transform);

                for (const MeshAsset& meshAsset : item.asset->meshes) {
                    for (const MeshPrimitive& primitive : meshAsset.primitives) {
                        if (objectIndex >= kMaxObjectCount) {
                            break;
                        }

                        const bool shouldDrawSkinned = !item.jointPalette.empty() && !primitive.skinnedVertices.empty();
                        bool drawingSkinned = false;
                        Mesh* mesh = shouldDrawSkinned ? GetOrCreateSkinnedPrimitiveMesh(primitive) : GetOrCreatePrimitiveMesh(primitive);
                        drawingSkinned = shouldDrawSkinned && mesh != nullptr && mesh->IsValid();
                        if (mesh == nullptr || !mesh->IsValid()) {
                            if (shouldDrawSkinned) {
                                ++g.debugStats.skinnedFallbackCount;
                                mesh = GetOrCreatePrimitiveMesh(primitive);
                            }
                            if (mesh == nullptr || !mesh->IsValid()) {
                                continue;
                            }
                        }

                        const MaterialAsset* materialAsset = GetPrimitiveMaterial(*item.asset, primitive.materialIndex);

                        ObjectCB obj{};
                        obj.world = world;
                        obj.normalMatrix = normalMatrix;
                        const int textureHandle = ResolvePrimitiveTextureHandle(*item.asset, materialAsset);
                        const int normalTextureHandle = ResolvePrimitiveNormalTextureHandle(*item.asset, materialAsset);
                        const int emissiveTextureHandle = ResolvePrimitiveEmissiveTextureHandle(*item.asset, materialAsset);
                        const int metallicRoughnessTextureHandle = ResolvePrimitiveMetallicRoughnessTextureHandle(*item.asset, materialAsset);
                        const int occlusionTextureHandle = ResolvePrimitiveOcclusionTextureHandle(*item.asset, materialAsset);
                        FillMaterialValues(obj, materialAsset, normalTextureHandle, emissiveTextureHandle, metallicRoughnessTextureHandle, occlusionTextureHandle);
                        obj.hasBaseColorTexture = (textureHandle >= 0 && textureHandle != g.fallbackTextureHandle) ? 1u : 0u;
                        obj.receiveShadow = item.receiveShadow ? 1u : 0u;
                        FillFxValues(obj, item);

                        uint8_t* dst = reinterpret_cast<uint8_t*>(g.objectMapped) + static_cast<size_t>(kObjectStride) * objectIndex;
                        std::memcpy(dst, &obj, sizeof(ObjectCB));

                        const D3D12_GPU_VIRTUAL_ADDRESS objAddress = g.objectCB->GetGPUVirtualAddress() + static_cast<UINT64>(kObjectStride) * objectIndex;
                        cmd->SetGraphicsRootSignature(drawingSkinned ? g.skinnedRootSig.Get() : g.rootSig.Get());
                        cmd->SetGraphicsRootConstantBufferView(0, g.cameraCB->GetGPUVirtualAddress());
                        cmd->SetGraphicsRootConstantBufferView(2, g.lightCB->GetGPUVirtualAddress());
                        cmd->SetGraphicsRootConstantBufferView(6, g.shadowCB->GetGPUVirtualAddress());
                        cmd->SetGraphicsRootConstantBufferView(10, g.skyEnvironmentCB->GetGPUVirtualAddress());
                        cmd->SetGraphicsRootConstantBufferView(1, objAddress);

                        const VFX::VariantKey primitiveVariant = ResolvePrimitiveVariant(item, materialAsset);
                        if (drawingSkinned) {
                            const size_t uploadedJointCount = UploadJointPalette(objectIndex, item.jointPalette);
                            const D3D12_GPU_VIRTUAL_ADDRESS paletteAddress = g.jointPaletteCB->GetGPUVirtualAddress() + static_cast<UINT64>(AlignConstantBufferSize(sizeof(JointPaletteCB))) * objectIndex;
                            cmd->SetGraphicsRootConstantBufferView(12, paletteAddress);
                            g.debugStats.uploadedJointCount += uploadedJointCount;
                            g.debugStats.maxJointCount = std::max(g.debugStats.maxJointCount, item.jointPalette.size());
                            g.debugStats.lastSkinnedVertexCount = primitive.skinnedVertices.size();
                        }

                        const D3D12_GPU_DESCRIPTOR_HANDLE textureSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(textureHandle);
                        if (textureSrv.ptr != 0) {
                            cmd->SetGraphicsRootDescriptorTable(3, textureSrv);
                        }
                        D3D12_GPU_DESCRIPTOR_HANDLE normalSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(normalTextureHandle);
                        if (normalSrv.ptr == 0) {
                            normalSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(g.fallbackTextureHandle);
                        }
                        if (normalSrv.ptr != 0) {
                            cmd->SetGraphicsRootDescriptorTable(4, normalSrv);
                        }
                        D3D12_GPU_DESCRIPTOR_HANDLE shadowSrv = SHADOW::GetDirectionalShadowSrv();
                        if (shadowSrv.ptr == 0) {
                            shadowSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(g.fallbackTextureHandle);
                        }
                        if (shadowSrv.ptr != 0) {
                            cmd->SetGraphicsRootDescriptorTable(5, shadowSrv);
                        }
                        D3D12_GPU_DESCRIPTOR_HANDLE emissiveSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(emissiveTextureHandle);
                        if (emissiveSrv.ptr == 0) {
                            emissiveSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(g.fallbackTextureHandle);
                        }
                        if (emissiveSrv.ptr != 0) {
                            cmd->SetGraphicsRootDescriptorTable(7, emissiveSrv);
                        }
                        D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(metallicRoughnessTextureHandle);
                        if (metallicRoughnessSrv.ptr == 0) {
                            metallicRoughnessSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(g.fallbackTextureHandle);
                        }
                        if (metallicRoughnessSrv.ptr != 0) {
                            cmd->SetGraphicsRootDescriptorTable(8, metallicRoughnessSrv);
                        }
                        D3D12_GPU_DESCRIPTOR_HANDLE occlusionSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(occlusionTextureHandle);
                        if (occlusionSrv.ptr == 0) {
                            occlusionSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(g.fallbackTextureHandle);
                        }
                        if (occlusionSrv.ptr != 0) {
                            cmd->SetGraphicsRootDescriptorTable(9, occlusionSrv);
                        }
                        const D3D12_GPU_DESCRIPTOR_HANDLE skyCubeSrv = ResolveSkyCubeSrv();
                        if (skyCubeSrv.ptr != 0) {
                            cmd->SetGraphicsRootDescriptorTable(11, skyCubeSrv);
                        }

                        D3D12_VERTEX_BUFFER_VIEW vb = mesh->GetVBView();
                        D3D12_INDEX_BUFFER_VIEW ib = mesh->GetIBView();
                        cmd->IASetVertexBuffers(0, 1, &vb);
                        cmd->IASetIndexBuffer(&ib);

                        const bool drawSolid = item.renderDebugMode != MeshRenderDebugMode::WireOnly;
                        const bool drawWire = item.renderDebugMode == MeshRenderDebugMode::WireOnly ||
                            item.renderDebugMode == MeshRenderDebugMode::WireOverlay;
                        auto drawPrimitive = [&](bool wireframe) {
                            ID3D12PipelineState* pso = GetOrCreateVariantPso(primitiveVariant, drawingSkinned, wireframe);
                            if (pso == nullptr) {
                                return;
                            }
                            cmd->SetPipelineState(pso);
                            cmd->DrawIndexedInstanced(mesh->GetIndexCount(), 1, 0, 0, 0);
                            if (drawingSkinned) {
                                ++g.debugStats.skinnedGpuDrawCount;
                            }
                            if (wireframe) {
                                ++g.debugStats.wireGpuDrawCount;
                            }
                        };

                        if (drawSolid) {
                            drawPrimitive(false);
                        }
                        if (drawWire) {
                            drawPrimitive(true);
                        }

                        ++objectIndex;
                    }
                }
                continue;
            }

            if (!item.asset->GetMesh() || !item.asset->GetMesh()->IsValid()) {
                continue;
            }

            ObjectCB obj{};
            obj.world = item.transform.GetWorldMatrix();
            obj.normalMatrix = BuildNormalMatrix(item.transform);
            FillMaterialValues(obj, nullptr, g.fallbackNormalTextureHandle, g.fallbackBlackTextureHandle, g.fallbackTextureHandle, g.fallbackTextureHandle);
            obj.receiveShadow = item.receiveShadow ? 1u : 0u;
            if (const Material* material = item.asset->GetMaterial()) {
                obj.baseColor = material->GetBaseColor();
                obj.hasBaseColorTexture = material->HasBaseColorTexture() ? 1u : 0u;
            } else {
                obj.baseColor = { 1,1,1,1 };
                obj.hasBaseColorTexture = 0u;
            }
            FillFxValues(obj, item);

            uint8_t* dst = reinterpret_cast<uint8_t*>(g.objectMapped) + static_cast<size_t>(kObjectStride) * objectIndex;
            std::memcpy(dst, &obj, sizeof(ObjectCB));

            const D3D12_GPU_VIRTUAL_ADDRESS objAddress = g.objectCB->GetGPUVirtualAddress() + static_cast<UINT64>(kObjectStride) * objectIndex;
            cmd->SetGraphicsRootSignature(g.rootSig.Get());
            cmd->SetGraphicsRootConstantBufferView(0, g.cameraCB->GetGPUVirtualAddress());
            cmd->SetGraphicsRootConstantBufferView(2, g.lightCB->GetGPUVirtualAddress());
            cmd->SetGraphicsRootConstantBufferView(6, g.shadowCB->GetGPUVirtualAddress());
            cmd->SetGraphicsRootConstantBufferView(10, g.skyEnvironmentCB->GetGPUVirtualAddress());
            cmd->SetGraphicsRootConstantBufferView(1, objAddress);

            int textureHandle = g.fallbackTextureHandle;
            if (const Material* material = item.asset->GetMaterial()) {
                if (material->HasBaseColorTexture()) {
                    textureHandle = material->GetBaseColorTextureHandle();
                }
            }
            const D3D12_GPU_DESCRIPTOR_HANDLE textureSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(textureHandle);
            if (textureSrv.ptr != 0) {
                cmd->SetGraphicsRootDescriptorTable(3, textureSrv);
            }
            D3D12_GPU_DESCRIPTOR_HANDLE normalSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(g.fallbackNormalTextureHandle);
            if (normalSrv.ptr == 0) {
                normalSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(textureHandle);
            }
            if (normalSrv.ptr != 0) {
                cmd->SetGraphicsRootDescriptorTable(4, normalSrv);
            }
            D3D12_GPU_DESCRIPTOR_HANDLE shadowSrv = SHADOW::GetDirectionalShadowSrv();
            if (shadowSrv.ptr == 0) {
                shadowSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(g.fallbackTextureHandle);
            }
            if (shadowSrv.ptr != 0) {
                cmd->SetGraphicsRootDescriptorTable(5, shadowSrv);
            }
            D3D12_GPU_DESCRIPTOR_HANDLE emissiveSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(g.fallbackTextureHandle);
            if (emissiveSrv.ptr != 0) {
                cmd->SetGraphicsRootDescriptorTable(7, emissiveSrv);
            }
            const D3D12_GPU_DESCRIPTOR_HANDLE pbrFallbackSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(g.fallbackTextureHandle);
            if (pbrFallbackSrv.ptr != 0) {
                cmd->SetGraphicsRootDescriptorTable(8, pbrFallbackSrv);
                cmd->SetGraphicsRootDescriptorTable(9, pbrFallbackSrv);
            }
            const D3D12_GPU_DESCRIPTOR_HANDLE skyCubeSrv = ResolveSkyCubeSrv();
            if (skyCubeSrv.ptr != 0) {
                cmd->SetGraphicsRootDescriptorTable(11, skyCubeSrv);
            }

            const Mesh* mesh = item.asset->GetMesh();
            D3D12_VERTEX_BUFFER_VIEW vb = mesh->GetVBView();
            D3D12_INDEX_BUFFER_VIEW ib = mesh->GetIBView();
            cmd->IASetVertexBuffers(0, 1, &vb);
            cmd->IASetIndexBuffer(&ib);
            const bool drawSolid = item.renderDebugMode != MeshRenderDebugMode::WireOnly;
            const bool drawWire = item.renderDebugMode == MeshRenderDebugMode::WireOnly ||
                item.renderDebugMode == MeshRenderDebugMode::WireOverlay;
            auto drawLegacyMesh = [&](bool wireframe) {
                ID3D12PipelineState* pso = GetOrCreateVariantPso(item.variant, false, wireframe);
                if (pso == nullptr) {
                    return;
                }
                cmd->SetPipelineState(pso);
                cmd->DrawIndexedInstanced(mesh->GetIndexCount(), 1, 0, 0, 0);
                if (wireframe) {
                    ++g.debugStats.wireGpuDrawCount;
                }
            };
            if (drawSolid) {
                drawLegacyMesh(false);
            }
            if (drawWire) {
                drawLegacyMesh(true);
            }
            ++objectIndex;
        }

        g.drawItems.clear();
    }

    const MeshRendererDebugStats& GetDebugStats() {
        const MaterialFxProfileCacheStats fxCacheStats = MaterialFxProfile::GetCacheStats();
        g.debugStats.materialFxProfileCacheHitCount = fxCacheStats.hitCount;
        g.debugStats.materialFxProfileCacheMissCount = fxCacheStats.missCount;
        g.debugStats.materialFxProfileCacheFailCount = fxCacheStats.failCount;
        return g.debugStats;
    }

} // namespace HIKARI::MESHRENDERER
