#include "HIKARI_ShadowMapRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <d3dcompiler.h>
#include <d3dx12.h>
#include <wrl/client.h>

#include "HIKARI_Services.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/HIKARI_Mesh.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPacket.h"
#include "Render3D/Shadow/HIKARI_ShadowPacketExecutor.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::SHADOW {

    using Microsoft::WRL::ComPtr;

    namespace {
        constexpr UINT kMaxCasterObjects = 2048u;
        constexpr size_t kMaxJointPaletteMatrices = 128u;

        constexpr UINT AlignConstantBufferSize(size_t size) {
            return static_cast<UINT>((size + 255u) & ~255u);
        }

        struct ShadowCameraCB {
            MATH::Mat4 lightViewProj{};
        };

        struct ShadowObjectCB {
            MATH::Mat4 world{};
            uint32_t materialFlags = 0;
            float alphaCutoff = 0.5f;
            float padding[2]{};
        };

        struct JointPaletteCB {
            MATH::Mat4 jointMatrices[kMaxJointPaletteMatrices]{};
        };

        struct DrawItem {
            const ModelAsset* asset = nullptr;
            Transform3D transform{};
            std::vector<MATH::Mat4> jointPalette{};
            bool usePrimitiveFilter = false;
            uint32_t meshIndexFilter = 0;
            uint32_t primitiveIndexFilter = 0;
        };

        struct State {
            bool initialized = false;
            bool frameEnabled = false;
            uint32_t resolution = 0;
            D3D12_RESOURCE_STATES shadowState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            MATH::Mat4 lightViewProj = MATH::Mat4::Identity();

            ComPtr<ID3D12Resource> shadowMap;
            ComPtr<ID3D12DescriptorHeap> dsvHeap;
            D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
            RENDER3D::TextureResourceHandle shadowSrvResource{};
            RENDER3D::TextureResourceHandle fallbackTextureResource{};
            int shadowSrvHandle = -1;
            int fallbackTextureHandle = -1;

            ComPtr<ID3D12RootSignature> rootSig;
            ComPtr<ID3D12RootSignature> skinnedRootSig;
            ComPtr<ID3D12PipelineState> staticPso;
            ComPtr<ID3D12PipelineState> skinnedPso;
            ComPtr<ID3D12Resource> cameraCB;
            ComPtr<ID3D12Resource> objectCB;
            ComPtr<ID3D12Resource> jointPaletteCB;
            ShadowCameraCB* cameraMapped = nullptr;
            ShadowObjectCB* objectMapped = nullptr;
            JointPaletteCB* jointPaletteMapped = nullptr;

            bool acceptingFrameSubmissions = false;
            std::vector<DrawItem> staticItems;
            std::vector<DrawItem> skinnedItems;
            std::vector<DrawItem> pendingStaticItems;
            std::vector<DrawItem> pendingSkinnedItems;
            size_t pendingSkippedNoCastShadowCount = 0;
            const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder* shadowPacketBuilder = nullptr;
            const std::vector<uint32_t>* shadowPacketExecutionIndices = nullptr;
            const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* shadowPacketExecutionCommands = nullptr;
            std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>> primitiveMeshCache;
            std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>> primitiveSkinnedMeshCache;
            std::unordered_map<std::string, RENDER3D::TextureResourceHandle> materialTextureCache;
            ShadowMapDebugStats debugStats;
            size_t shadowMapRecreateCount = 0;
        };

        State g;

        const MaterialAsset* GetPrimitiveMaterial(const ModelAsset& asset, uint32_t materialIndex) {
            if (materialIndex >= asset.materials.size()) {
                return nullptr;
            }
            return &asset.materials[static_cast<size_t>(materialIndex)];
        }

        Mesh* GetOrCreatePrimitiveMesh(const MeshPrimitive& primitive) {
            auto found = g.primitiveMeshCache.find(&primitive);
            if (found != g.primitiveMeshCache.end()) {
                return found->second.get();
            }

            if (primitive.staticVertices.empty() || primitive.indices.empty()) {
                return nullptr;
            }

            std::vector<VertexStatic3D> vertices;
            vertices.reserve(primitive.staticVertices.size());
            for (const Vertex3D& src : primitive.staticVertices) {
                VertexStatic3D dst{};
                dst.position = src.position;
                dst.normal = src.normal;
                dst.tangent = src.tangent;
                if (MATH::Length({ dst.tangent.x, dst.tangent.y, dst.tangent.z }) <= 1e-6f) {
                    dst.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
                }
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
                return found->second.get();
            }

            if (primitive.skinnedVertices.empty() || primitive.indices.empty()) {
                return nullptr;
            }

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

        RENDER3D::TextureResourceHandle ResolvePrimitiveTextureResource(const ModelAsset& asset, const MaterialAsset* materialAsset) {
            if (materialAsset == nullptr ||
                materialAsset->baseColorTexture.textureIndex < 0 ||
                materialAsset->baseColorTexture.textureIndex >= static_cast<int>(asset.textures.size())) {
                return g.fallbackTextureResource;
            }

            const TextureAsset3D& texture = asset.textures[static_cast<size_t>(materialAsset->baseColorTexture.textureIndex)];
            if (texture.sourcePath.empty()) {
                return g.fallbackTextureResource;
            }

            const std::string cacheKey = "shadow:base:" + texture.sourcePath;
            auto found = g.materialTextureCache.find(cacheKey);
            if (found != g.materialTextureCache.end() &&
                RENDER3D::IsTextureResourceValid(found->second)) {
                return found->second;
            }

            RENDER3D::TextureResourceHandle resource =
                RENDER3D::LoadTextureResourceSrgb(cacheKey, texture.sourcePath);
            g.materialTextureCache[cacheKey] = resource;
            return RENDER3D::IsTextureResourceValid(resource) ? resource : g.fallbackTextureResource;
        }

        size_t UploadJointPalette(size_t objectIndex, const std::vector<MATH::Mat4>& palette) {
            if (g.jointPaletteMapped == nullptr || objectIndex >= kMaxCasterObjects) {
                return 0;
            }

            JointPaletteCB cb{};
            for (MATH::Mat4& matrix : cb.jointMatrices) {
                matrix = MATH::Mat4::Identity();
            }
            const size_t uploadCount = std::min(palette.size(), kMaxJointPaletteMatrices);
            for (size_t i = 0; i < uploadCount; ++i) {
                cb.jointMatrices[i] = palette[i];
            }

            uint8_t* dst = reinterpret_cast<uint8_t*>(g.jointPaletteMapped) +
                static_cast<size_t>(AlignConstantBufferSize(sizeof(JointPaletteCB))) * objectIndex;
            std::memcpy(dst, &cb, sizeof(cb));
            return uploadCount;
        }

        bool CreateBuffers(ID3D12Device* device) {
            const UINT cameraBytes = AlignConstantBufferSize(sizeof(ShadowCameraCB));
            const UINT objectBytes = AlignConstantBufferSize(sizeof(ShadowObjectCB)) * kMaxCasterObjects;
            const UINT paletteBytes = AlignConstantBufferSize(sizeof(JointPaletteCB)) * kMaxCasterObjects;
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

            auto paletteDesc = CD3DX12_RESOURCE_DESC::Buffer(paletteBytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &paletteDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.jointPaletteCB.GetAddressOf())))) {
                return false;
            }
            return SUCCEEDED(g.jointPaletteCB->Map(0, nullptr, reinterpret_cast<void**>(&g.jointPaletteMapped)));
        }

        bool CreateShadowMap(uint32_t resolution) {
            auto* device = SERVICES::gCtx.device;
            if (device == nullptr || resolution == 0) {
                return false;
            }

            RENDER3D::ReleaseTextureResource(g.shadowSrvResource);
            g.shadowSrvResource = {};
            g.shadowSrvHandle = -1;

            g.shadowMap.Reset();
            g.dsvHeap.Reset();

            D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
            dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            dsvHeapDesc.NumDescriptors = 1;
            if (FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(g.dsvHeap.GetAddressOf())))) {
                return false;
            }

            auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
                DXGI_FORMAT_R32_TYPELESS,
                resolution,
                resolution,
                1,
                1,
                1,
                0,
                D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
            D3D12_CLEAR_VALUE clearValue{};
            clearValue.Format = DXGI_FORMAT_D32_FLOAT;
            clearValue.DepthStencil.Depth = 1.0f;
            clearValue.DepthStencil.Stencil = 0;
            const HRESULT hr = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &texDesc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                &clearValue,
                IID_PPV_ARGS(g.shadowMap.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "ShadowMapRenderer::CreateShadowMapResource")) {
                return false;
            }
            GFX::SetD3D12Name(g.shadowMap.Get(), L"Directional Shadow Map");

            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            g.dsv = g.dsvHeap->GetCPUDescriptorHandleForHeapStart();
            device->CreateDepthStencilView(g.shadowMap.Get(), &dsvDesc, g.dsv);

            RENDER3D::RenderResourceDesc shadowSrvDesc{};
            shadowSrvDesc.kind = RENDER3D::RenderResourceKind::Texture;
            shadowSrvDesc.usage =
                RENDER3D::RenderResourceUsageFlags::ShaderResource |
                RENDER3D::RenderResourceUsageFlags::DepthStencil;
            shadowSrvDesc.lifetime = RENDER3D::RenderResourceLifetime::External;
            shadowSrvDesc.debugName = "directional_shadow_map_srv";
            shadowSrvDesc.sourceKey = "shadow/directional_map";
            shadowSrvDesc.width = resolution;
            shadowSrvDesc.height = resolution;
            shadowSrvDesc.mipLevels = 1;
            shadowSrvDesc.depthOrArraySize = 1;
            shadowSrvDesc.format = DXGI_FORMAT_R32_FLOAT;
            g.shadowSrvResource = RENDER3D::RegisterTextureResourceFromNative(
                g.shadowMap.Get(),
                DXGI_FORMAT_R32_FLOAT,
                std::move(shadowSrvDesc));
            g.shadowSrvHandle = RENDER3D::GetTextureResourceBackendHandle(g.shadowSrvResource);
            g.resolution = resolution;
            g.shadowState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            ++g.shadowMapRecreateCount;
            return RENDER3D::IsTextureResourceValid(g.shadowSrvResource);
        }

        bool CreatePipeline(ID3D12Device* device) {
            if (!GFX::SupportsShaderModel6(device)) {
                DEBUGLOG::PushRenderError("[Shadow][ERROR] Shader Model 6.0 is not supported by this device.");
                return false;
            }

            ComPtr<ID3DBlob> staticVs;
            ComPtr<ID3DBlob> skinnedVs;
            ComPtr<ID3DBlob> ps;
            if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_ShadowStaticVS.hlsl", "main", GFX::ShaderStage::Vertex, staticVs.GetAddressOf())) {
                DEBUGLOG::PushRenderError("[Shadow][ERROR] Compile ShadowStaticVS failed.");
                return false;
            }
            if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_ShadowSkinnedVS.hlsl", "main", GFX::ShaderStage::Vertex, skinnedVs.GetAddressOf())) {
                DEBUGLOG::PushRenderError("[Shadow][ERROR] Compile ShadowSkinnedVS failed.");
                return false;
            }
            if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_ShadowAlphaPS.hlsl", "main", GFX::ShaderStage::Pixel, ps.GetAddressOf())) {
                DEBUGLOG::PushRenderError("[Shadow][ERROR] Compile ShadowAlphaPS failed.");
                return false;
            }

            D3D12_DESCRIPTOR_RANGE textureRange{};
            textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            textureRange.NumDescriptors = 1;
            textureRange.BaseShaderRegister = 0;
            textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_DESCRIPTOR_RANGE objectDataRange{};
            objectDataRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            objectDataRange.NumDescriptors = 1;
            objectDataRange.BaseShaderRegister = 1;
            objectDataRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER params[5]{};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            params[0].Descriptor.ShaderRegister = 0;
            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[1].Descriptor.ShaderRegister = 1;
            params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[2].DescriptorTable.NumDescriptorRanges = 1;
            params[2].DescriptorTable.pDescriptorRanges = &textureRange;
            params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            params[3].DescriptorTable.NumDescriptorRanges = 1;
            params[3].DescriptorTable.pDescriptorRanges = &objectDataRange;
            params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            params[4].Constants.ShaderRegister = 2;
            params[4].Constants.Num32BitValues = 2;

            D3D12_STATIC_SAMPLER_DESC sampler{};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ShaderRegister = 0;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = static_cast<UINT>(std::size(params));
            rsDesc.pParameters = params;
            rsDesc.NumStaticSamplers = 1;
            rsDesc.pStaticSamplers = &sampler;
            rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ComPtr<ID3DBlob> sigBlob;
            ComPtr<ID3DBlob> errBlob;
            if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, sigBlob.GetAddressOf(), errBlob.GetAddressOf()))) {
                if (errBlob) OutputDebugStringA(static_cast<const char*>(errBlob->GetBufferPointer()));
                return false;
            }
            HRESULT hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(g.rootSig.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "CreateRootSignature: Shadow static")) {
                return false;
            }
            GFX::SetD3D12Name(g.rootSig.Get(), L"Shadow Static RootSignature");

            D3D12_ROOT_PARAMETER skinnedParams[4]{};
            for (size_t i = 0; i < 3; ++i) {
                skinnedParams[i] = params[i];
            }
            skinnedParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            skinnedParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            skinnedParams[3].Descriptor.ShaderRegister = 3;

            D3D12_ROOT_SIGNATURE_DESC skinnedRsDesc = rsDesc;
            skinnedRsDesc.NumParameters = static_cast<UINT>(std::size(skinnedParams));
            skinnedRsDesc.pParameters = skinnedParams;
            sigBlob.Reset();
            errBlob.Reset();
            if (FAILED(D3D12SerializeRootSignature(&skinnedRsDesc, D3D_ROOT_SIGNATURE_VERSION_1, sigBlob.GetAddressOf(), errBlob.GetAddressOf()))) {
                if (errBlob) OutputDebugStringA(static_cast<const char*>(errBlob->GetBufferPointer()));
                return false;
            }
            hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(g.skinnedRootSig.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "CreateRootSignature: Shadow skinned")) {
                return false;
            }
            GFX::SetD3D12Name(g.skinnedRootSig.Get(), L"Shadow Skinned RootSignature");

            const D3D12_INPUT_ELEMENT_DESC staticInput[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, normal)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, tangent)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, u)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };
            const D3D12_INPUT_ELEMENT_DESC skinnedInput[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, normal)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, tangent)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, uv0)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, uv1)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, color0)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "JOINTS", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, joints)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "WEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, weights)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = g.rootSig.Get();
            psoDesc.VS = { staticVs->GetBufferPointer(), staticVs->GetBufferSize() };
            psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = TRUE;
            psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            psoDesc.InputLayout = { staticInput, static_cast<UINT>(std::size(staticInput)) };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 0;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = 1;
            hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(g.staticPso.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "Create PSO: Shadow static")) {
                return false;
            }
            GFX::SetD3D12Name(g.staticPso.Get(), L"Shadow Static PSO");

            D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedPsoDesc = psoDesc;
            skinnedPsoDesc.pRootSignature = g.skinnedRootSig.Get();
            skinnedPsoDesc.VS = { skinnedVs->GetBufferPointer(), skinnedVs->GetBufferSize() };
            skinnedPsoDesc.InputLayout = { skinnedInput, static_cast<UINT>(std::size(skinnedInput)) };
            hr = device->CreateGraphicsPipelineState(&skinnedPsoDesc, IID_PPV_ARGS(g.skinnedPso.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "Create PSO: Shadow skinned")) {
                return false;
            }
            GFX::SetD3D12Name(g.skinnedPso.Get(), L"Shadow Skinned PSO");
            return true;
        }

        bool EnsureInitialized() {
            if (g.initialized) {
                return true;
            }
            auto* device = SERVICES::gCtx.device;
            if (device == nullptr) {
                return false;
            }
            g.fallbackTextureResource = RENDER3D::LoadTextureResource(
                "shadow/fallback_white",
                "HIKARI/white1x1.png");
            g.fallbackTextureHandle =
                RENDER3D::GetTextureResourceBackendHandle(g.fallbackTextureResource);
            g.initialized =
                RENDER3D::IsTextureResourceValid(g.fallbackTextureResource) &&
                CreateBuffers(device) &&
                PACKET::InitializeShadowPacketExecutor(device) &&
                CreatePipeline(device);
            return g.initialized;
        }

        uint32_t ResolveShadowResolution(uint32_t resolution) {
            if (resolution <= 1024) {
                return 1024;
            }
            if (resolution <= 2048) {
                return 2048;
            }
            return 4096;
        }

        MATH::Mat4 BuildLightViewProj(const SceneEnvironment& environment, const Camera3D& camera) {
            MATH::Vec3 lightDir = MATH::Normalize(environment.directional.direction);
            if (MATH::Length(lightDir) <= 1e-6f) {
                lightDir = MATH::Normalize(MATH::Vec3{ 0.4f, -1.0f, -0.6f });
            }
            const MATH::Vec3 center = camera.GetPosition();
            const float lightDistance = std::max(1.0f, environment.directionalShadow.shadowDistance);
            const MATH::Vec3 lightPos = center - lightDir * lightDistance;
            MATH::Vec3 up{ 0.0f, 1.0f, 0.0f };
            if (std::abs(MATH::Dot(lightDir, up)) > 0.95f) {
                up = { 1.0f, 0.0f, 0.0f };
            }
            MATH::Mat4 view = MATH::Mat4::LookAtRH(lightPos, center, up);
            const float orthoSize = std::max(1.0f, environment.directionalShadow.orthoSize);
            const float nearPlane = std::max(0.001f, environment.directionalShadow.nearPlane);
            const float farPlane = std::max(nearPlane + 0.01f, environment.directionalShadow.farPlane);
            if (environment.directionalShadow.stabilize && g.resolution > 0) {
                const float unitsPerTexel = orthoSize / static_cast<float>(g.resolution);
                const MATH::Vec4 centerLS = view.TransformPoint({ center.x, center.y, center.z, 1.0f });
                const float snappedX = std::floor(centerLS.x / unitsPerTexel) * unitsPerTexel;
                const float snappedY = std::floor(centerLS.y / unitsPerTexel) * unitsPerTexel;
                view.m[3][0] += snappedX - centerLS.x;
                view.m[3][1] += snappedY - centerLS.y;
            }
            return MATH::Mat4::OrthoRH_ZO(orthoSize, orthoSize, nearPlane, farPlane) * view;
        }

        void SubmitDebugFrustum(const SceneEnvironment& environment, const Camera3D& camera) {
            if (!environment.directionalShadow.showDebugFrustum) {
                return;
            }

            MATH::Vec3 lightDir = MATH::Normalize(environment.directional.direction);
            if (MATH::Length(lightDir) <= 1e-6f) {
                lightDir = MATH::Normalize(MATH::Vec3{ 0.4f, -1.0f, -0.6f });
            }
            const MATH::Vec3 center = camera.GetPosition();
            const float lightDistance = std::max(1.0f, environment.directionalShadow.shadowDistance);
            const MATH::Vec3 lightPos = center - lightDir * lightDistance;
            MATH::Vec3 up{ 0.0f, 1.0f, 0.0f };
            if (std::abs(MATH::Dot(lightDir, up)) > 0.95f) {
                up = { 1.0f, 0.0f, 0.0f };
            }
            const MATH::Vec3 forward = MATH::Normalize(center - lightPos);
            const MATH::Vec3 right = MATH::Normalize(MATH::Cross(up, forward));
            const MATH::Vec3 actualUp = MATH::Cross(forward, right);
            const float half = std::max(1.0f, environment.directionalShadow.orthoSize) * 0.5f;
            const float nearPlane = std::max(0.001f, environment.directionalShadow.nearPlane);
            const float farPlane = std::max(nearPlane + 0.01f, environment.directionalShadow.farPlane);
            const MATH::Vec3 nearCenter = lightPos + forward * nearPlane;
            const MATH::Vec3 farCenter = lightPos + forward * farPlane;

            const std::array<MATH::Vec3, 8> corners = {
                nearCenter - right * half - actualUp * half,
                nearCenter + right * half - actualUp * half,
                nearCenter + right * half + actualUp * half,
                nearCenter - right * half + actualUp * half,
                farCenter - right * half - actualUp * half,
                farCenter + right * half - actualUp * half,
                farCenter + right * half + actualUp * half,
                farCenter - right * half + actualUp * half,
            };
            constexpr uint32_t color = 0xFFD45CFF;
            const auto submit = [&](int a, int b) {
                RENDERER3D::DEBUG::SubmitLine3D({
                    corners[static_cast<size_t>(a)],
                    corners[static_cast<size_t>(b)],
                    color,
                    RENDERER3D::DEBUG::DebugDepthMode::XRay
                });
            };
            submit(0, 1); submit(1, 2); submit(2, 3); submit(3, 0);
            submit(4, 5); submit(5, 6); submit(6, 7); submit(7, 4);
            submit(0, 4); submit(1, 5); submit(2, 6); submit(3, 7);
        }

        void RestoreMainRenderTarget() {
            if (POST::PostSystem::RebindCurrentRenderTarget()) {
                return;
            }

            auto* cmd = SERVICES::gCtx.cmdList;
            if (cmd == nullptr) {
                return;
            }
            cmd->OMSetRenderTargets(1, &SERVICES::gCtx.rtv, FALSE, &SERVICES::gCtx.dsv);
            D3D12_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(SERVICES::gCtx.backBufferWidth);
            viewport.Height = static_cast<float>(SERVICES::gCtx.backBufferHeight);
            viewport.MaxDepth = 1.0f;
            D3D12_RECT scissor{ 0, 0, SERVICES::gCtx.backBufferWidth, SERVICES::gCtx.backBufferHeight };
            cmd->RSSetViewports(1, &viewport);
            cmd->RSSetScissorRects(1, &scissor);
        }

        void ClearFrameSubmissions() {
            g.staticItems.clear();
            g.skinnedItems.clear();
            g.debugStats = {};
            g.acceptingFrameSubmissions = false;
        }

        void ClearPendingSubmissions() {
            g.pendingStaticItems.clear();
            g.pendingSkinnedItems.clear();
            g.pendingSkippedNoCastShadowCount = 0;
        }

        void FlushPendingSubmissions() {
            if (!g.pendingStaticItems.empty()) {
                g.debugStats.submittedCasterCount += g.pendingStaticItems.size();
                g.staticItems.insert(
                    g.staticItems.end(),
                    std::make_move_iterator(g.pendingStaticItems.begin()),
                    std::make_move_iterator(g.pendingStaticItems.end()));
                g.pendingStaticItems.clear();
            }
            if (!g.pendingSkinnedItems.empty()) {
                g.debugStats.submittedCasterCount += g.pendingSkinnedItems.size();
                g.skinnedItems.insert(
                    g.skinnedItems.end(),
                    std::make_move_iterator(g.pendingSkinnedItems.begin()),
                    std::make_move_iterator(g.pendingSkinnedItems.end()));
                g.pendingSkinnedItems.clear();
            }
            if (g.pendingSkippedNoCastShadowCount > 0) {
                g.debugStats.skippedNoCastShadowCount += g.pendingSkippedNoCastShadowCount;
                g.pendingSkippedNoCastShadowCount = 0;
            }
        }

        void QueueStaticItem(DrawItem&& item) {
            if (g.acceptingFrameSubmissions && g.frameEnabled) {
                g.staticItems.push_back(std::move(item));
                ++g.debugStats.submittedCasterCount;
                return;
            }
            g.pendingStaticItems.push_back(std::move(item));
        }

        void QueueSkinnedItem(DrawItem&& item) {
            if (g.acceptingFrameSubmissions && g.frameEnabled) {
                g.skinnedItems.push_back(std::move(item));
                ++g.debugStats.submittedCasterCount;
                return;
            }
            g.pendingSkinnedItems.push_back(std::move(item));
        }

        void CountSkippedNoCastShadow() {
            if (g.acceptingFrameSubmissions && g.frameEnabled) {
                ++g.debugStats.skippedNoCastShadowCount;
                return;
            }
            ++g.pendingSkippedNoCastShadowCount;
        }

        bool HasShadowPacketExecutionPlan() {
            return
                g.shadowPacketBuilder != nullptr &&
                g.shadowPacketExecutionIndices != nullptr &&
                g.shadowPacketExecutionCommands != nullptr &&
                !g.shadowPacketExecutionCommands->empty();
        }
    }

    void Reset() {
        ClearFrameSubmissions();
        ClearPendingSubmissions();
        SetSurfaceDrawPacketExecutionPlan(nullptr, nullptr, nullptr);
    }

    void BeginFrame(const SceneEnvironment& environment, const Camera3D& camera) {
        ClearFrameSubmissions();
        g.frameEnabled = environment.directional.enabled && environment.directionalShadow.enabled;
        g.debugStats.enabled = g.frameEnabled;
        g.debugStats.resolution = ResolveShadowResolution(environment.directionalShadow.resolution);
        g.debugStats.shadowMapRecreateCount = g.shadowMapRecreateCount;
        g.debugStats.pcfEnabled = environment.directionalShadow.pcfEnabled ? 1u : 0u;
        g.debugStats.pcfRadius = environment.directionalShadow.pcfRadius;
        g.debugStats.orthoSize = environment.directionalShadow.orthoSize;
        g.debugStats.nearPlane = environment.directionalShadow.nearPlane;
        g.debugStats.farPlane = environment.directionalShadow.farPlane;
        g.debugStats.depthBias = environment.directionalShadow.depthBias;
        g.debugStats.normalBias = environment.directionalShadow.normalBias;
        g.debugStats.strength = environment.directionalShadow.strength;
        if (!g.frameEnabled) {
            ClearPendingSubmissions();
            return;
        }
        if (!EnsureInitialized()) {
            g.frameEnabled = false;
            g.debugStats.enabled = false;
            ClearPendingSubmissions();
            return;
        }

        const uint32_t resolution = ResolveShadowResolution(environment.directionalShadow.resolution);
        if (g.shadowMap == nullptr || g.resolution != resolution) {
            if (!CreateShadowMap(resolution)) {
                g.frameEnabled = false;
                g.debugStats.enabled = false;
                ClearPendingSubmissions();
                return;
            }
            g.debugStats.shadowMapRecreateCount = g.shadowMapRecreateCount;
        }
        g.lightViewProj = BuildLightViewProj(environment, camera);
        SubmitDebugFrustum(environment, camera);
        if (g.cameraMapped != nullptr) {
            g.cameraMapped->lightViewProj = g.lightViewProj;
        }
        g.acceptingFrameSubmissions = true;
        FlushPendingSubmissions();
    }

    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform, bool castShadow) {
        if (!castShadow) {
            CountSkippedNoCastShadow();
            return;
        }
        DrawItem item{};
        item.asset = &asset;
        item.transform = transform;
        QueueStaticItem(std::move(item));
    }

    void SubmitStaticSubmesh(
        const ModelAsset& asset,
        const Transform3D& transform,
        uint32_t meshIndex,
        uint32_t primitiveIndex,
        bool castShadow) {

        if (!castShadow) {
            CountSkippedNoCastShadow();
            return;
        }
        if (meshIndex >= asset.meshes.size()) {
            return;
        }
        const MeshAsset& mesh = asset.meshes[meshIndex];
        if (primitiveIndex >= mesh.primitives.size()) {
            return;
        }

        DrawItem item{};
        item.asset = &asset;
        item.transform = transform;
        item.usePrimitiveFilter = true;
        item.meshIndexFilter = meshIndex;
        item.primitiveIndexFilter = primitiveIndex;
        QueueStaticItem(std::move(item));
    }

    void SubmitSkinnedMesh(const ModelAsset& asset, const Transform3D& transform, const std::vector<MATH::Mat4>& jointPalette, bool castShadow) {
        if (!castShadow) {
            CountSkippedNoCastShadow();
            return;
        }
        DrawItem item{};
        item.asset = &asset;
        item.transform = transform;
        item.jointPalette = jointPalette;
        QueueSkinnedItem(std::move(item));
    }

    void SetSurfaceDrawPacketExecutionPlan(
        const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder* builder,
        const std::vector<uint32_t>* executablePacketIndices,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* executableCommands) {

        g.shadowPacketBuilder = builder;
        g.shadowPacketExecutionIndices = executablePacketIndices;
        g.shadowPacketExecutionCommands = executableCommands;
    }

    void RenderDirectionalShadowMap() {
        if (!g.frameEnabled || g.shadowMap == nullptr) {
            g.acceptingFrameSubmissions = false;
            return;
        }
        auto* cmd = SERVICES::gCtx.cmdList;
        if (cmd == nullptr) {
            g.acceptingFrameSubmissions = false;
            return;
        }
        GFX::GPU_PROFILE::ScopedGpuTimer gpuShadow(cmd, GFX::GPU_PROFILE::Pass::ShadowMap);

        if (g.shadowState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(g.shadowMap.Get(), g.shadowState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            cmd->ResourceBarrier(1, &barrier);
            g.shadowState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }

        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(g.resolution);
        viewport.Height = static_cast<float>(g.resolution);
        viewport.MaxDepth = 1.0f;
        D3D12_RECT scissor{ 0, 0, static_cast<LONG>(g.resolution), static_cast<LONG>(g.resolution) };
        cmd->RSSetViewports(1, &viewport);
        cmd->RSSetScissorRects(1, &scissor);
        cmd->OMSetRenderTargets(0, nullptr, FALSE, &g.dsv);
        cmd->ClearDepthStencilView(g.dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
        if (srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            cmd->SetDescriptorHeaps(1, heaps);
        }

        constexpr UINT objectStride = AlignConstantBufferSize(sizeof(ShadowObjectCB));
        size_t objectIndex = 0;
        auto drawPrimitive = [&](const DrawItem& item, const MeshPrimitive& primitive, Mesh* mesh, bool skinned) {
            if (objectIndex >= kMaxCasterObjects || mesh == nullptr || !mesh->IsValid()) {
                return;
            }
            const MaterialAsset* materialAsset = GetPrimitiveMaterial(*item.asset, primitive.materialIndex);
            ShadowObjectCB object{};
            object.world = item.transform.GetWorldMatrix();
            object.alphaCutoff = materialAsset ? materialAsset->alphaCutoff : 0.5f;
            if (materialAsset && materialAsset->alphaMode == AlphaMode::Mask) {
                object.materialFlags |= MATERIAL_FEATURES::AlphaMask;
                ++g.debugStats.alphaMaskCasterDrawCount;
            }

            uint8_t* dst = reinterpret_cast<uint8_t*>(g.objectMapped) + static_cast<size_t>(objectStride) * objectIndex;
            std::memcpy(dst, &object, sizeof(object));
            const D3D12_GPU_VIRTUAL_ADDRESS objectAddress = g.objectCB->GetGPUVirtualAddress() + static_cast<UINT64>(objectStride) * objectIndex;

            cmd->SetGraphicsRootSignature(skinned ? g.skinnedRootSig.Get() : g.rootSig.Get());
            cmd->SetPipelineState(skinned ? g.skinnedPso.Get() : g.staticPso.Get());
            cmd->SetGraphicsRootConstantBufferView(0, g.cameraCB->GetGPUVirtualAddress());
            cmd->SetGraphicsRootConstantBufferView(1, objectAddress);
            const RENDER3D::TextureResourceHandle textureResource =
                ResolvePrimitiveTextureResource(*item.asset, materialAsset);
            const D3D12_GPU_DESCRIPTOR_HANDLE textureSrv =
                RENDER3D::GetTextureResourceSrvGpuHandle(textureResource);
            if (textureSrv.ptr != 0) {
                cmd->SetGraphicsRootDescriptorTable(2, textureSrv);
            }
            if (!skinned) {
                PACKET::BindLegacyShadowObjectDataMode(cmd);
            }
            if (skinned) {
                UploadJointPalette(objectIndex, item.jointPalette);
                const D3D12_GPU_VIRTUAL_ADDRESS paletteAddress = g.jointPaletteCB->GetGPUVirtualAddress() +
                    static_cast<UINT64>(AlignConstantBufferSize(sizeof(JointPaletteCB))) * objectIndex;
                cmd->SetGraphicsRootConstantBufferView(3, paletteAddress);
                ++g.debugStats.skinnedCasterDrawCount;
            } else {
                ++g.debugStats.staticCasterDrawCount;
            }

            D3D12_VERTEX_BUFFER_VIEW vb = mesh->GetVBView();
            D3D12_INDEX_BUFFER_VIEW ib = mesh->GetIBView();
            cmd->IASetVertexBuffers(0, 1, &vb);
            cmd->IASetIndexBuffer(&ib);
            cmd->DrawIndexedInstanced(mesh->GetIndexCount(), 1, 0, 0, 0);
            ++g.debugStats.totalPrimitiveCasterDrawCount;
            ++objectIndex;
        };

        if (HasShadowPacketExecutionPlan()) {
            const std::vector<RENDER3D::RUNTIME::SurfaceDrawPacket>& packets =
                g.shadowPacketBuilder->GetPackets();

            PACKET::ShadowPacketExecutorContext packetCtx{};
            packetCtx.cmd = cmd;
            packetCtx.staticRootSig = g.rootSig.Get();
            packetCtx.staticPso = g.staticPso.Get();
            packetCtx.cameraAddress = g.cameraCB ? g.cameraCB->GetGPUVirtualAddress() : 0;
            packetCtx.resolveStaticMesh = &GetOrCreatePrimitiveMesh;
            packetCtx.resolveBaseColorTexture = &ResolvePrimitiveTextureResource;

            const PACKET::ShadowPacketDrawResult packetResult =
                PACKET::DrawShadowPacketCommands(
                    packetCtx,
                    packets.data(),
                    packets.size(),
                    g.shadowPacketExecutionIndices->data(),
                    g.shadowPacketExecutionIndices->size(),
                    *g.shadowPacketExecutionCommands,
                    objectIndex);

            g.debugStats.shadowPacketCasterDrawCount += packetResult.submittedPacketCount;
            g.debugStats.shadowPacketSkippedCount += packetResult.skippedPacketCount;
            g.debugStats.shadowPacketCommandCount += packetResult.commandCount;
            g.debugStats.shadowPacketSingleCommandCount += packetResult.singlePacketCommandCount;
            g.debugStats.shadowPacketMaxCommandPacketCount =
                (std::max)(g.debugStats.shadowPacketMaxCommandPacketCount, packetResult.maxCommandPacketCount);
            g.debugStats.shadowPacketDrawCallCount += packetResult.drawCallCount;
            g.debugStats.shadowPacketInstancedDrawCount += packetResult.instancedDrawCount;
            g.debugStats.shadowPacketInstancedCasterCount += packetResult.instancedPacketCount;
            g.debugStats.shadowPacketMaxInstanceCount =
                (std::max)(g.debugStats.shadowPacketMaxInstanceCount, packetResult.maxInstanceCount);
            g.debugStats.staticCasterDrawCount += packetResult.submittedPacketCount;
            g.debugStats.totalPrimitiveCasterDrawCount += packetResult.submittedPacketCount;
            g.debugStats.submittedCasterCount += packetResult.submittedPacketCount;
        }

        for (const DrawItem& item : g.staticItems) {
            if (item.asset == nullptr) {
                continue;
            }
            if (!item.asset->meshes.empty()) {
                for (size_t meshIndex = 0; meshIndex < item.asset->meshes.size(); ++meshIndex) {
                    if (item.usePrimitiveFilter && meshIndex != item.meshIndexFilter) {
                        continue;
                    }
                    const MeshAsset& meshAsset = item.asset->meshes[meshIndex];
                    for (size_t primitiveIndex = 0; primitiveIndex < meshAsset.primitives.size(); ++primitiveIndex) {
                        if (item.usePrimitiveFilter && primitiveIndex != item.primitiveIndexFilter) {
                            continue;
                        }
                        const MeshPrimitive& primitive = meshAsset.primitives[primitiveIndex];
                        drawPrimitive(item, primitive, GetOrCreatePrimitiveMesh(primitive), false);
                    }
                }
            } else if (const Mesh* legacyMesh = item.asset->GetMesh()) {
                if (item.usePrimitiveFilter) {
                    continue;
                }
                if (objectIndex >= kMaxCasterObjects || !legacyMesh->IsValid()) {
                    continue;
                }
                ShadowObjectCB object{};
                object.world = item.transform.GetWorldMatrix();
                uint8_t* dst = reinterpret_cast<uint8_t*>(g.objectMapped) + static_cast<size_t>(objectStride) * objectIndex;
                std::memcpy(dst, &object, sizeof(object));
                const D3D12_GPU_VIRTUAL_ADDRESS objectAddress = g.objectCB->GetGPUVirtualAddress() + static_cast<UINT64>(objectStride) * objectIndex;

                cmd->SetGraphicsRootSignature(g.rootSig.Get());
                cmd->SetPipelineState(g.staticPso.Get());
                cmd->SetGraphicsRootConstantBufferView(0, g.cameraCB->GetGPUVirtualAddress());
                cmd->SetGraphicsRootConstantBufferView(1, objectAddress);
                const D3D12_GPU_DESCRIPTOR_HANDLE textureSrv =
                    RENDER3D::GetTextureResourceSrvGpuHandle(g.fallbackTextureResource);
                if (textureSrv.ptr != 0) {
                    cmd->SetGraphicsRootDescriptorTable(2, textureSrv);
                }
                PACKET::BindLegacyShadowObjectDataMode(cmd);

                D3D12_VERTEX_BUFFER_VIEW vb = legacyMesh->GetVBView();
                D3D12_INDEX_BUFFER_VIEW ib = legacyMesh->GetIBView();
                cmd->IASetVertexBuffers(0, 1, &vb);
                cmd->IASetIndexBuffer(&ib);
                cmd->DrawIndexedInstanced(legacyMesh->GetIndexCount(), 1, 0, 0, 0);
                ++g.debugStats.staticCasterDrawCount;
                ++g.debugStats.totalPrimitiveCasterDrawCount;
                ++objectIndex;
            }
        }

        for (const DrawItem& item : g.skinnedItems) {
            if (item.asset == nullptr || item.jointPalette.empty()) {
                continue;
            }
            for (const MeshAsset& meshAsset : item.asset->meshes) {
                for (const MeshPrimitive& primitive : meshAsset.primitives) {
                    Mesh* mesh = !primitive.skinnedVertices.empty() ? GetOrCreateSkinnedPrimitiveMesh(primitive) : GetOrCreatePrimitiveMesh(primitive);
                    drawPrimitive(item, primitive, mesh, !primitive.skinnedVertices.empty());
                }
            }
        }

        if (g.shadowState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(g.shadowMap.Get(), g.shadowState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            cmd->ResourceBarrier(1, &barrier);
            g.shadowState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        RestoreMainRenderTarget();
        g.acceptingFrameSubmissions = false;
    }

    bool IsDirectionalShadowEnabled() {
        return g.frameEnabled &&
            g.shadowMap != nullptr &&
            RENDER3D::IsTextureResourceValid(g.shadowSrvResource);
    }

    const MATH::Mat4& GetDirectionalLightViewProj() {
        return g.lightViewProj;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetDirectionalShadowSrv() {
        return RENDER3D::GetTextureResourceSrvGpuHandle(g.shadowSrvResource);
    }

    uint32_t GetShadowResolution() {
        return g.resolution;
    }

    float GetShadowStrength() {
        return 0.75f;
    }

    float GetDepthBias() {
        return 0.001f;
    }

    float GetNormalBias() {
        return 0.02f;
    }

    const ShadowMapDebugStats& GetDebugStats() {
        return g.debugStats;
    }

} // namespace HIKARI::SHADOW
