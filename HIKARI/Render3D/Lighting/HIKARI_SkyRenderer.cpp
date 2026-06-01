#include "HIKARI_SkyRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <sstream>
#include <wrl/client.h>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "HIKARI_DxTexture.h"
#include "HIKARI_Services.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace HIKARI::SKYRENDERER {

    using Microsoft::WRL::ComPtr;

    namespace {
        struct SkyVertex {
            MATH::Vec3 position{};
        };

        struct SkyCB {
            MATH::Mat4 worldViewProj{};
            MATH::Vec4 zenithExposure{};
            MATH::Vec4 horizonPower{};
            MATH::Vec4 groundYaw{};
            MATH::Vec4 tintMode{};
            MATH::Vec4 sunDirectionIntensity{};
            MATH::Vec4 sunSizeParams{};
        };

        struct State {
            bool initialized = false;
            ComPtr<ID3D12RootSignature> rootSig;
            ComPtr<ID3D12PipelineState> pso;
            ComPtr<ID3DBlob> vsBlob;
            ComPtr<ID3DBlob> psBlob;
            ComPtr<ID3D12Resource> cb;
            ComPtr<ID3D12Resource> vertexBuffer;
            ComPtr<ID3D12Resource> indexBuffer;
            D3D12_VERTEX_BUFFER_VIEW vbView{};
            D3D12_INDEX_BUFFER_VIEW ibView{};
            UINT indexCount = 0;
            SkyCB* mapped = nullptr;
            std::string loadedTexturePath{};
            std::string loadedCubemapPath{};
            int textureHandle = -1;
            int cubemapHandle = -1;
            int fallbackTextureHandle = -1;
            SkyRendererDebugState debug{};
            SkyEnvironmentData environmentData{};
        };

        State g;

        constexpr UINT AlignConstantBufferSize(size_t size) {
            return static_cast<UINT>((size + 255u) & ~255u);
        }

        bool CreateBuffers(ID3D12Device* device) {
            const UINT cbBytes = AlignConstantBufferSize(sizeof(SkyCB));
            auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(cbBytes);
            if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.cb.GetAddressOf())))) {
                HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Create constant buffer failed.");
                return false;
            }
            if (FAILED(g.cb->Map(0, nullptr, reinterpret_cast<void**>(&g.mapped)))) {
                HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Map constant buffer failed.");
                return false;
            }

            const std::array<SkyVertex, 8> vertices = {
                SkyVertex{{ -1.0f, -1.0f, -1.0f }},
                SkyVertex{{ -1.0f,  1.0f, -1.0f }},
                SkyVertex{{  1.0f,  1.0f, -1.0f }},
                SkyVertex{{  1.0f, -1.0f, -1.0f }},
                SkyVertex{{ -1.0f, -1.0f,  1.0f }},
                SkyVertex{{ -1.0f,  1.0f,  1.0f }},
                SkyVertex{{  1.0f,  1.0f,  1.0f }},
                SkyVertex{{  1.0f, -1.0f,  1.0f }},
            };
            const std::array<uint32_t, 36> indices = {
                0, 1, 2, 0, 2, 3,
                7, 6, 5, 7, 5, 4,
                4, 5, 1, 4, 1, 0,
                3, 2, 6, 3, 6, 7,
                1, 5, 6, 1, 6, 2,
                4, 0, 3, 4, 3, 7,
            };
            g.indexCount = static_cast<UINT>(indices.size());

            auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices));
            if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.vertexBuffer.GetAddressOf())))) {
                HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Create vertex buffer failed.");
                return false;
            }
            void* vbData = nullptr;
            if (FAILED(g.vertexBuffer->Map(0, nullptr, &vbData))) {
                return false;
            }
            std::memcpy(vbData, vertices.data(), sizeof(vertices));
            g.vertexBuffer->Unmap(0, nullptr);
            g.vbView.BufferLocation = g.vertexBuffer->GetGPUVirtualAddress();
            g.vbView.SizeInBytes = static_cast<UINT>(sizeof(vertices));
            g.vbView.StrideInBytes = static_cast<UINT>(sizeof(SkyVertex));

            auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(indices));
            if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.indexBuffer.GetAddressOf())))) {
                HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Create index buffer failed.");
                return false;
            }
            void* ibData = nullptr;
            if (FAILED(g.indexBuffer->Map(0, nullptr, &ibData))) {
                return false;
            }
            std::memcpy(ibData, indices.data(), sizeof(indices));
            g.indexBuffer->Unmap(0, nullptr);
            g.ibView.BufferLocation = g.indexBuffer->GetGPUVirtualAddress();
            g.ibView.SizeInBytes = static_cast<UINT>(sizeof(indices));
            g.ibView.Format = DXGI_FORMAT_R32_UINT;
            return true;
        }

        bool CreatePipeline(ID3D12Device* device) {
            UINT flags = 0;
#if defined(_DEBUG)
            flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
            ComPtr<ID3DBlob> err;
            if (FAILED(D3DCompileFromFile(L"HIKARI/Shaders/Render3D_SkyVS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", flags, 0, g.vsBlob.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Compile Render3D_SkyVS.hlsl failed.");
                return false;
            }
            err.Reset();
            if (FAILED(D3DCompileFromFile(L"HIKARI/Shaders/Render3D_SkyPS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", flags, 0, g.psBlob.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Compile Render3D_SkyPS.hlsl failed.");
                return false;
            }

            D3D12_DESCRIPTOR_RANGE texture2DRange{};
            texture2DRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            texture2DRange.NumDescriptors = 1;
            texture2DRange.BaseShaderRegister = 0;
            texture2DRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_DESCRIPTOR_RANGE cubeRange{};
            cubeRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            cubeRange.NumDescriptors = 1;
            cubeRange.BaseShaderRegister = 1;
            cubeRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER params[3]{};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[0].Descriptor.ShaderRegister = 0;
            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[1].DescriptorTable.NumDescriptorRanges = 1;
            params[1].DescriptorTable.pDescriptorRanges = &texture2DRange;
            params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[2].DescriptorTable.NumDescriptorRanges = 1;
            params[2].DescriptorTable.pDescriptorRanges = &cubeRange;

            D3D12_STATIC_SAMPLER_DESC linearClampSampler{};
            linearClampSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            linearClampSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            linearClampSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            linearClampSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            linearClampSampler.ShaderRegister = 0;
            linearClampSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            linearClampSampler.MaxLOD = D3D12_FLOAT32_MAX;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = static_cast<UINT>(std::size(params));
            rsDesc.pParameters = params;
            rsDesc.NumStaticSamplers = 1;
            rsDesc.pStaticSamplers = &linearClampSampler;
            rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ComPtr<ID3DBlob> sigBlob;
            ComPtr<ID3DBlob> errBlob;
            if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, sigBlob.GetAddressOf(), errBlob.GetAddressOf()))) {
                if (errBlob) OutputDebugStringA(static_cast<const char*>(errBlob->GetBufferPointer()));
                HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Serialize root signature failed.");
                return false;
            }
            if (FAILED(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(g.rootSig.GetAddressOf())))) {
                HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Create root signature failed.");
                return false;
            }

            const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(SkyVertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = g.rootSig.Get();
            psoDesc.VS = { g.vsBlob->GetBufferPointer(), g.vsBlob->GetBufferSize() };
            psoDesc.PS = { g.psBlob->GetBufferPointer(), g.psBlob->GetBufferSize() };
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            psoDesc.InputLayout = { inputElements, static_cast<UINT>(std::size(inputElements)) };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = 1;

            if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(g.pso.GetAddressOf())))) {
                HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Create PSO failed.");
                return false;
            }
            ++g.debug.psoCreateCount;
            return true;
        }

        bool EnsureInitialized() {
            if (g.initialized) {
                return true;
            }
            auto* device = SERVICES::gCtx.device;
            if (!device) {
                return false;
            }
            if (!CreateBuffers(device) || !CreatePipeline(device)) {
                return false;
            }
            g.fallbackTextureHandle = DXTEX::DxTextureManager::LoadTexture("sky_renderer/fallback_white", "HIKARI/white1x1.png");
            g.initialized = true;
            g.debug.initialized = true;
            return true;
        }

        int ResolveTexture2D(const SkySettings& settings, SkyManager& skyManager) {
            const SkyAsset* skyAsset = skyManager.FindAsset(settings.skyAsset);
            g.debug.skyAssetFound = (skyAsset != nullptr);
            if (!skyAsset) {
                g.debug.activeTexturePath.clear();
                g.debug.textureValid = false;
                g.debug.usingFallback = true;
                if (!settings.skyAsset.empty()) {
                    HIKARI_LOG_WARN("[SkyRenderer] sky asset not registered: " + settings.skyAsset);
                }
                return -1;
            }

            g.debug.activeTexturePath = skyAsset->texturePath;
            if (g.loadedTexturePath != skyAsset->texturePath) {
                g.loadedTexturePath = skyAsset->texturePath;
                g.textureHandle = -1;
                if (!skyAsset->texturePath.empty()) {
                    g.textureHandle = DXTEX::DxTextureManager::LoadTexture("sky_renderer/scene_sky", skyAsset->texturePath);
                }
            }
            return g.textureHandle;
        }

        int ResolveCubemap(const SkySettings& settings, SkyManager& skyManager) {
            const SkyAsset* skyAsset = skyManager.FindAsset(settings.skyAsset);
            g.debug.skyAssetFound = (skyAsset != nullptr);
            if (!skyAsset) {
                g.debug.activeTexturePath.clear();
                g.debug.cubemapLoaded = false;
                g.debug.textureValid = false;
                g.debug.usingFallback = true;
                if (!settings.skyAsset.empty()) {
                    HIKARI_LOG_WARN("[SkyRenderer] sky asset not registered: " + settings.skyAsset);
                }
                return -1;
            }

            const std::string path = skyAsset->texturePath;
            g.debug.activeTexturePath = path;
            if (path.empty()) {
                return -1;
            }

            if (g.loadedCubemapPath != path) {
                g.loadedCubemapPath = path;
                g.cubemapHandle = DXTEX::DxTextureManager::LoadCubemap("sky_renderer/scene_sky_cube", path, DXTEX::TextureColorSpace::Srgb);
                if (g.cubemapHandle < 0) {
                    std::ostringstream oss;
                    oss << "[SkyRenderer][WARN] Cubemap load failed. Falling back to Gradient sky. skyAsset="
                        << settings.skyAsset << " path=" << path;
                    HIKARI_LOG_WARN(oss.str());
                }
            }
            return g.cubemapHandle;
        }
    }

    void Reset() {
        g.environmentData = {};
        g.debug.lastRenderSubmitted = false;
        g.debug.skyAssetFound = false;
        g.debug.cubemapLoaded = false;
        g.debug.usingFallback = false;
        g.debug.textureValid = false;
        g.debug.drawCount = 0;
        g.debug.mode = SkyMode::None;
        g.debug.cubemapHandle = -1;
        g.debug.textureHandle = -1;
    }
	// キャッシュされたテクスチャを解放し、次回の描画で再ロードするようにする
    void InvalidateSkyTextureCache()
    {
		// 既にロードされているテクスチャがあれば解放する
        if (g.textureHandle >= 0) {
            DXTEX::DxTextureManager::ReleaseTextureDeferred(g.textureHandle);
        }
		// キューブマップがテクスチャと同じハンドルでない場合のみ解放する
        if (g.cubemapHandle >= 0 && g.cubemapHandle != g.textureHandle) {
            DXTEX::DxTextureManager::ReleaseTextureDeferred(g.cubemapHandle);
        }

        g.loadedTexturePath.clear();
        g.loadedCubemapPath.clear();

        g.textureHandle = -1;
        g.cubemapHandle = -1;

        g.debug.textureHandle = -1;
        g.debug.cubemapHandle = -1;
        g.debug.activeTexturePath.clear();

        DXTEX::DxTextureManager::InvalidateTextureCacheByName("sky_renderer/scene_sky");
        DXTEX::DxTextureManager::InvalidateTextureCacheByName("sky_renderer/scene_sky_cube");
    }

    void Render(const Camera3D& camera, const SceneEnvironment& environment, ModelManager&, SkyManager& skyManager) {
        const SkySettings& settings = environment.sky;
        const DirectionalLight& sun = environment.directional;
        g.debug.initialized = g.initialized;
        g.debug.activeSkyAsset = settings.skyAsset;
        g.debug.activeTexturePath.clear();

        if (!settings.enabled || settings.mode == SkyMode::None || !EnsureInitialized()) {
            g.debug.initialized = g.initialized;
            return;
        }

        int texture2DHandle = g.fallbackTextureHandle;
        int cubemapHandle = -1;
        SkyMode renderMode = settings.mode;

        if (settings.mode == SkyMode::Texture2D) {
            const int resolved = ResolveTexture2D(settings, skyManager);
            if (resolved >= 0) {
                texture2DHandle = resolved;
            } else {
                g.debug.usingFallback = true;
                renderMode = SkyMode::Gradient;
            }
        } else if (settings.mode == SkyMode::Cubemap) {
            cubemapHandle = ResolveCubemap(settings, skyManager);
            if (cubemapHandle < 0) {
                g.debug.usingFallback = true;
                renderMode = SkyMode::Gradient;
            }
        }

        auto* cmd = SERVICES::gCtx.cmdList;
        if (!cmd) {
            return;
        }
        // PIX 上で sky pass の境界を追いやすくする。
        GFX::PIX::ScopedGpuEvent pixSky(cmd, GFX::PIX::kColorRender, "SkyRenderer.Render");

        const MATH::Vec3 cameraPos = settings.followCamera ? camera.GetPosition() : MATH::Vec3{};
        const MATH::Quat yawRot = MATH::Quat::FromEulerXYZ(0.0f, settings.yaw, 0.0f);
        const float s = std::max(0.0001f, settings.scale);
        const MATH::Mat4 world = MATH::Mat4::TRS(cameraPos, yawRot, { s, s, s });
        MATH::Vec3 sunDir = sun.direction;
        if (MATH::Length(sunDir) < 1e-5f) {
            sunDir = { 0.4f, -1.0f, -0.6f };
        }
        sunDir = MATH::Normalize(sunDir);
        const float sunIntensity = sun.enabled ? std::max(0.0f, sun.intensity) : 0.0f;

        g.mapped->worldViewProj = camera.GetViewProj() * world;
        g.mapped->zenithExposure = { settings.zenithColor.x, settings.zenithColor.y, settings.zenithColor.z, std::max(0.0f, settings.exposure) };
        g.mapped->horizonPower = { settings.horizonColor.x, settings.horizonColor.y, settings.horizonColor.z, std::max(0.01f, settings.horizonPower) };
        g.mapped->groundYaw = { settings.groundColor.x, settings.groundColor.y, settings.groundColor.z, settings.yaw };
        g.mapped->tintMode = { settings.tint.x, settings.tint.y, settings.tint.z, static_cast<float>(renderMode) };
        g.mapped->sunDirectionIntensity = { sunDir.x, sunDir.y, sunDir.z, settings.showSunDisk ? settings.sunDiskIntensity * sunIntensity : 0.0f };
        g.mapped->sunSizeParams = { std::max(0.0001f, settings.sunDiskSize), settings.ambientFromSky, settings.reflectionIntensity, 0.0f };

        cmd->SetGraphicsRootSignature(g.rootSig.Get());
        cmd->SetPipelineState(g.pso.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->SetGraphicsRootConstantBufferView(0, g.cb->GetGPUVirtualAddress());

        ID3D12DescriptorHeap* srvHeap = DXTEX::DxTextureManager::GetSrvHeap();
        if (srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            cmd->SetDescriptorHeaps(1, heaps);
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE textureSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(texture2DHandle);
        if (textureSrv.ptr != 0) {
            cmd->SetGraphicsRootDescriptorTable(1, textureSrv);
        }
        const D3D12_GPU_DESCRIPTOR_HANDLE cubeSrv = DXTEX::DxTextureManager::GetSrvGpuHandle(cubemapHandle);
        if (cubeSrv.ptr != 0) {
            cmd->SetGraphicsRootDescriptorTable(2, cubeSrv);
        } else if (textureSrv.ptr != 0) {
            // Keep every root descriptor table initialized for Gradient/Texture2D fallback paths.
            cmd->SetGraphicsRootDescriptorTable(2, textureSrv);
        }

        g.debug.textureValid = (textureSrv.ptr != 0) || (cubeSrv.ptr != 0) || renderMode == SkyMode::Gradient;
        g.debug.cubemapLoaded = (cubemapHandle >= 0 && cubeSrv.ptr != 0);
        g.debug.cubemapHandle = cubemapHandle;
        g.debug.textureHandle = texture2DHandle;
        g.debug.mode = renderMode;



        g.environmentData.valid = true;
        g.environmentData.hasCubemap = g.debug.cubemapLoaded;
        g.environmentData.usingFallback = g.debug.usingFallback;
        g.environmentData.mode = renderMode;
        g.environmentData.cubemapHandle = cubemapHandle;
        g.environmentData.textureHandle = texture2DHandle;
        g.environmentData.cubemapSrv = cubeSrv;
        g.environmentData.zenithColor = {
            settings.zenithColor.x * settings.tint.x,
            settings.zenithColor.y * settings.tint.y,
            settings.zenithColor.z * settings.tint.z
        };

        g.environmentData.horizonColor = {
            settings.horizonColor.x * settings.tint.x,
            settings.horizonColor.y * settings.tint.y,
            settings.horizonColor.z * settings.tint.z
        };

        g.environmentData.groundColor = {
            settings.groundColor.x * settings.tint.x,
            settings.groundColor.y * settings.tint.y,
            settings.groundColor.z * settings.tint.z
        };
        g.environmentData.exposure = std::max(0.0f, settings.exposure);
        g.environmentData.ambientFromSky = std::max(0.0f, settings.ambientFromSky);
        g.environmentData.reflectionIntensity = std::max(0.0f, settings.reflectionIntensity);
        g.environmentData.horizonPower = std::max(0.01f, settings.horizonPower);
        g.environmentData.yaw = settings.yaw;
        g.environmentData.activeSkyAsset = settings.skyAsset;
        g.environmentData.activeTexturePath = g.debug.activeTexturePath;

        cmd->IASetVertexBuffers(0, 1, &g.vbView);
        cmd->IASetIndexBuffer(&g.ibView);
        cmd->DrawIndexedInstanced(g.indexCount, 1, 0, 0, 0);
        g.debug.lastRenderSubmitted = true;
        ++g.debug.drawCount;
    }

    const SkyRendererDebugState& GetDebugState() {
        return g.debug;
    }

    const SkyEnvironmentData& GetEnvironmentData() {
        return g.environmentData;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetActiveCubemapSrv() {
        return g.environmentData.cubemapSrv;
    }

    int GetActiveCubemapHandle() {
        return g.environmentData.cubemapHandle;
    }

    bool HasActiveCubemap() {
        return g.environmentData.valid && g.environmentData.hasCubemap;
    }

} // namespace HIKARI::SKYRENDERER
