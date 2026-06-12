#include "Render3D/Cluster/HIKARI_ClusterDrawExecutor.h"

#include <algorithm>
#include <limits>

#include <d3dcompiler.h>
#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"

namespace HIKARI::RENDER3D::CLUSTER {

    namespace {
        bool CreateClusterPipelineState(
            ID3D12Device* device,
            ID3D12RootSignature* rootSignature,
            ID3DBlob* vertexShader,
            ID3DBlob* pixelShader,
            D3D12_CULL_MODE cullMode,
            const wchar_t* debugName,
            ID3D12PipelineState** outPipelineState) {

            if (device == nullptr ||
                rootSignature == nullptr ||
                vertexShader == nullptr ||
                pixelShader == nullptr ||
                outPipelineState == nullptr) {
                return false;
            }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = rootSignature;
            psoDesc.VS = {
                vertexShader->GetBufferPointer(),
                vertexShader->GetBufferSize()
            };
            psoDesc.PS = {
                pixelShader->GetBufferPointer(),
                pixelShader->GetBufferSize()
            };
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.CullMode = cullMode;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = TRUE;
            psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            psoDesc.InputLayout = { nullptr, 0 };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = 1;

            const HRESULT hr = device->CreateGraphicsPipelineState(
                &psoDesc,
                IID_PPV_ARGS(outPipelineState));
            if (!HIKARI_DX_CHECK(hr, "ClusterDraw::CreateGraphicsPipelineState")) {
                return false;
            }
            GFX::SetD3D12Name(*outPipelineState, debugName);
            return true;
        }

        bool ArePipelinesReady(const ClusterDrawExecutor::PipelineBucketArray& pipelines) {
            for (const auto& pipeline : pipelines) {
                if (pipeline == nullptr) {
                    return false;
                }
            }
            return true;
        }

        D3D12_CULL_MODE CullModeForBucket(ClusterDrawCullModeBucket bucket) {
            return bucket == ClusterDrawCullModeBucket::DoubleSided
                ? D3D12_CULL_MODE_NONE
                : D3D12_CULL_MODE_BACK;
        }

        bool HasSourceForBucket(
            const ClusterGpuCullingPassStats& stats,
            ClusterDrawCullModeBucket bucket) {

            const size_t knownBucketSourceCount =
                stats.sourceSingleSidedInstanceCount +
                stats.sourceDoubleSidedInstanceCount;
            if (knownBucketSourceCount == 0) {
                return true;
            }
            return bucket == ClusterDrawCullModeBucket::DoubleSided
                ? stats.sourceDoubleSidedInstanceCount > 0
                : stats.sourceSingleSidedInstanceCount > 0;
        }

        const wchar_t* ForwardDebugName(ClusterDrawCullModeBucket bucket) {
            return bucket == ClusterDrawCullModeBucket::DoubleSided
                ? L"Cluster Draw Forward DoubleSided PSO"
                : L"Cluster Draw Forward BackFace PSO";
        }

        const wchar_t* GeometryDebugName(ClusterDrawCullModeBucket bucket) {
            return bucket == ClusterDrawCullModeBucket::DoubleSided
                ? L"Cluster Draw GeometryAux DoubleSided PSO"
                : L"Cluster Draw GeometryAux BackFace PSO";
        }

        const char* PipelineEventName(
            ClusterDrawPipelineKind kind,
            ClusterDrawCullModeBucket bucket) {

            switch (kind) {
            case ClusterDrawPipelineKind::GeometryAux:
                return bucket == ClusterDrawCullModeBucket::DoubleSided
                    ? "ClusterDraw.GeometryAux.DoubleSided"
                    : "ClusterDraw.GeometryAux.BackFace";
            case ClusterDrawPipelineKind::ForwardOpaque:
            default:
                return bucket == ClusterDrawCullModeBucket::DoubleSided
                    ? "ClusterDraw.ForwardOpaque.DoubleSided"
                    : "ClusterDraw.ForwardOpaque.BackFace";
            }
        }
    }

