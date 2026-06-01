#include "Render3D/ScreenSpace/HIKARI_SsaoRenderer.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <d3dcompiler.h>
#include <d3dx12.h>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "HIKARI_Services.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace HIKARI::RENDER3D::SCREENSPACE {

    namespace {
        struct SsaoPassCB {
            MATH::Mat4 viewProj{};
            MATH::Mat4 invViewProj{};
            MATH::Vec4 screenParams{};
            MATH::Vec4 aoParams0{};
            MATH::Vec4 aoParams1{};
            MATH::Vec4 blurParams{};
        };

        SsaoDebugState gDebugState{};

        uint32_t NormalizeSampleCount(uint32_t value) {
            if (value <= 8u) {
                return 8u;
            }
            if (value <= 16u) {
                return 16u;
            }
            if (value <= 24u) {
                return 24u;
            }
            return 32u;
        }

        bool CompileShader(const wchar_t* path, const char* entry, const char* target, ID3DBlob** outBlob) {
            UINT flags = 0;
#if defined(_DEBUG)
            flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
            Microsoft::WRL::ComPtr<ID3DBlob> err;
            HRESULT hr = D3DCompileFromFile(path, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target, flags, 0, outBlob, err.GetAddressOf());
            if (FAILED(hr)) {
                if (err) {
                    OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                    DEBUGLOG::PushRenderError(static_cast<const char*>(err->GetBufferPointer()));
                }
                return false;
            }
            return true;
        }
    }

    const SsaoDebugState& GetSsaoDebugState() {
        return gDebugState;
    }

    bool SsaoRenderer::Render(
        ID3D12GraphicsCommandList* cmd,
        const SceneGeometryBuffer& geometryBuffer,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv,
        const MESHRENDERER::CameraCB& camera,
        const AmbientOcclusionSettings& settings) {

        gDebugState.enabled = settings.enabled;
        gDebugState.valid = false;
        gDebugState.width = geometryBuffer.GetWidth();
        gDebugState.height = geometryBuffer.GetHeight();
        gDebugState.sampleCount = NormalizeSampleCount(settings.sampleCount);
        gDebugState.blurIterations = std::clamp<uint32_t>(settings.blurIterations, 0u, 4u);
        gDebugState.radius = settings.radius;
        gDebugState.strength = settings.strength;
        gDebugState.power = settings.power;

        valid_ = false;
        lastAoSrv_ = {};

        if (!settings.enabled || cmd == nullptr || !geometryBuffer.IsValid() || sceneDepthSrv.ptr == 0) {
            return false;
        }
        if (!EnsurePipeline() || !EnsureResources(geometryBuffer.GetWidth(), geometryBuffer.GetHeight())) {
            return false;
        }

        GFX::PIX::ScopedGpuEvent pixSsao(cmd, GFX::PIX::kColorPost, "SSAO.GenerateAndBlur");

        SsaoPassCB cb{};
        cb.viewProj = camera.viewProj;
        cb.invViewProj = camera.invViewProj;
        cb.screenParams = camera.screenParams;
        cb.aoParams0 = {
            std::max(0.01f, settings.radius),
            std::max(0.0f, settings.bias),
            std::max(0.0f, settings.strength),
            std::max(0.1f, settings.power)
        };
        cb.aoParams1 = {
            static_cast<float>(gDebugState.sampleCount),
            camera.timeParams.w,
            0.0f,
            0.0f
        };
        std::memcpy(constantMapped_, &cb, sizeof(cb));

        ID3D12DescriptorHeap* heaps[] = { SERVICES::gCtx.srvHeap };
        cmd->SetDescriptorHeaps(1, heaps);

        Transition(cmd, rawAo_.Get(), rawState_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd->OMSetRenderTargets(1, &rawRtv_, FALSE, nullptr);
        const float white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        cmd->ClearRenderTargetView(rawRtv_, white, 0, nullptr);

        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width_);
        viewport.Height = static_cast<float>(height_);
        viewport.MaxDepth = 1.0f;
        D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
        cmd->RSSetViewports(1, &viewport);
        cmd->RSSetScissorRects(1, &scissor);

        cmd->SetGraphicsRootSignature(generateRootSig_.Get());
        cmd->SetGraphicsRootConstantBufferView(0, constantBuffer_->GetGPUVirtualAddress());
        cmd->SetGraphicsRootDescriptorTable(1, sceneDepthSrv);
        cmd->SetGraphicsRootDescriptorTable(2, geometryBuffer.GetNormalRoughnessSrv());
        cmd->SetPipelineState(generatePso_.Get());
        DrawFullscreen(cmd);
        Transition(cmd, rawAo_.Get(), rawState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        D3D12_GPU_DESCRIPTOR_HANDLE sourceSrv = rawSrvGpu_;
        D3D12_CPU_DESCRIPTOR_HANDLE targetRtv = blurredRtv_;
        D3D12_RESOURCE_STATES* targetState = &blurredState_;
        ID3D12Resource* targetResource = blurredAo_.Get();

        const uint32_t blurIterations = gDebugState.blurIterations;
        for (uint32_t i = 0; i < blurIterations; ++i) {
            const bool horizontal = (i % 2u) == 0u;
            cb.blurParams = {
                horizontal ? 1.0f : 0.0f,
                horizontal ? 0.0f : 1.0f,
                0.0f,
                0.0f
            };
            std::memcpy(constantMapped_, &cb, sizeof(cb));

            Transition(cmd, targetResource, *targetState, D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmd->OMSetRenderTargets(1, &targetRtv, FALSE, nullptr);
            cmd->ClearRenderTargetView(targetRtv, white, 0, nullptr);

            cmd->SetGraphicsRootSignature(blurRootSig_.Get());
            cmd->SetGraphicsRootConstantBufferView(0, constantBuffer_->GetGPUVirtualAddress());
            cmd->SetGraphicsRootDescriptorTable(1, sourceSrv);
            cmd->SetGraphicsRootDescriptorTable(2, sceneDepthSrv);
            cmd->SetGraphicsRootDescriptorTable(3, geometryBuffer.GetNormalRoughnessSrv());
            cmd->SetPipelineState(blurPso_.Get());
            DrawFullscreen(cmd);

            Transition(cmd, targetResource, *targetState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

            if (targetResource == blurredAo_.Get()) {
                sourceSrv = blurredSrvGpu_;
                targetRtv = rawRtv_;
                targetState = &rawState_;
                targetResource = rawAo_.Get();
            }
            else {
                sourceSrv = rawSrvGpu_;
                targetRtv = blurredRtv_;
                targetState = &blurredState_;
                targetResource = blurredAo_.Get();
            }
        }

        lastAoSrv_ = blurIterations == 0u ? rawSrvGpu_ : sourceSrv;
        valid_ = lastAoSrv_.ptr != 0;
        gDebugState.valid = valid_;
        return valid_;
    }

    bool SsaoRenderer::EnsureResources(uint32_t width, uint32_t height) {
        width = std::max(1u, width);
        height = std::max(1u, height);
        if (rawAo_ && blurredAo_ && width_ == width && height_ == height && constantBuffer_) {
            return true;
        }

        ID3D12Device* device = SERVICES::gCtx.device;
        if (device == nullptr) {
            return false;
        }

        rawAo_.Reset();
        blurredAo_.Reset();
        rtvHeap_.Reset();
        rawRtv_ = {};
        blurredRtv_ = {};
        rawSrvCpu_ = {};
        blurredSrvCpu_ = {};
        rawSrvGpu_ = {};
        blurredSrvGpu_ = {};
        rawState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        blurredState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = 2;
        HRESULT hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap_.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "SsaoRenderer::Create RTV heap")) {
            return false;
        }

        const UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        rawRtv_ = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        blurredRtv_ = rawRtv_;
        blurredRtv_.ptr += rtvSize;

        width_ = width;
        height_ = height;

        if (!CreateAoResource(width, height, GFX::DESCRIPTOR::SystemSrv::SsaoRaw, rawAo_, rawRtv_, rawSrvCpu_, rawSrvGpu_, L"HIKARI.SSAO.Raw")) {
            return false;
        }
        if (!CreateAoResource(width, height, GFX::DESCRIPTOR::SystemSrv::SsaoBlurred, blurredAo_, blurredRtv_, blurredSrvCpu_, blurredSrvGpu_, L"HIKARI.SSAO.Blurred")) {
            return false;
        }

        if (!constantBuffer_) {
            const auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            const auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(MESHRENDERER::AlignConstantBufferSize(sizeof(SsaoPassCB)));
            hr = device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &cbDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(constantBuffer_.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "SsaoRenderer::Create constant buffer")) {
                return false;
            }
            if (FAILED(constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&constantMapped_)))) {
                return false;
            }
            constantBuffer_->SetName(L"HIKARI.SSAO.CB");
        }

        HIKARI_LOG_INFO("[SSAO] resized " + std::to_string(width_) + "x" + std::to_string(height_));
        return true;
    }

    bool SsaoRenderer::CreateAoResource(
        uint32_t width,
        uint32_t height,
        GFX::DESCRIPTOR::SystemSrv srvSlot,
        Microsoft::WRL::ComPtr<ID3D12Resource>& outResource,
        D3D12_CPU_DESCRIPTOR_HANDLE& outRtv,
        D3D12_CPU_DESCRIPTOR_HANDLE& outSrvCpu,
        D3D12_GPU_DESCRIPTOR_HANDLE& outSrvGpu,
        const wchar_t* debugName) {

        ID3D12Device* device = SERVICES::gCtx.device;
        ID3D12DescriptorHeap* srvHeap = SERVICES::gCtx.srvHeap;
        if (device == nullptr || srvHeap == nullptr) {
            return false;
        }

        const D3D12_CLEAR_VALUE clearValue{ DXGI_FORMAT_R8_UNORM, { 1.0f, 0.0f, 0.0f, 0.0f } };
        const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R8_UNORM,
            width,
            height,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearValue,
            IID_PPV_ARGS(outResource.GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "SsaoRenderer::Create AO texture")) {
            return false;
        }
        outResource->SetName(debugName);
        device->CreateRenderTargetView(outResource.Get(), nullptr, outRtv);

        const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT srvIndex = GFX::DESCRIPTOR::ToIndex(srvSlot);
        outSrvCpu = GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, srvIndex);
        outSrvGpu = GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, srvIndex);

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(outResource.Get(), &srv, outSrvCpu);
        return true;
    }

    bool SsaoRenderer::EnsurePipeline() {
        if (generatePso_ && blurPso_) {
            return true;
        }

        ID3D12Device* device = SERVICES::gCtx.device;
        if (device == nullptr) {
            return false;
        }

        D3D12_STATIC_SAMPLER_DESC pointSampler{};
        pointSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        pointSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        pointSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        pointSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        pointSampler.ShaderRegister = 0;
        pointSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC linearSampler = pointSampler;
        linearSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        linearSampler.ShaderRegister = 1;

        const D3D12_STATIC_SAMPLER_DESC samplers[] = { pointSampler, linearSampler };

        auto createRootSignature = [&](UINT textureCount, ID3D12RootSignature** outRootSig) -> bool {
            std::array<D3D12_DESCRIPTOR_RANGE, 3> ranges{};
            std::array<D3D12_ROOT_PARAMETER, 4> params{};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[0].Descriptor.ShaderRegister = 0;

            for (UINT i = 0; i < textureCount; ++i) {
                ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                ranges[i].NumDescriptors = 1;
                ranges[i].BaseShaderRegister = i;
                ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                params[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                params[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
                params[i + 1].DescriptorTable.NumDescriptorRanges = 1;
                params[i + 1].DescriptorTable.pDescriptorRanges = &ranges[i];
            }

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = textureCount + 1;
            rsDesc.pParameters = params.data();
            rsDesc.NumStaticSamplers = static_cast<UINT>(std::size(samplers));
            rsDesc.pStaticSamplers = samplers;
            rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            Microsoft::WRL::ComPtr<ID3DBlob> sig;
            Microsoft::WRL::ComPtr<ID3DBlob> err;
            HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, sig.GetAddressOf(), err.GetAddressOf());
            if (FAILED(hr)) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }
            return SUCCEEDED(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(outRootSig)));
        };

        if (!createRootSignature(2, generateRootSig_.GetAddressOf()) ||
            !createRootSignature(3, blurRootSig_.GetAddressOf())) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> generateVs;
        Microsoft::WRL::ComPtr<ID3DBlob> generatePs;
        Microsoft::WRL::ComPtr<ID3DBlob> blurVs;
        Microsoft::WRL::ComPtr<ID3DBlob> blurPs;
        if (!CompileShader(L"HIKARI/Shaders/Post_SSAOPS.hlsl", "VSMain", "vs_5_0", generateVs.GetAddressOf()) ||
            !CompileShader(L"HIKARI/Shaders/Post_SSAOPS.hlsl", "PSMain", "ps_5_0", generatePs.GetAddressOf()) ||
            !CompileShader(L"HIKARI/Shaders/Post_SSAOBlurPS.hlsl", "VSMain", "vs_5_0", blurVs.GetAddressOf()) ||
            !CompileShader(L"HIKARI/Shaders/Post_SSAOBlurPS.hlsl", "PSMain", "ps_5_0", blurPs.GetAddressOf())) {
            return false;
        }

        auto makePso = [&](ID3D12RootSignature* rootSig, ID3DBlob* vs, ID3DBlob* ps, ID3D12PipelineState** outPso) -> bool {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = rootSig;
            desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
            desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.InputLayout = { nullptr, 0 };
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 1;
            desc.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
            desc.SampleDesc.Count = 1;
            return SUCCEEDED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(outPso)));
        };

        return makePso(generateRootSig_.Get(), generateVs.Get(), generatePs.Get(), generatePso_.GetAddressOf()) &&
            makePso(blurRootSig_.Get(), blurVs.Get(), blurPs.Get(), blurPso_.GetAddressOf());
    }

    void SsaoRenderer::Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES nextState) {
        if (cmd == nullptr || resource == nullptr || state == nextState) {
            state = nextState;
            return;
        }

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, state, nextState);
        cmd->ResourceBarrier(1, &barrier);
        state = nextState;
    }

    void SsaoRenderer::DrawFullscreen(ID3D12GraphicsCommandList* cmd) {
        if (cmd == nullptr) {
            return;
        }
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->IASetVertexBuffers(0, 0, nullptr);
        cmd->IASetIndexBuffer(nullptr);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

    void SsaoRenderer::Release() {
        if (constantBuffer_ && constantMapped_) {
            constantBuffer_->Unmap(0, nullptr);
            constantMapped_ = nullptr;
        }
        constantBuffer_.Reset();
        rawAo_.Reset();
        blurredAo_.Reset();
        rtvHeap_.Reset();
        generateRootSig_.Reset();
        generatePso_.Reset();
        blurRootSig_.Reset();
        blurPso_.Reset();
        lastAoSrv_ = {};
        valid_ = false;
        width_ = 0;
        height_ = 0;
        rawState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        blurredState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        gDebugState = {};
    }

} // namespace HIKARI::RENDER3D::SCREENSPACE
