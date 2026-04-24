#include "HIKARI_MeshRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include <unordered_map>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <wrl/client.h>
#include "Gfx/HIKARI_GpuResources.h"
#include "HIKARI_Services.h"
#include "Core/HIKARI_TimeService.h"
#include "HIKARI_D3DBlobCompat.h"
#include "Vfx/Common/HIKARI_FxTypes.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace HIKARI::MESHRENDERER {

    using Microsoft::WRL::ComPtr;

    namespace {
        constexpr uint32_t kVariantFlagAlphaBlend = 1u << 31;

        struct CameraCB {
            MATH::Mat4 viewProj{};
            MATH::Vec4 cameraPos{};
        };

        struct ObjectCB {
            MATH::Mat4 world{};
            MATH::Mat4 normalMatrix{};
            MATH::Vec4 baseColorFactor{};
            MATH::Vec4 emissiveFactor{};
            float normalScale = 1.0f;
            float occlusionStrength = 1.0f;
            float metallicFactor = 1.0f;
            float roughnessFactor = 1.0f;
            uint32_t hasBaseColorTexture = 0;
            uint32_t hasNormalTexture = 0;
            uint32_t hasOrmTexture = 0;
            uint32_t hasEmissiveTexture = 0;
            uint32_t alphaMode = 0;
            float alphaCutoff = 0.5f;
            uint32_t fxFlags = 0;
            float padding[1]{};
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
            MATH::Vec4 pointLightPosRange[4]{};
            MATH::Vec4 pointLightColorIntensity[4]{};
            float directionalIntensity = 1.0f;
            float ambientIntensity = 0.25f;
            uint32_t pointLightCount = 0;
            float padding[2]{};
        };

        struct DrawItemRuntime {
            StaticModelDrawItem item{};
            VFX::VariantKey variant{};
            std::array<MATH::Vec4, 4> fxValues{};
            uint32_t fxFlags = 0;
            ASSET::AlphaMode alphaMode = ASSET::AlphaMode::Opaque;
            float sortDistanceSq = 0.0f;
            uint32_t materialBlockTextureIds[4]{};
        };

        struct VariantKeyHasher {
            size_t operator()(const VFX::VariantKey& key) const noexcept {
                size_t seed = std::hash<std::string>{}(key.shaderId);
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
            ComPtr<ID3D12PipelineState> pso;
            ComPtr<ID3DBlob> vsBlob;
            ComPtr<ID3DBlob> psBlob;
            ComPtr<ID3D12Resource> cameraCB;
            ComPtr<ID3D12Resource> objectCB;
            ComPtr<ID3D12Resource> lightCB;
            CameraCB* cameraMapped = nullptr;
            ObjectCB* objectMapped = nullptr;
            LightCB* lightMapped = nullptr;
            std::vector<StaticModelDrawItem> submittedDrawItems;
            std::vector<StaticModelDrawItem> baseDrawItems;
            std::unordered_map<ObjectFxBucketKey, std::vector<StaticModelDrawItem>, ObjectFxBucketKeyHasher> objectFxBuckets;
            std::vector<DrawItemRuntime> runtimeItems;
            uint32_t fallbackTextureId = 0;
            uint32_t fallbackNormalTextureId = 0;
            uint32_t fallbackOrmTextureId = 0;
            uint32_t fallbackEmissiveTextureId = 0;
            std::unordered_map<VFX::VariantKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, VariantKeyHasher> variantPsoCache;
            std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> psBlobCache;
        };

        State g;

        bool CreateBuffers(ID3D12Device* device) {
            const UINT cameraBytes = (sizeof(CameraCB) + 255u) & ~255u;
            const UINT objectBytes = (sizeof(ObjectCB) * 2048u + 255u) & ~255u;
            const UINT lightBytes = (sizeof(LightCB) + 255u) & ~255u;

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
            if (FAILED(D3DCompileFromFile(L"HIKARI/Shaders/Render3D_StaticPS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", flags, 0, g.psBlob.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }

            D3D12_DESCRIPTOR_RANGE textureRange{};
            textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            textureRange.NumDescriptors = 4;
            textureRange.BaseShaderRegister = 0;
            textureRange.RegisterSpace = 0;
            textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER params[4]{};
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

            D3D12_STATIC_SAMPLER_DESC linearWrapSampler{};
            linearWrapSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            linearWrapSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            linearWrapSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            linearWrapSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            linearWrapSampler.ShaderRegister = 0;
            linearWrapSampler.RegisterSpace = 0;
            linearWrapSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            linearWrapSampler.MaxLOD = D3D12_FLOAT32_MAX;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = static_cast<UINT>(std::size(params));
            rsDesc.pParameters = params;
            rsDesc.NumStaticSamplers = 1;
            rsDesc.pStaticSamplers = &linearWrapSampler;
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

            const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(ASSET::MeshAsset::Vertex, px)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(ASSET::MeshAsset::Vertex, nx)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(ASSET::MeshAsset::Vertex, tx)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, static_cast<UINT>(offsetof(ASSET::MeshAsset::Vertex, u)),        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = 1;

            return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(g.pso.GetAddressOf())));
        }

        const wchar_t* ResolvePixelShaderPath(const std::string& shaderProfileId) {
            if (shaderProfileId == "StaticFx") {
                return L"HIKARI/Shaders/Render3D_StaticFxPS.hlsl";
            }
            return L"HIKARI/Shaders/Render3D_StaticPS.hlsl";
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
            if (FAILED(D3DCompileFromFile(ResolvePixelShaderPath(cacheKey), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", flags, 0, blob.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }
            auto [insertIt, _] = g.psBlobCache.emplace(cacheKey, blob);
            *outBlob = insertIt->second.Get();
            return true;
        }

        bool CreateVariantPipeline(ID3D12Device* device, const VFX::VariantKey& key, ID3D12PipelineState** outPso) {
            ID3DBlob* psBlob = nullptr;
            if (!LoadPixelShaderBlob(key.shaderId, &psBlob) || psBlob == nullptr) {
                return false;
            }

            const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(ASSET::MeshAsset::Vertex, px)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(ASSET::MeshAsset::Vertex, nx)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(ASSET::MeshAsset::Vertex, tx)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, static_cast<UINT>(offsetof(ASSET::MeshAsset::Vertex, u)),        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = g.rootSig.Get();
            psoDesc.VS = { g.vsBlob->GetBufferPointer(), g.vsBlob->GetBufferSize() };
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
            } else if ((key.featureBits & kVariantFlagAlphaBlend) != 0u) {
                D3D12_RENDER_TARGET_BLEND_DESC& rt0 = psoDesc.BlendState.RenderTarget[0];
                rt0.BlendEnable = TRUE;
                rt0.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                rt0.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                rt0.BlendOp = D3D12_BLEND_OP_ADD;
                rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
                rt0.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            }
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = key.doubleSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = key.depthTest ? TRUE : FALSE;
            psoDesc.DepthStencilState.DepthWriteMask = key.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            psoDesc.InputLayout = { inputElements, static_cast<UINT>(std::size(inputElements)) };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = 1;
            return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(outPso)));
        }

        void ResolveDrawVariant(DrawItemRuntime& runtime, bool applyMaterialFxProfile, const std::string* overrideMaterialFxProfileId = nullptr) {
            StaticModelDrawItem& item = runtime.item;
            if (item.registry == nullptr) {
                return;
            }
            const ASSET::MaterialAsset* material = item.registry->FindMaterial(item.material);
            if (material) {
                runtime.variant.shaderId = material->shaderProfileId;
                runtime.variant.featureBits = material->featureBits;
                runtime.variant.doubleSided = material->doubleSided;
                runtime.alphaMode = material->alphaMode;
            }
            runtime.variant.composite = VFX::CompositeMode::Alpha;
            runtime.variant.depthTest = true;
            runtime.variant.depthWrite = true;
            if (!material) {
                runtime.variant.doubleSided = false;
                runtime.alphaMode = ASSET::AlphaMode::Opaque;
            }

            const std::string* materialFxProfileId = overrideMaterialFxProfileId;
            if (materialFxProfileId == nullptr) {
                materialFxProfileId = &item.materialFxProfileId;
            }

            if (applyMaterialFxProfile && materialFxProfileId != nullptr && !materialFxProfileId->empty()) {
                MaterialFxProfile profile{};
                if (MaterialFxProfile::LoadById(*materialFxProfileId, profile)) {
                    if (!profile.shaderProfileId.empty()) {
                        runtime.variant.shaderId = profile.shaderProfileId;
                    }
                    runtime.variant.featureBits = profile.featureBits;
                    runtime.variant.composite = profile.composite;
                    runtime.variant.depthTest = profile.depthTest;
                    runtime.variant.depthWrite = profile.depthWrite;
                    runtime.variant.doubleSided = profile.doubleSided;
                }
            }

            if (runtime.alphaMode == ASSET::AlphaMode::Blend) {
                runtime.variant.depthWrite = false;
                runtime.variant.featureBits |= kVariantFlagAlphaBlend;
            }

            runtime.fxValues = {};
            runtime.fxFlags = 0;
            if (!item.materialFxValuesInitialized) {
                return;
            }
            for (size_t i = 0; i < runtime.fxValues.size(); ++i) {
                const DirectX::XMFLOAT4& value = item.materialFxUser[i];
                runtime.fxValues[i] = { value.x, value.y, value.z, value.w };
            }
            runtime.fxFlags = runtime.variant.featureBits;
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
            g.fallbackTextureId = GpuResources::LoadTexture("mesh_renderer/fallback_white", "HIKARI/white1x1.png");
            g.fallbackNormalTextureId = GpuResources::LoadTexture("mesh_renderer/fallback_normal", "Data/vfx/Textures/Normal01.png");
            g.fallbackOrmTextureId = GpuResources::LoadTexture("mesh_renderer/fallback_orm", "HIKARI/white1x1.png");
            g.fallbackEmissiveTextureId = GpuResources::LoadTexture("mesh_renderer/fallback_black", "Data/vfx/Textures/Gradation_White_Black_Sharp.png");
            g.psBlobCache["StaticLit"] = g.psBlob;

            g.initialized = true;
            return true;
        }
    }

    void Reset() {
        g.submittedDrawItems.clear();
        g.baseDrawItems.clear();
        g.objectFxBuckets.clear();
        g.runtimeItems.clear();
        GpuResources::ResetMaterialSrvAllocator();
    }

    void SubmitStaticDrawItem(const StaticModelDrawItem& item) {
        if (item.registry == nullptr || item.gpuMeshId == 0) {
            return;
        }

        g.submittedDrawItems.push_back(item);

        const bool useObjectFx = (item.postGroupMask != 0u) && !item.materialFxProfileId.empty();
        if (!useObjectFx) {
            g.baseDrawItems.push_back(item);
        } else {
            ObjectFxBucketKey key{};
            key.postGroupMask = item.postGroupMask;
            key.materialFxProfileId = item.materialFxProfileId;
            auto& bucket = g.objectFxBuckets[key];
            bucket.push_back(item);
        }

#if defined(_DEBUG)
        char msg[256]{};
        std::snprintf(
            msg,
            sizeof(msg),
            "[MeshRenderer] SubmitStaticDrawItem base=%zu objectFxBuckets=%zu (mask=%u, profile=%s)\n",
            g.baseDrawItems.size(),
            g.objectFxBuckets.size(),
            item.postGroupMask,
            item.materialFxProfileId.empty() ? "<none>" : item.materialFxProfileId.c_str());
        OutputDebugStringA(msg);
        for (const auto& [bucketKey, bucketItems] : g.objectFxBuckets) {
            std::snprintf(
                msg,
                sizeof(msg),
                "[MeshRenderer]   bucket mask=%u profile=%s count=%zu\n",
                bucketKey.postGroupMask,
                bucketKey.materialFxProfileId.c_str(),
                bucketItems.size());
            OutputDebugStringA(msg);
        }
#endif
    }

    const std::vector<StaticModelDrawItem>& GetSubmittedDrawItems() {
        return g.submittedDrawItems;
    }

    size_t GetObjectFxBucketCount() {
        return g.objectFxBuckets.size();
    }

    const char* ResolveDrawItemBucketTag(const StaticModelDrawItem& item) {
        const bool useObjectFx = (item.postGroupMask != 0u) && !item.materialFxProfileId.empty();
        return useObjectFx ? "ObjectFx" : "Base";
    }

    SubmissionRendererDebugOptions& GetDebugOptions() {
        static SubmissionRendererDebugOptions options{};
        return options;
    }

    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment) {
        if (g.submittedDrawItems.empty()) {
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

        MATH::Vec3 normalizedDir = MATH::Normalize(environment.directional.direction);
        if (GetDebugOptions().rotateLight) {
            const FrameContext& frame = TIME::GetFrameContext();
            const float t = static_cast<float>(std::fmod(static_cast<double>(frame.frameIndex) * frame.unscaledDt, 1000.0));
            normalizedDir = MATH::Normalize(MATH::Vec3{ std::cos(t * 0.5f), normalizedDir.y, std::sin(t * 0.5f) });
        }
        g.lightMapped->directionalDir = { normalizedDir.x, normalizedDir.y, normalizedDir.z, 0.0f };
        g.lightMapped->directionalColor = { environment.directional.color.x, environment.directional.color.y, environment.directional.color.z, 1.0f };
        g.lightMapped->ambientColor = { environment.ambient.color.x, environment.ambient.color.y, environment.ambient.color.z, 1.0f };
        g.lightMapped->specularParams = { std::max(0.0f, environment.specularIntensity), std::max(1.0f, environment.specularPower), 0.0f, 0.0f };
        g.lightMapped->directionalIntensity = environment.directional.enabled ? std::max(0.0f, environment.directional.intensity) : 0.0f;
        g.lightMapped->ambientIntensity = std::max(0.0f, environment.ambient.intensity);
        g.lightMapped->pointLightCount = 0;
        for (size_t i = 0; i < std::size(g.lightMapped->pointLightPosRange); ++i) {
            g.lightMapped->pointLightPosRange[i] = {};
            g.lightMapped->pointLightColorIntensity[i] = {};
        }
        constexpr uint32_t kMaxPointLights = 8;
        for (const PointLight& pointLight : environment.pointLights) {
            if (!pointLight.enabled || g.lightMapped->pointLightCount >= kMaxPointLights) {
                continue;
            }
            const uint32_t index = g.lightMapped->pointLightCount++;
            g.lightMapped->pointLightPosRange[index] = {
                pointLight.position.x,
                pointLight.position.y,
                pointLight.position.z,
                std::max(0.001f, pointLight.range)
            };
            g.lightMapped->pointLightColorIntensity[index] = {
                pointLight.color.x,
                pointLight.color.y,
                pointLight.color.z,
                std::max(0.0f, pointLight.intensity)
            };
        }

        cmd->SetGraphicsRootSignature(g.rootSig.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->SetGraphicsRootConstantBufferView(0, g.cameraCB->GetGPUVirtualAddress());
        cmd->SetGraphicsRootConstantBufferView(2, g.lightCB->GetGPUVirtualAddress());

        constexpr UINT kObjectStride = (sizeof(ObjectCB) + 255u) & ~255u;

        SubmissionRendererDebugOptions& debugOptions = GetDebugOptions();
        auto prepareRuntimeQueue = [&](const std::vector<StaticModelDrawItem>& queueItems, bool applyMaterialFxProfile, const std::string* overrideProfileId = nullptr) {
            g.runtimeItems.clear();
            g.runtimeItems.reserve(queueItems.size());
            for (const StaticModelDrawItem& item : queueItems) {
                DrawItemRuntime runtime{};
                runtime.item = item;
                ResolveDrawVariant(runtime, applyMaterialFxProfile, overrideProfileId);
                if (const ASSET::MaterialAsset* material = item.registry->FindMaterial(item.material)) {
                    const ASSET::TextureAsset* baseColor = item.registry->FindTexture(material->baseColorTexture);
                    const ASSET::TextureAsset* normal = item.registry->FindTexture(material->normalTexture);
                    const ASSET::TextureAsset* orm = item.registry->FindTexture(material->ormTexture);
                    const ASSET::TextureAsset* emissive = item.registry->FindTexture(material->emissiveTexture);
                    runtime.materialBlockTextureIds[0] = (baseColor && baseColor->gpuResourceId != 0) ? baseColor->gpuResourceId : g.fallbackTextureId;
                    runtime.materialBlockTextureIds[1] = (normal && normal->gpuResourceId != 0) ? normal->gpuResourceId : g.fallbackNormalTextureId;
                    runtime.materialBlockTextureIds[2] = (orm && orm->gpuResourceId != 0) ? orm->gpuResourceId : g.fallbackOrmTextureId;
                    runtime.materialBlockTextureIds[3] = (emissive && emissive->gpuResourceId != 0) ? emissive->gpuResourceId : g.fallbackEmissiveTextureId;
                    if (runtime.alphaMode == ASSET::AlphaMode::Blend) {
                        const MATH::Vec3 cameraToObject = {
                            item.world.m[3][0] - cameraPos.x,
                            item.world.m[3][1] - cameraPos.y,
                            item.world.m[3][2] - cameraPos.z
                        };
                        runtime.sortDistanceSq = cameraToObject.x * cameraToObject.x + cameraToObject.y * cameraToObject.y + cameraToObject.z * cameraToObject.z;
                    }
                } else {
                    runtime.materialBlockTextureIds[0] = g.fallbackTextureId;
                    runtime.materialBlockTextureIds[1] = g.fallbackNormalTextureId;
                    runtime.materialBlockTextureIds[2] = g.fallbackOrmTextureId;
                    runtime.materialBlockTextureIds[3] = g.fallbackEmissiveTextureId;
                }
                g.runtimeItems.push_back(std::move(runtime));
            }
        };

        auto renderRuntimeQueue = [&](const char* /*queueTag*/) {
            std::vector<size_t> opaqueIndices;
            std::vector<size_t> maskedIndices;
            std::vector<size_t> blendedIndices;
            opaqueIndices.reserve(g.runtimeItems.size());
            maskedIndices.reserve(g.runtimeItems.size());
            blendedIndices.reserve(g.runtimeItems.size());
            for (size_t i = 0; i < g.runtimeItems.size(); ++i) {
                const ASSET::AlphaMode alphaMode = g.runtimeItems[i].alphaMode;
                if (alphaMode == ASSET::AlphaMode::Mask) {
                    maskedIndices.push_back(i);
                } else if (alphaMode == ASSET::AlphaMode::Blend) {
                    blendedIndices.push_back(i);
                } else {
                    opaqueIndices.push_back(i);
                }
            }
            std::sort(blendedIndices.begin(), blendedIndices.end(), [](size_t a, size_t b) {
                return g.runtimeItems[a].sortDistanceSq > g.runtimeItems[b].sortDistanceSq;
            });

            auto drawRange = [&](const std::vector<size_t>& indices) {
                ID3D12DescriptorHeap* materialHeap = GpuResources::GetMaterialSrvHeap();
                if (materialHeap != nullptr) {
                    ID3D12DescriptorHeap* heaps[] = { materialHeap };
                    cmd->SetDescriptorHeaps(1, heaps);
                }

                for (size_t drawIndex : indices) {
                    const DrawItemRuntime& runtime = g.runtimeItems[drawIndex];
                    const StaticModelDrawItem& item = runtime.item;
                    if (item.registry == nullptr || item.gpuMeshId == 0) {
                        continue;
                    }

                    ObjectCB obj{};
                    obj.world = item.world;
                    obj.normalMatrix = item.normalMatrix;
                    if (const ASSET::MaterialAsset* material = item.registry->FindMaterial(item.material)) {
                        obj.baseColorFactor = { material->baseColorFactor.x, material->baseColorFactor.y, material->baseColorFactor.z, material->baseColorFactor.w };
                        obj.emissiveFactor = { material->emissiveFactor.x, material->emissiveFactor.y, material->emissiveFactor.z, 0.0f };
                        obj.normalScale = material->normalScale;
                        obj.occlusionStrength = material->occlusionStrength;
                        obj.metallicFactor = material->metallicFactor;
                        obj.roughnessFactor = material->roughnessFactor;
                        obj.hasBaseColorTexture = material->baseColorTexture.IsValid() ? 1u : 0u;
                        obj.hasNormalTexture = (debugOptions.useNormal && material->normalTexture.IsValid()) ? 1u : 0u;
                        obj.hasOrmTexture = material->ormTexture.IsValid() ? 1u : 0u;
                        obj.hasEmissiveTexture = (debugOptions.useEmissive && material->emissiveTexture.IsValid()) ? 1u : 0u;
                        obj.alphaMode = static_cast<uint32_t>(material->alphaMode);
                        obj.alphaCutoff = material->alphaCutoff;
                    } else {
                        obj.baseColorFactor = { 1,1,1,1 };
                        obj.emissiveFactor = { 0,0,0,0 };
                        obj.hasBaseColorTexture = 0u;
                        obj.hasNormalTexture = 0u;
                        obj.hasOrmTexture = 0u;
                        obj.hasEmissiveTexture = 0u;
                        obj.alphaMode = 0u;
                        obj.alphaCutoff = 0.5f;
                    }
                    obj.fxFlags = runtime.fxFlags;
                    obj.fxUser0 = runtime.fxValues[0];
                    obj.fxUser1 = runtime.fxValues[1];
                    obj.fxUser2 = runtime.fxValues[2];
                    obj.fxUser3 = runtime.fxValues[3];

                    uint8_t* dst = reinterpret_cast<uint8_t*>(g.objectMapped) + static_cast<size_t>(kObjectStride) * drawIndex;
                    std::memcpy(dst, &obj, sizeof(ObjectCB));

                    const D3D12_GPU_VIRTUAL_ADDRESS objAddress = g.objectCB->GetGPUVirtualAddress() + static_cast<UINT64>(kObjectStride) * drawIndex;
                    cmd->SetGraphicsRootConstantBufferView(1, objAddress);

                    auto foundPso = g.variantPsoCache.find(runtime.variant);
                    if (foundPso == g.variantPsoCache.end()) {
                        ComPtr<ID3D12PipelineState> variantPso;
                        if (!CreateVariantPipeline(SERVICES::gCtx.device, runtime.variant, variantPso.GetAddressOf())) {
                            variantPso = g.pso;
                        }
                        foundPso = g.variantPsoCache.emplace(runtime.variant, std::move(variantPso)).first;
                    }
                    cmd->SetPipelineState(foundPso->second.Get());

                    const auto materialBlock = GpuResources::AllocateMaterialSrvBlock(
                        runtime.materialBlockTextureIds[0],
                        runtime.materialBlockTextureIds[1],
                        runtime.materialBlockTextureIds[2],
                        runtime.materialBlockTextureIds[3]);
                    if (materialBlock.valid) {
                        cmd->SetGraphicsRootDescriptorTable(3, materialBlock.gpuStart);
                    }

                    const GpuResources::StaticMeshViews views = GpuResources::GetStaticMeshViews(item.gpuMeshId);
                    if (!views.valid) {
                        continue;
                    }
                    cmd->IASetVertexBuffers(0, 1, &views.vbv);
                    cmd->IASetIndexBuffer(&views.ibv);
                    cmd->DrawIndexedInstanced(views.indexCount, 1, 0, 0, 0);
                }
            };

            drawRange(opaqueIndices);
            drawRange(maskedIndices);
            drawRange(blendedIndices);
        };

        GpuResources::ResetMaterialSrvAllocator();

        // Stage A: base static draw.
        prepareRuntimeQueue(g.baseDrawItems, false, nullptr);
        renderRuntimeQueue("base");

        // Stage B: object-level FX buckets.
        for (const auto& [bucketKey, bucket] : g.objectFxBuckets) {
            MaterialFxProfile profile{};
            const bool profileOk = MaterialFxProfile::LoadById(bucketKey.materialFxProfileId, profile);
            char msg[256]{};
            std::snprintf(
                msg,
                sizeof(msg),
                "[MeshRenderer] ObjectFx bucket mask=%u profile=%s count=%zu load=%s\n",
                bucketKey.postGroupMask,
                bucketKey.materialFxProfileId.c_str(),
                bucket.size(),
                profileOk ? "ok" : "fail");
            OutputDebugStringA(msg);

            const std::string* overrideProfile = profileOk ? &bucketKey.materialFxProfileId : nullptr;
            prepareRuntimeQueue(bucket, true, overrideProfile);
            renderRuntimeQueue("object_fx");
        }
    }

} // namespace HIKARI::MESHRENDERER