    bool ClusterDrawExecutor::Initialize(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature) {

        Reset();
        if (device == nullptr || rootSignature == nullptr) {
            return false;
        }
        if (!GFX::SupportsShaderModel6(device)) {
            DEBUGLOG::PushRenderError("[ClusterDraw][ERROR] Shader Model 6.0 is not supported.");
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
        if (!GFX::CompileShaderFileSm6(
            L"HIKARI/Shaders/Render3D_ClusterVS.hlsl",
            "main",
            GFX::ShaderStage::Vertex,
            vertexShader.GetAddressOf())) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
        if (!GFX::CompileShaderFileSm6(
            L"HIKARI/Shaders/Render3D_StaticPS.hlsl",
            "main",
            GFX::ShaderStage::Pixel,
            pixelShader.GetAddressOf())) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> geometryPixelShader;
        if (!GFX::CompileShaderFileSm6(
            L"HIKARI/Shaders/Render3D_GeometryAuxPS.hlsl",
            "main",
            GFX::ShaderStage::Pixel,
            geometryPixelShader.GetAddressOf())) {
            return false;
        }

        for (size_t bucketIndex = 0; bucketIndex < kClusterDrawCullModeBucketCount; ++bucketIndex) {
            const ClusterDrawCullModeBucket bucket =
                static_cast<ClusterDrawCullModeBucket>(bucketIndex);
            if (!CreateClusterPipelineState(
                device,
                rootSignature,
                vertexShader.Get(),
                pixelShader.Get(),
                CullModeForBucket(bucket),
                ForwardDebugName(bucket),
                forwardPipelineStates_[bucketIndex].GetAddressOf())) {
                forwardPipelineStates_[bucketIndex].Reset();
                return false;
            }
            if (!CreateClusterPipelineState(
                device,
                rootSignature,
                vertexShader.Get(),
                geometryPixelShader.Get(),
                CullModeForBucket(bucket),
                GeometryDebugName(bucket),
                geometryAuxPipelineStates_[bucketIndex].GetAddressOf())) {
                geometryAuxPipelineStates_[bucketIndex].Reset();
                return false;
            }
        }
        return true;
    }

    void ClusterDrawExecutor::Reset() {
        for (auto& pipeline : forwardPipelineStates_) {
            pipeline.Reset();
        }
        for (auto& pipeline : geometryAuxPipelineStates_) {
            pipeline.Reset();
        }
        stats_ = {};
    }

    void ClusterDrawExecutor::ResetFrame() {
        stats_ = {};
        stats_.forwardPipelineReady = ArePipelinesReady(forwardPipelineStates_);
        stats_.geometryAuxPipelineReady = ArePipelinesReady(geometryAuxPipelineStates_);
        stats_.drawPipelineReady =
            stats_.forwardPipelineReady && stats_.geometryAuxPipelineReady;
    }

    bool ClusterDrawExecutor::Execute(const ClusterDrawExecutionContext& ctx) {
        if (ctx.cullingPass == nullptr) {
            return false;
        }

        const ClusterGpuCullingPassStats& cullStats = ctx.cullingPass->GetStats();
        const size_t requestedDrawCount = cullStats.submittedDrawSeedCount;
        stats_.requestedDrawCount += requestedDrawCount;
        stats_.drawArgumentBufferReady = ctx.cullingPass->GetDrawArgumentBuffer() != nullptr;
        stats_.drawCommandSignatureReady = ctx.cullingPass->GetDrawCommandSignature() != nullptr;
        stats_.forwardPipelineReady = ArePipelinesReady(forwardPipelineStates_);
        stats_.geometryAuxPipelineReady = ArePipelinesReady(geometryAuxPipelineStates_);
        stats_.drawPipelineReady =
            stats_.forwardPipelineReady && stats_.geometryAuxPipelineReady;

        if (requestedDrawCount == 0) {
            return false;
        }
        if (ctx.commandList == nullptr ||
            !stats_.drawArgumentBufferReady ||
            !stats_.drawCommandSignatureReady) {
            stats_.skippedDrawCount += requestedDrawCount;
            return false;
        }

        ID3D12Resource* argumentBuffer = ctx.cullingPass->GetDrawArgumentBuffer();
        ID3D12Resource* countBuffer = ctx.cullingPass->GetCounterBuffer();
        ID3D12CommandSignature* commandSignature =
            ctx.cullingPass->GetDrawCommandSignature();
        if (argumentBuffer == nullptr ||
            countBuffer == nullptr ||
            commandSignature == nullptr) {
            stats_.skippedDrawCount += requestedDrawCount;
            return false;
        }

        const size_t bucketCapacity = ctx.cullingPass->GetDrawArgumentBucketCapacity();
        const UINT maxCommandCount = static_cast<UINT>((std::min)(
            bucketCapacity,
            static_cast<size_t>((std::numeric_limits<UINT>::max)())));
        if (maxCommandCount == 0) {
            stats_.skippedDrawCount += requestedDrawCount;
            return false;
        }

        bool submittedAnyBucket = false;
        GFX::GPU_PROFILE::ScopedGpuTimer gpuDraw(
            ctx.commandList,
            ctx.pipelineKind == ClusterDrawPipelineKind::GeometryAux
                ? GFX::GPU_PROFILE::Pass::ClusterDrawGeometryAux
                : GFX::GPU_PROFILE::Pass::ClusterDrawForward);
        for (size_t bucketIndex = 0; bucketIndex < kClusterDrawCullModeBucketCount; ++bucketIndex) {
            const ClusterDrawCullModeBucket bucket =
                static_cast<ClusterDrawCullModeBucket>(bucketIndex);
            if (!HasSourceForBucket(cullStats, bucket)) {
                ++stats_.skippedBucketCount;
                continue;
            }
            ID3D12PipelineState* selectedPipeline =
                GetPipelineState(ctx.pipelineKind, bucket);
            if (selectedPipeline == nullptr) {
                continue;
            }

            ++stats_.submitCallCount;
            if (ctx.pipelineKind == ClusterDrawPipelineKind::GeometryAux) {
                ++stats_.geometryAuxSubmitCallCount;
            } else {
                ++stats_.forwardSubmitCallCount;
            }
            if (bucket == ClusterDrawCullModeBucket::DoubleSided) {
                ++stats_.doubleSidedSubmitCallCount;
            } else {
                ++stats_.backFaceSubmitCallCount;
            }

            GFX::PIX::ScopedGpuEvent pix(
                ctx.commandList,
                GFX::PIX::kColorRender,
                PipelineEventName(ctx.pipelineKind, bucket));

            ctx.commandList->SetPipelineState(selectedPipeline);
            ctx.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx.commandList->ExecuteIndirect(
                commandSignature,
                maxCommandCount,
                argumentBuffer,
                ctx.cullingPass->GetDrawArgumentBufferOffset(bucket),
                countBuffer,
                ctx.cullingPass->GetDrawCommandCounterOffset(bucket));
            submittedAnyBucket = true;
        }

        if (!submittedAnyBucket) {
            stats_.skippedDrawCount += requestedDrawCount;
            return false;
        }

        stats_.submittedDrawCount += requestedDrawCount;
        if (ctx.pipelineKind == ClusterDrawPipelineKind::GeometryAux) {
            stats_.geometryAuxSubmittedDrawCount += requestedDrawCount;
        } else {
            stats_.forwardSubmittedDrawCount += requestedDrawCount;
        }
        return true;
    }

    const ClusterDrawExecutorStats& ClusterDrawExecutor::GetStats() const {
        return stats_;
    }

    ID3D12PipelineState* ClusterDrawExecutor::GetPipelineState() const {
        return forwardPipelineStates_[0].Get();
    }

    ID3D12PipelineState* ClusterDrawExecutor::GetPipelineState(
        ClusterDrawPipelineKind kind) const {

        return GetPipelineState(kind, ClusterDrawCullModeBucket::BackFace);
    }

    ID3D12PipelineState* ClusterDrawExecutor::GetPipelineState(
        ClusterDrawPipelineKind kind,
        ClusterDrawCullModeBucket bucket) const {

        const size_t bucketIndex = static_cast<size_t>(bucket);
        if (bucketIndex >= kClusterDrawCullModeBucketCount) {
            return nullptr;
        }
        switch (kind) {
        case ClusterDrawPipelineKind::GeometryAux:
            return geometryAuxPipelineStates_[bucketIndex].Get();
        case ClusterDrawPipelineKind::ForwardOpaque:
        default:
            return forwardPipelineStates_[bucketIndex].Get();
        }
    }

} // namespace HIKARI::RENDER3D::CLUSTER
