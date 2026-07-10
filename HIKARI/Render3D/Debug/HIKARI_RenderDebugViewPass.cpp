#include "Render3D/Debug/HIKARI_RenderDebugViewPass.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "HIKARI_Services.h"

namespace HIKARI::RENDER3D::DEBUGVIEW {

    namespace {
        using Microsoft::WRL::ComPtr;

        constexpr DXGI_FORMAT kSceneHdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

        struct MotionVectorDebugConstants {
            MATH::Vec4 params{};
        };

        struct MotionVectorDebugPassState {
            ComPtr<ID3D12RootSignature> rootSignature{};
            ComPtr<ID3D12PipelineState> pipelineState{};
            ComPtr<ID3D12Resource> constantBuffer{};
            MotionVectorDebugConstants* mappedConstants = nullptr;
            float pixelsToColorScale = 1.0f / 32.0f;
            bool ready = false;
        };

        MotionVectorDebugPassState& State() {
            static MotionVectorDebugPassState state{};
            return state;
        }

        UINT AlignConstantBufferSize(UINT size) {
            return (size + 255u) & ~255u;
        }

        bool EnsureConstantBuffer(ID3D12Device* device) {
            MotionVectorDebugPassState& state = State();
            if (state.constantBuffer != nullptr && state.mappedConstants != nullptr) {
                return true;
            }

            const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
                AlignConstantBufferSize(sizeof(MotionVectorDebugConstants)));
            const HRESULT hr = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(state.constantBuffer.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "RenderDebugViewPass::CreateMotionVectorCB")) {
                return false;
            }
            state.constantBuffer->SetName(L"HIKARI.DebugView.MotionVectorCB");
            const CD3DX12_RANGE readRange(0, 0);
            if (FAILED(state.constantBuffer->Map(
                    0,
                    &readRange,
                    reinterpret_cast<void**>(&state.mappedConstants)))) {
                state.constantBuffer.Reset();
                state.mappedConstants = nullptr;
                return false;
            }
            return true;
        }

        bool EnsureMotionVectorPipeline(ID3D12Device* device) {
            MotionVectorDebugPassState& state = State();
            if (state.ready &&
                state.rootSignature != nullptr &&
                state.pipelineState != nullptr) {
                return true;
            }
            if (device == nullptr) {
                return false;
            }

            D3D12_DESCRIPTOR_RANGE motionRange{};
            motionRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            motionRange.NumDescriptors = 1;
            motionRange.BaseShaderRegister = 0;
            motionRange.RegisterSpace = 0;
            motionRange.OffsetInDescriptorsFromTableStart =
                D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER params[2]{};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[0].Descriptor.ShaderRegister = 0;
            params[0].Descriptor.RegisterSpace = 0;

            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[1].DescriptorTable.NumDescriptorRanges = 1;
            params[1].DescriptorTable.pDescriptorRanges = &motionRange;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = static_cast<UINT>(std::size(params));
            rsDesc.pParameters = params;
            rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            ComPtr<ID3DBlob> sigBlob;
            ComPtr<ID3DBlob> errBlob;
            HRESULT hr = D3D12SerializeRootSignature(
                &rsDesc,
                D3D_ROOT_SIGNATURE_VERSION_1,
                sigBlob.GetAddressOf(),
                errBlob.GetAddressOf());
            if (FAILED(hr)) {
                if (errBlob != nullptr) {
                    DEBUGLOG::PushRenderError(
                        static_cast<const char*>(errBlob->GetBufferPointer()));
                }
                return false;
            }
            hr = device->CreateRootSignature(
                0,
                sigBlob->GetBufferPointer(),
                sigBlob->GetBufferSize(),
                IID_PPV_ARGS(state.rootSignature.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "RenderDebugViewPass::CreateMotionVectorRootSignature")) {
                return false;
            }
            state.rootSignature->SetName(L"HIKARI.DebugView.MotionVectorRootSignature");

            ComPtr<ID3DBlob> vs;
            ComPtr<ID3DBlob> ps;
            if (!GFX::CompileShaderFileSm6(
                    L"HIKARI/Shaders/Debug_MotionVectorView.hlsl",
                    "VSMain",
                    GFX::ShaderStage::Vertex,
                    vs.GetAddressOf()) ||
                !GFX::CompileShaderFileSm6(
                    L"HIKARI/Shaders/Debug_MotionVectorView.hlsl",
                    "PSMain",
                    GFX::ShaderStage::Pixel,
                    ps.GetAddressOf())) {
                return false;
            }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = state.rootSignature.Get();
            psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
            psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = FALSE;
            psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            psoDesc.InputLayout = {};
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = kSceneHdrFormat;
            psoDesc.SampleDesc.Count = 1;

            hr = device->CreateGraphicsPipelineState(
                &psoDesc,
                IID_PPV_ARGS(state.pipelineState.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "RenderDebugViewPass::CreateMotionVectorPSO")) {
                return false;
            }
            state.pipelineState->SetName(L"HIKARI.DebugView.MotionVectorPSO");

            state.ready = EnsureConstantBuffer(device);
            return state.ready;
        }
    }

    void SetMotionVectorDebugScale(float pixelsToColorScale) {
        MotionVectorDebugPassState& state = State();
        state.pixelsToColorScale =
            std::clamp(pixelsToColorScale, 1.0f / 512.0f, 1.0f / 4.0f);
    }

    bool ExecuteMotionVectorDebugView(
        const RENDER3D::TEMPORAL::TemporalTextureView& motionVectors) {

        MotionVectorDebugPassState& state = State();
        ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
        ID3D12Device* device = SERVICES::gCtx.device;
        if (cmd == nullptr ||
            device == nullptr ||
            !motionVectors.valid ||
            motionVectors.srv.ptr == 0) {
            return false;
        }
        if (!EnsureMotionVectorPipeline(device) ||
            state.mappedConstants == nullptr ||
            state.constantBuffer == nullptr) {
            return false;
        }

        MotionVectorDebugConstants constants{};
        constants.params = {
            state.pixelsToColorScale,
            static_cast<float>((std::max)(1u, motionVectors.width)),
            static_cast<float>((std::max)(1u, motionVectors.height)),
            0.0f
        };
        *state.mappedConstants = constants;

        GFX::PIX::ScopedGpuEvent pix(
            cmd,
            GFX::PIX::kColorRender,
            "DebugView.MotionVectors");
        cmd->SetGraphicsRootSignature(state.rootSignature.Get());
        cmd->SetPipelineState(state.pipelineState.Get());
        cmd->SetGraphicsRootConstantBufferView(
            0,
            state.constantBuffer->GetGPUVirtualAddress());
        cmd->SetGraphicsRootDescriptorTable(1, motionVectors.srv);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
        return true;
    }

    void ShutdownRenderDebugViewPasses() {
        MotionVectorDebugPassState& state = State();
        if (state.constantBuffer != nullptr && state.mappedConstants != nullptr) {
            state.constantBuffer->Unmap(0, nullptr);
        }
        state.mappedConstants = nullptr;
        state.constantBuffer.Reset();
        state.pipelineState.Reset();
        state.rootSignature.Reset();
        state.ready = false;
    }

} // namespace HIKARI::RENDER3D::DEBUGVIEW
