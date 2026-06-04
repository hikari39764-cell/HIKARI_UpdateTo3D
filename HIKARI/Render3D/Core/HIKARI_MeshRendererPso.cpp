#include "Render3D/Core/HIKARI_MeshRendererPso.h"

#include <array>
#include <cstddef>

#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/HIKARI_Mesh.h"
#include "Render3D/Core/HIKARI_MeshRendererRootParams.h"

namespace HIKARI::MESHRENDERER {

    using Microsoft::WRL::ComPtr;

    size_t VariantKeyHasher::operator()(const VFX::VariantKey& key) const noexcept {
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

    namespace {
		// シェーダIDから、実際のシェーダファイルのパスを解決する。特定のIDに対しては、デフォルトのファイルパスを返す。
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
		// シェーダIDをキーにして、コンパイル済みのピクセルシェーダのバイナリをキャッシュから取得する。キャッシュに存在しない場合は、ファイルからコンパイルしてキャッシュに保存する。
        bool LoadPixelShaderBlob(MeshPipelineStore& store, const std::string& shaderProfileId, ID3DBlob** outBlob) {
            const std::string cacheKey = shaderProfileId.empty() ? "StaticLit" : shaderProfileId;
            auto it = store.psBlobCache.find(cacheKey);
            if (it != store.psBlobCache.end()) {
                *outBlob = it->second.Get();
                return true;
            }

            ComPtr<ID3DBlob> blob;
            const std::wstring path = ResolveShaderPath(cacheKey, L"HIKARI/Shaders/Render3D_StaticPS.hlsl");
            if (!GFX::CompileShaderFileSm6(path.c_str(), "main", GFX::ShaderStage::Pixel, blob.GetAddressOf())) {
                DEBUGLOG::PushRenderError(std::string("[MeshRenderer][MaterialFx][WARN] Pixel shader compile failed. shaderId=") + cacheKey + " fallback used");
                return false;
            }
            auto [insertIt, _] = store.psBlobCache.emplace(cacheKey, blob);
            *outBlob = insertIt->second.Get();
            return true;
        }
        // シェーダIDをキーにして、コンパイル済みの頂点シェーダのバイナリをキャッシュから取得する。キャッシュに存在しない場合は、ファイルからコンパイルしてキャッシュに保存する。
        bool LoadVertexShaderBlob(MeshPipelineStore& store, const std::string& vertexShaderId, ID3DBlob** outBlob) {
            const std::string cacheKey = vertexShaderId.empty() ? "Render3D_StaticVS" : vertexShaderId;
            auto it = store.vsBlobCache.find(cacheKey);
            if (it != store.vsBlobCache.end()) {
                *outBlob = it->second.Get();
                return true;
            }

            ComPtr<ID3DBlob> blob;
            const std::wstring path = ResolveShaderPath(cacheKey, L"HIKARI/Shaders/Render3D_StaticVS.hlsl");
            if (!GFX::CompileShaderFileSm6(path.c_str(), "main", GFX::ShaderStage::Vertex, blob.GetAddressOf())) {
                DEBUGLOG::PushRenderError(std::string("[MeshRenderer][MaterialFx][WARN] Vertex shader compile failed. shaderId=") + cacheKey);
                return false;
            }
            auto [insertIt, _] = store.vsBlobCache.emplace(cacheKey, blob);
            *outBlob = insertIt->second.Get();
            return true;
        }

        bool CreateVariantPipeline(MeshPipelineStore& store, ID3D12Device* device, const VFX::VariantKey& key, bool wireframe, ID3D12PipelineState** outPso) {
            ID3DBlob* vsBlob = nullptr;
            if (!LoadVertexShaderBlob(store, key.vertexShaderId, &vsBlob) || vsBlob == nullptr) {
                vsBlob = store.vsBlob.Get();
            }
            ID3DBlob* psBlob = nullptr;
            const std::string psId = !key.pixelShaderId.empty() ? key.pixelShaderId : key.shaderId;
            if (!LoadPixelShaderBlob(store, psId, &psBlob) || psBlob == nullptr) {
                return false;
            }

            const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, normal)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, tangent)),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, static_cast<UINT>(offsetof(VertexStatic3D, u)),        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = store.rootSig.Get();
            psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
            psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
            ApplyCompositeBlendState(psoDesc.BlendState, key.composite);
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

