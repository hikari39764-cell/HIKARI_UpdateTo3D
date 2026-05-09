#include "HIKARI_ShadowMapRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <d3dcompiler.h>
#include <d3dx12.h>
#include <wrl/client.h>

#include "HIKARI_DxTexture.h"
#include "HIKARI_Services.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/HIKARI_Mesh.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#pragma comment(lib, "d3dcompiler.lib")

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

            std::vector<DrawItem> staticItems;
            std::vector<DrawItem> skinnedItems;
            std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>> primitiveMeshCache;
            std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>> primitiveSkinnedMeshCache;
            std::unordered_map<std::string, int> materialTextureCache;
            ShadowMapDebugStats debugStats;
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

        int ResolvePrimitiveTextureHandle(const ModelAsset& asset, const MaterialAsset* materialAsset) {
            if (materialAsset == nullptr ||
                materialAsset->baseColorTexture.textureIndex < 0 ||
                materialAsset->baseColorTexture.textureIndex >= static_cast<int>(asset.textures.size())) {
                return g.fallbackTextureHandle;
            }

            const TextureAsset3D& texture = asset.textures[static_cast<size_t>(materialAsset->baseColorTexture.textureIndex)];
            if (texture.sourcePath.empty()) {
                return g.fallbackTextureHandle;
            }

            const std::string cacheKey = "shadow:base:" + texture.sourcePath;
            auto found = g.materialTextureCache.find(cacheKey);
            if (found != g.materialTextureCache.end()) {
                return found->second;
            }

            const int handle = DXTEX::DxTextureManager::LoadTexture(cacheKey, texture.sourcePath);
            g.materialTextureCache.emplace(cacheKey, handle);
            return handle >= 0 ? handle : g.fallbackTextureHandle;
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

            g.shadowMap.Reset();
            g.dsvHeap.Reset();
            g.shadowSrvHandle = -1;

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
            if (FAILED(device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &texDesc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                &clearValue,
                IID_PPV_ARGS(g.shadowMap.GetAddressOf())))) {
                return false;
            }

            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            g.dsv = g.dsvHeap->GetCPUDescriptorHandleForHeapStart();
            device->CreateDepthStencilView(g.shadowMap.Get(), &dsvDesc, g.dsv);

            g.shadowSrvHandle = DXTEX::DxTextureManager::RegisterFromResourceAs(g.shadowMap.Get(), DXGI_FORMAT_R32_FLOAT);
            g.resolution = resolution;
            g.shadowState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            return g.shadowSrvHandle >= 0;
        }

        bool CreatePipeline(ID3D12Device* device) {
            UINT flags = 0;
#if defined(_DEBUG)
            flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

            ComPtr<ID3DBlob> staticVs;
            ComPtr<ID3DBlob> skinnedVs;
            ComPtr<ID3DBlob> ps;
            ComPtr<ID3DBlob> err;
            if (FAILED(D3DCompileFromFile(L"HIKARI/Shaders/Render3D_ShadowStaticVS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", flags, 0, staticVs.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }
            err.Reset();
            if (FAILED(D3DCompileFromFile(L"HIKARI/Shaders/Render3D_ShadowSkinnedVS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", flags, 0, skinnedVs.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }
            err.Reset();
            if (FAILED(D3DCompileFromFile(L"HIKARI/Shaders/Render3D_ShadowAlphaPS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", flags, 0, ps.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }

            D3D12_DESCRIPTOR_RANGE textureRange{};
            textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            textureRange.NumDescriptors = 1;
            textureRange.BaseShaderRegister = 0;
            textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER params[3]{};
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
            if (FAILED(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(g.rootSig.GetAddressOf())))) {
                return false;
            }

            D3D12_ROOT_PARAMETER skinnedParams[4]{};
            for (size_t i = 0; i < std::size(params); ++i) {
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
            if (FAILED(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(g.skinnedRootSig.GetAddressOf())))) {
                return false;
            }

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
            if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(g.staticPso.GetAddressOf())))) {
                return false;
            }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedPsoDesc = psoDesc;
            skinnedPsoDesc.pRootSignature = g.skinnedRootSig.Get();
            skinnedPsoDesc.VS = { skinnedVs->GetBufferPointer(), skinnedVs->GetBufferSize() };
            skinnedPsoDesc.InputLayout = { skinnedInput, static_cast<UINT>(std::size(skinnedInput)) };
            return SUCCEEDED(device->CreateGraphicsPipelineState(&skinnedPsoDesc, IID_PPV_ARGS(g.skinnedPso.GetAddressOf())));
        }

        bool EnsureInitialized() {
            if (g.initialized) {
                return true;
            }
            auto* device = SERVICES::gCtx.device;
            if (device == nullptr) {
                return false;
            }
            g.fallbackTextureHandle = DXTEX::DxTextureManager::LoadTexture("shadow/fallback_white", "HIKARI/white1x1.png");
            g.initialized = CreateBuffers(device) && CreatePipeline(device);
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
            const float lightDistance = std::max(1.0f, environment.directionalShadow.farPlane * 0.5f);
            const MATH::Vec3 lightPos = center - lightDir * lightDistance;
            MATH::Vec3 up{ 0.0f, 1.0f, 0.0f };
            if (std::abs(MATH::Dot(lightDir, up)) > 0.95f) {
                up = { 1.0f, 0.0f, 0.0f };
            }
            const MATH::Mat4 view = MATH::Mat4::LookAtRH(lightPos, center, up);
            const float orthoSize = std::max(1.0f, environment.directionalShadow.orthoSize);
            const float nearPlane = std::max(0.001f, environment.directionalShadow.nearPlane);
            const float farPlane = std::max(nearPlane + 0.01f, environment.directionalShadow.farPlane);
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
            const float lightDistance = std::max(1.0f, environment.directionalShadow.farPlane * 0.5f);
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
    }

    void Reset() {
        g.staticItems.clear();
        g.skinnedItems.clear();
        g.debugStats = {};
    }

    void BeginFrame(const SceneEnvironment& environment, const Camera3D& camera) {
        Reset();
        g.frameEnabled = environment.directional.enabled && environment.directionalShadow.enabled;
        g.debugStats.enabled = g.frameEnabled;
        g.debugStats.resolution = ResolveShadowResolution(environment.directionalShadow.resolution);
        if (!g.frameEnabled) {
            return;
        }
        if (!EnsureInitialized()) {
            g.frameEnabled = false;
            g.debugStats.enabled = false;
            return;
        }

        const uint32_t resolution = ResolveShadowResolution(environment.directionalShadow.resolution);
        if (g.shadowMap == nullptr || g.resolution != resolution) {
            if (!CreateShadowMap(resolution)) {
                g.frameEnabled = false;
                g.debugStats.enabled = false;
                return;
            }
        }
        g.lightViewProj = BuildLightViewProj(environment, camera);
        SubmitDebugFrustum(environment, camera);
        if (g.cameraMapped != nullptr) {
            g.cameraMapped->lightViewProj = g.lightViewProj;
        }
    }

    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform, bool castShadow) {
        if (!g.frameEnabled) {
            return;
        }
        if (!castShadow) {
            ++g.debugStats.skippedNoCastShadowCount;
            return;
        }
        DrawItem item{};
        item.asset = &asset;
        item.transform = transform;
        g.staticItems.push_back(std::move(item));
        ++g.debugStats.submittedCasterCount;
    }

    void SubmitSkinnedMesh(const ModelAsset& asset, const Transform3D& transform, const std::vector<MATH::Mat4>& jointPalette, bool castShadow) {
        if (!g.frameEnabled) {
            return;
        }
        if (!castShadow) {
            ++g.debugStats.skippedNoCastShadowCount;
            return;
        }
        DrawItem item{};
        item.asset = &asset;
        item.transform = transform;
        item.jointPalette = jointPalette;
        g.skinnedItems.push_back(std::move(item));
        ++g.debugStats.submittedCasterCount;
    }

    void RenderDirectionalShadowMap() {
        if (!g.frameEnabled || g.shadowMap == nullptr) {
            return;
        }
        auto* cmd = SERVICES::gCtx.cmdList;
        if (cmd == nullptr) {
            return;
        }

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

        ID3D12DescriptorHeap* srvHeap = DXTEX::DxTextureManager::GetSrvHeap();
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
            const int textureHandle = ResolvePrimitiveTextureHandle(*item.asset, materialAsset);
            const D3D12_GPU_DESCRIPTOR_HANDLE textureSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(textureHandle);
            if (textureSrv.ptr != 0) {
                cmd->SetGraphicsRootDescriptorTable(2, textureSrv);
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
            ++objectIndex;
        };

        for (const DrawItem& item : g.staticItems) {
            if (item.asset == nullptr) {
                continue;
            }
            if (!item.asset->meshes.empty()) {
                for (const MeshAsset& meshAsset : item.asset->meshes) {
                    for (const MeshPrimitive& primitive : meshAsset.primitives) {
                        drawPrimitive(item, primitive, GetOrCreatePrimitiveMesh(primitive), false);
                    }
                }
            } else if (const Mesh* legacyMesh = item.asset->GetMesh()) {
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
                const D3D12_GPU_DESCRIPTOR_HANDLE textureSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(g.fallbackTextureHandle);
                if (textureSrv.ptr != 0) {
                    cmd->SetGraphicsRootDescriptorTable(2, textureSrv);
                }

                D3D12_VERTEX_BUFFER_VIEW vb = legacyMesh->GetVBView();
                D3D12_INDEX_BUFFER_VIEW ib = legacyMesh->GetIBView();
                cmd->IASetVertexBuffers(0, 1, &vb);
                cmd->IASetIndexBuffer(&ib);
                cmd->DrawIndexedInstanced(legacyMesh->GetIndexCount(), 1, 0, 0, 0);
                ++g.debugStats.staticCasterDrawCount;
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
    }

    bool IsDirectionalShadowEnabled() {
        return g.frameEnabled && g.shadowMap != nullptr && g.shadowSrvHandle >= 0;
    }

    const MATH::Mat4& GetDirectionalLightViewProj() {
        return g.lightViewProj;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetDirectionalShadowSrv() {
        return DXTEX::DxTextureManager::GetSrvGpuHandle(g.shadowSrvHandle);
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