        bool CreateSkinnedVariantPipeline(MeshPipelineStore& store, ID3D12Device* device, const VFX::VariantKey& key, bool wireframe, ID3D12PipelineState** outPso) {
            ID3DBlob* psBlob = nullptr;
            const std::string psId = !key.pixelShaderId.empty() ? key.pixelShaderId : key.shaderId;
            if (!LoadPixelShaderBlob(store, psId, &psBlob) || psBlob == nullptr || store.skinnedVsBlob == nullptr) {
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
            psoDesc.pRootSignature = store.skinnedRootSig.Get();
            psoDesc.VS = { store.skinnedVsBlob->GetBufferPointer(), store.skinnedVsBlob->GetBufferSize() };
            psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
            ApplyCompositeBlendState(psoDesc.BlendState, key.composite);
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
    }

    void ApplyCompositeBlendState(D3D12_BLEND_DESC& blendState, VFX::CompositeMode composite) {
        blendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

        D3D12_RENDER_TARGET_BLEND_DESC& rt0 = blendState.RenderTarget[0];

        if (composite == VFX::CompositeMode::Alpha) {
            rt0.BlendEnable = TRUE;
            rt0.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            rt0.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            rt0.BlendOp = D3D12_BLEND_OP_ADD;
            rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt0.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        } else if (composite == VFX::CompositeMode::Additive) {
            rt0.BlendEnable = TRUE;
            rt0.SrcBlend = D3D12_BLEND_ONE;
            rt0.DestBlend = D3D12_BLEND_ONE;
            rt0.BlendOp = D3D12_BLEND_OP_ADD;
            rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt0.DestBlendAlpha = D3D12_BLEND_ONE;
            rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        } else if (composite == VFX::CompositeMode::Multiply){
			rt0.BlendEnable = TRUE;
			rt0.SrcBlend = D3D12_BLEND_DEST_COLOR;
			rt0.DestBlend = D3D12_BLEND_ZERO;
			rt0.BlendOp = D3D12_BLEND_OP_ADD;
			rt0.SrcBlendAlpha = D3D12_BLEND_DEST_ALPHA;
			rt0.DestBlendAlpha = D3D12_BLEND_ZERO;
			rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
    }

    bool InitializeMeshPipelines(ID3D12Device* device, MeshPipelineStore& store) {
        if (!GFX::SupportsShaderModel6(device)) {
            DEBUGLOG::PushRenderError("[MeshRenderer][ERROR] Shader Model 6.0 is not supported by this device.");
            return false;
        }

        if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_StaticVS.hlsl", "main", GFX::ShaderStage::Vertex, store.vsBlob.GetAddressOf())) {
            return false;
        }
        if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_SkinnedVS.hlsl", "main", GFX::ShaderStage::Vertex, store.skinnedVsBlob.GetAddressOf())) {
            return false;
        }
        if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_StaticPS.hlsl", "main", GFX::ShaderStage::Pixel, store.psBlob.GetAddressOf())) {
            return false;
        }
        if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_GeometryBufferPS.hlsl", "main", GFX::ShaderStage::Pixel, store.geometryPsBlob.GetAddressOf())) {
            return false;
        }

        D3D12_DESCRIPTOR_RANGE textureRange{};
        textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        textureRange.NumDescriptors = 1;
        textureRange.BaseShaderRegister = 0;
        textureRange.RegisterSpace = 0;
        textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE normalTextureRange{};
        normalTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        normalTextureRange.NumDescriptors = 1;
        normalTextureRange.BaseShaderRegister = 1;
        normalTextureRange.RegisterSpace = 0;
        normalTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE shadowTextureRange{};
        shadowTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        shadowTextureRange.NumDescriptors = 1;
        shadowTextureRange.BaseShaderRegister = 2;
        shadowTextureRange.RegisterSpace = 0;
        shadowTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE emissiveTextureRange{};
        emissiveTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        emissiveTextureRange.NumDescriptors = 1;
        emissiveTextureRange.BaseShaderRegister = 3;
        emissiveTextureRange.RegisterSpace = 0;
        emissiveTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE metallicRoughnessTextureRange{};
        metallicRoughnessTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        metallicRoughnessTextureRange.NumDescriptors = 1;
        metallicRoughnessTextureRange.BaseShaderRegister = 4;
        metallicRoughnessTextureRange.RegisterSpace = 0;
        metallicRoughnessTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE occlusionTextureRange{};
        occlusionTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        occlusionTextureRange.NumDescriptors = 1;
        occlusionTextureRange.BaseShaderRegister = 5;
        occlusionTextureRange.RegisterSpace = 0;
        occlusionTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE skyCubeTextureRange{};
        skyCubeTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        skyCubeTextureRange.NumDescriptors = 1;
        skyCubeTextureRange.BaseShaderRegister = 6;
        skyCubeTextureRange.RegisterSpace = 0;
        skyCubeTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE sceneDepthTextureRange{};
        sceneDepthTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        sceneDepthTextureRange.NumDescriptors = 1;
        sceneDepthTextureRange.BaseShaderRegister = 7;
        sceneDepthTextureRange.RegisterSpace = 0;
        sceneDepthTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE sceneColorTextureRange{};
        sceneColorTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        sceneColorTextureRange.NumDescriptors = 1;
        sceneColorTextureRange.BaseShaderRegister = 8;
        sceneColorTextureRange.RegisterSpace = 0;
        sceneColorTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE iblIrradianceRange{};
        iblIrradianceRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        iblIrradianceRange.NumDescriptors = 1;
        iblIrradianceRange.BaseShaderRegister = 9;
        iblIrradianceRange.RegisterSpace = 0;
        iblIrradianceRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE iblPrefilteredRange{};
        iblPrefilteredRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        iblPrefilteredRange.NumDescriptors = 1;
        iblPrefilteredRange.BaseShaderRegister = 10;
        iblPrefilteredRange.RegisterSpace = 0;
        iblPrefilteredRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE iblBrdfLutRange{};
        iblBrdfLutRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        iblBrdfLutRange.NumDescriptors = 1;
        iblBrdfLutRange.BaseShaderRegister = 11;
        iblBrdfLutRange.RegisterSpace = 0;
        iblBrdfLutRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE reflectionProbePrefilteredRange{};
        reflectionProbePrefilteredRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        reflectionProbePrefilteredRange.NumDescriptors = 1;
        reflectionProbePrefilteredRange.BaseShaderRegister = 12;
        reflectionProbePrefilteredRange.RegisterSpace = 0;
        reflectionProbePrefilteredRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE ssaoRange{};
        ssaoRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ssaoRange.NumDescriptors = 1;
        ssaoRange.BaseShaderRegister = 13;
        ssaoRange.RegisterSpace = 0;
        ssaoRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE lightProbeShRange{};
        lightProbeShRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        lightProbeShRange.NumDescriptors = 1;
        lightProbeShRange.BaseShaderRegister = 14;
        lightProbeShRange.RegisterSpace = 0;
        lightProbeShRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE objectDataRange{};
        objectDataRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        objectDataRange.NumDescriptors = 1;
        objectDataRange.BaseShaderRegister = 15;
        objectDataRange.RegisterSpace = 0;
        objectDataRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE materialDataRange{};
        materialDataRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        materialDataRange.NumDescriptors = 1;
        materialDataRange.BaseShaderRegister = 16;
        materialDataRange.RegisterSpace = 0;
        materialDataRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[ROOT_PARAM::MaterialIndex + 1]{};
        params[ROOT_PARAM::Camera].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[ROOT_PARAM::Camera].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::Camera].Descriptor.ShaderRegister = 0;
        params[ROOT_PARAM::Camera].Descriptor.RegisterSpace = 0;

        params[ROOT_PARAM::Object].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[ROOT_PARAM::Object].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::Object].Descriptor.ShaderRegister = 1;
        params[ROOT_PARAM::Object].Descriptor.RegisterSpace = 0;

        params[ROOT_PARAM::Light].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[ROOT_PARAM::Light].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::Light].Descriptor.ShaderRegister = 2;
        params[ROOT_PARAM::Light].Descriptor.RegisterSpace = 0;

        params[ROOT_PARAM::BaseColor].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::BaseColor].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::BaseColor].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::BaseColor].DescriptorTable.pDescriptorRanges = &textureRange;

        params[ROOT_PARAM::Normal].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::Normal].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::Normal].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::Normal].DescriptorTable.pDescriptorRanges = &normalTextureRange;

        params[ROOT_PARAM::ShadowMap].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::ShadowMap].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::ShadowMap].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::ShadowMap].DescriptorTable.pDescriptorRanges = &shadowTextureRange;

        params[ROOT_PARAM::ShadowCB].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[ROOT_PARAM::ShadowCB].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::ShadowCB].Descriptor.ShaderRegister = 4;
        params[ROOT_PARAM::ShadowCB].Descriptor.RegisterSpace = 0;

        params[ROOT_PARAM::Emissive].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::Emissive].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::Emissive].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::Emissive].DescriptorTable.pDescriptorRanges = &emissiveTextureRange;

        params[ROOT_PARAM::MetallicRoughness].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::MetallicRoughness].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::MetallicRoughness].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::MetallicRoughness].DescriptorTable.pDescriptorRanges = &metallicRoughnessTextureRange;

        params[ROOT_PARAM::Occlusion].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::Occlusion].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::Occlusion].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::Occlusion].DescriptorTable.pDescriptorRanges = &occlusionTextureRange;

        params[ROOT_PARAM::SkyEnvironment].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[ROOT_PARAM::SkyEnvironment].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::SkyEnvironment].Descriptor.ShaderRegister = 5;
        params[ROOT_PARAM::SkyEnvironment].Descriptor.RegisterSpace = 0;

        params[ROOT_PARAM::SkyCube].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::SkyCube].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::SkyCube].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::SkyCube].DescriptorTable.pDescriptorRanges = &skyCubeTextureRange;

        params[ROOT_PARAM::SceneDepth].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::SceneDepth].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::SceneDepth].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::SceneDepth].DescriptorTable.pDescriptorRanges = &sceneDepthTextureRange;

        params[ROOT_PARAM::SceneColor].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::SceneColor].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::SceneColor].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::SceneColor].DescriptorTable.pDescriptorRanges = &sceneColorTextureRange;

        params[ROOT_PARAM::IblIrradiance].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::IblIrradiance].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::IblIrradiance].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::IblIrradiance].DescriptorTable.pDescriptorRanges = &iblIrradianceRange;

        params[ROOT_PARAM::IblPrefiltered].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::IblPrefiltered].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::IblPrefiltered].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::IblPrefiltered].DescriptorTable.pDescriptorRanges = &iblPrefilteredRange;

        params[ROOT_PARAM::IblBrdfLut].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::IblBrdfLut].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::IblBrdfLut].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::IblBrdfLut].DescriptorTable.pDescriptorRanges = &iblBrdfLutRange;

        params[ROOT_PARAM::ReflectionProbePrefiltered].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::ReflectionProbePrefiltered].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::ReflectionProbePrefiltered].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::ReflectionProbePrefiltered].DescriptorTable.pDescriptorRanges = &reflectionProbePrefilteredRange;

        params[ROOT_PARAM::Ssao].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::Ssao].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::Ssao].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::Ssao].DescriptorTable.pDescriptorRanges = &ssaoRange;

        params[ROOT_PARAM::LightProbeSh].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::LightProbeSh].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[ROOT_PARAM::LightProbeSh].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::LightProbeSh].DescriptorTable.pDescriptorRanges = &lightProbeShRange;

        params[ROOT_PARAM::ObjectData].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::ObjectData].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::ObjectData].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::ObjectData].DescriptorTable.pDescriptorRanges = &objectDataRange;

        params[ROOT_PARAM::ObjectIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[ROOT_PARAM::ObjectIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::ObjectIndex].Constants.ShaderRegister = 6;
        params[ROOT_PARAM::ObjectIndex].Constants.RegisterSpace = 0;
        params[ROOT_PARAM::ObjectIndex].Constants.Num32BitValues = 1;

        params[ROOT_PARAM::MaterialData].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[ROOT_PARAM::MaterialData].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::MaterialData].DescriptorTable.NumDescriptorRanges = 1;
        params[ROOT_PARAM::MaterialData].DescriptorTable.pDescriptorRanges = &materialDataRange;

        params[ROOT_PARAM::MaterialIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[ROOT_PARAM::MaterialIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[ROOT_PARAM::MaterialIndex].Constants.ShaderRegister = 7;
        params[ROOT_PARAM::MaterialIndex].Constants.RegisterSpace = 0;
        params[ROOT_PARAM::MaterialIndex].Constants.Num32BitValues = 1;

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

        if (FAILED(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(store.rootSig.GetAddressOf())))) {
            return false;
        }

        D3D12_ROOT_PARAMETER skinnedParams[ROOT_PARAM::JointPalette + 1]{};
        for (size_t i = 0; i < std::size(params); ++i) {
            skinnedParams[i] = params[i];
        }
        skinnedParams[ROOT_PARAM::JointPalette].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        skinnedParams[ROOT_PARAM::JointPalette].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        skinnedParams[ROOT_PARAM::JointPalette].Descriptor.ShaderRegister = 3;
        skinnedParams[ROOT_PARAM::JointPalette].Descriptor.RegisterSpace = 0;

        D3D12_ROOT_SIGNATURE_DESC skinnedRsDesc = rsDesc;
        skinnedRsDesc.NumParameters = static_cast<UINT>(std::size(skinnedParams));
        skinnedRsDesc.pParameters = skinnedParams;

        sigBlob.Reset();
        errBlob.Reset();
        if (FAILED(D3D12SerializeRootSignature(&skinnedRsDesc, D3D_ROOT_SIGNATURE_VERSION_1, sigBlob.GetAddressOf(), errBlob.GetAddressOf()))) {
            if (errBlob) OutputDebugStringA(static_cast<const char*>(errBlob->GetBufferPointer()));
            return false;
        }
        if (FAILED(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(store.skinnedRootSig.GetAddressOf())))) {
            return false;
        }

        const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, normal)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, tangent)),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, static_cast<UINT>(offsetof(VertexStatic3D, u)),        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = store.rootSig.Get();
        psoDesc.VS = { store.vsBlob->GetBufferPointer(), store.vsBlob->GetBufferSize() };
        psoDesc.PS = { store.psBlob->GetBufferPointer(), store.psBlob->GetBufferSize() };
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

        if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(store.pso.GetAddressOf())))) {
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
        skinnedPsoDesc.pRootSignature = store.skinnedRootSig.Get();
        skinnedPsoDesc.VS = { store.skinnedVsBlob->GetBufferPointer(), store.skinnedVsBlob->GetBufferSize() };
        skinnedPsoDesc.InputLayout = { skinnedInputElements, static_cast<UINT>(std::size(skinnedInputElements)) };
        if (FAILED(device->CreateGraphicsPipelineState(&skinnedPsoDesc, IID_PPV_ARGS(store.skinnedPso.GetAddressOf())))) {
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC geometryPsoDesc = psoDesc;
        geometryPsoDesc.PS = { store.geometryPsBlob->GetBufferPointer(), store.geometryPsBlob->GetBufferSize() };
        geometryPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        geometryPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        geometryPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        geometryPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (FAILED(device->CreateGraphicsPipelineState(&geometryPsoDesc, IID_PPV_ARGS(store.geometryPso.GetAddressOf())))) {
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC geometrySkinnedPsoDesc = geometryPsoDesc;
        geometrySkinnedPsoDesc.pRootSignature = store.skinnedRootSig.Get();
        geometrySkinnedPsoDesc.VS = { store.skinnedVsBlob->GetBufferPointer(), store.skinnedVsBlob->GetBufferSize() };
        geometrySkinnedPsoDesc.InputLayout = { skinnedInputElements, static_cast<UINT>(std::size(skinnedInputElements)) };
        if (FAILED(device->CreateGraphicsPipelineState(&geometrySkinnedPsoDesc, IID_PPV_ARGS(store.geometrySkinnedPso.GetAddressOf())))) {
            return false;
        }

        store.psBlobCache["StaticLit"] = store.psBlob;
        return true;
    }

    void ShutdownMeshPipelines(MeshPipelineStore& store) {
        store.variantPsoCache.clear();
        store.skinnedVariantPsoCache.clear();
        store.wireVariantPsoCache.clear();
        store.skinnedWireVariantPsoCache.clear();
        store.psBlobCache.clear();
        store.vsBlobCache.clear();
        store.pso.Reset();
        store.skinnedPso.Reset();
        store.geometryPso.Reset();
        store.geometrySkinnedPso.Reset();
        store.rootSig.Reset();
        store.skinnedRootSig.Reset();
        store.vsBlob.Reset();
        store.skinnedVsBlob.Reset();
        store.psBlob.Reset();
        store.geometryPsBlob.Reset();
    }

    ID3D12RootSignature* GetStaticRootSignature(MeshPipelineStore& store) {
        return store.rootSig.Get();
    }

    ID3D12RootSignature* GetSkinnedRootSignature(MeshPipelineStore& store) {
        return store.skinnedRootSig.Get();
    }

    ID3D12PipelineState* GetGeometryPso(MeshPipelineStore& store, bool skinned) {
        return skinned ? store.geometrySkinnedPso.Get() : store.geometryPso.Get();
    }

    ID3D12PipelineState* GetOrCreateVariantPso(
        MeshPipelineStore& store,
        ID3D12Device* device,
        MeshRendererDebugStats& stats,
        const VFX::VariantKey& key,
        bool skinned,
        bool wireframe) {
        auto& cache = skinned
            ? (wireframe ? store.skinnedWireVariantPsoCache : store.skinnedVariantPsoCache)
            : (wireframe ? store.wireVariantPsoCache : store.variantPsoCache);

        auto found = cache.find(key);
        if (found == cache.end()) {
            ++stats.psoCacheMissCount;
            ComPtr<ID3D12PipelineState> variantPso;
            const bool created = skinned
                ? CreateSkinnedVariantPipeline(store, device, key, wireframe, variantPso.GetAddressOf())
                : CreateVariantPipeline(store, device, key, wireframe, variantPso.GetAddressOf());
            if (!created) {
                variantPso = skinned ? (store.skinnedPso ? store.skinnedPso : store.pso) : store.pso;
            }
            found = cache.emplace(key, std::move(variantPso)).first;
        } else {
            ++stats.psoCacheHitCount;
        }

        return found->second.Get();
    }

} // namespace HIKARI::MESHRENDERER
