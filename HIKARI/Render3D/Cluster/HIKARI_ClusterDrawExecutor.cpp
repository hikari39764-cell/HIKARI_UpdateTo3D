#include "Render3D/Cluster/HIKARI_ClusterDrawExecutor.h"

#include <algorithm>
#include <limits>

#include <d3dcompiler.h>
#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenDrawCommandStream.h"

namespace HIKARI::RENDER3D::CLUSTER {

    namespace {
        bool CreateClusterPipelineState(
            ID3D12Device* device,
            ID3D12RootSignature* rootSignature,
            ID3DBlob* vertexShader,
            ID3DBlob* pixelShader,
            D3D12_CULL_MODE cullMode,
            bool alphaBlend,
            bool depthWrite,
            bool hasRenderTarget,
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
            if (alphaBlend && hasRenderTarget) {
                D3D12_RENDER_TARGET_BLEND_DESC& rt0 =
                    psoDesc.BlendState.RenderTarget[0];
                rt0.BlendEnable = TRUE;
                rt0.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                rt0.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                rt0.BlendOp = D3D12_BLEND_OP_ADD;
                rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
                rt0.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            }
            if (!hasRenderTarget) {
                psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
                psoDesc.BlendState.IndependentBlendEnable = FALSE;
                for (D3D12_RENDER_TARGET_BLEND_DESC& rt :
                    psoDesc.BlendState.RenderTarget) {
                    rt.BlendEnable = FALSE;
                    rt.LogicOpEnable = FALSE;
                    rt.RenderTargetWriteMask = 0;
                }
            }
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.CullMode = cullMode;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = TRUE;
            psoDesc.DepthStencilState.DepthWriteMask =
                depthWrite
                    ? D3D12_DEPTH_WRITE_MASK_ALL
                    : D3D12_DEPTH_WRITE_MASK_ZERO;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            psoDesc.InputLayout = { nullptr, 0 };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = hasRenderTarget ? 1u : 0u;
            if (hasRenderTarget) {
                psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            }
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = 1;

            GFX::ClearD3D12InfoQueue(device);
            const HRESULT hr = device->CreateGraphicsPipelineState(
                &psoDesc,
                IID_PPV_ARGS(outPipelineState));
            if (!HIKARI_DX_CHECK(hr, "ClusterDraw::CreateGraphicsPipelineState")) {
                GFX::DumpD3D12InfoQueue(device, "ClusterDraw::CreateGraphicsPipelineState");
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

        D3D12_CULL_MODE CullModeForBucket(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? D3D12_CULL_MODE_NONE
                : D3D12_CULL_MODE_BACK;
        }

        bool HasSourceForBucket(
            const GPUDRIVEN::GpuVisibilityResult& visibility,
            GPUDRIVEN::GpuDrivenPassKind pass,
            GPUDRIVEN::GpuDrivenCommandBucket bucket) {

            return visibility.HasSourceForBucket(pass, bucket);
        }

        UINT64 DrawArgumentOffsetForBucket(
            const GPUDRIVEN::GpuDrivenCommandPassLayout& layout,
            GPUDRIVEN::GpuDrivenCommandBucket bucket) {

            return layout.GetBucket(bucket).gpuDrawIndexedArgumentOffset;
        }

        UINT64 CounterOffsetForBucket(
            const GPUDRIVEN::GpuDrivenCommandPassLayout& layout,
            GPUDRIVEN::GpuDrivenCommandBucket bucket) {

            return layout.GetBucket(bucket).counterOffset;
        }

        const wchar_t* ForwardDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Cluster Draw Forward DoubleSided PSO"
                : L"Cluster Draw Forward BackFace PSO";
        }

        const wchar_t* DepthAwareDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Cluster Draw DepthAware DoubleSided PSO"
                : L"Cluster Draw DepthAware BackFace PSO";
        }

        const wchar_t* TransparentDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Cluster Draw Transparent DoubleSided PSO"
                : L"Cluster Draw Transparent BackFace PSO";
        }

        const wchar_t* ShadowDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Cluster Draw Shadow DoubleSided PSO"
                : L"Cluster Draw Shadow BackFace PSO";
        }

        const wchar_t* GeometryDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Cluster Draw GeometryAux DoubleSided PSO"
                : L"Cluster Draw GeometryAux BackFace PSO";
        }

        const char* PipelineEventName(
            ClusterDrawPipelineKind kind,
            GPUDRIVEN::GpuDrivenCommandBucket bucket) {

            switch (kind) {
            case ClusterDrawPipelineKind::GeometryAux:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                    ? "ClusterDraw.GeometryAux.DoubleSided"
                    : "ClusterDraw.GeometryAux.BackFace";
            case ClusterDrawPipelineKind::ForwardDepthAware:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                    ? "ClusterDraw.DepthAware.DoubleSided"
                    : "ClusterDraw.DepthAware.BackFace";
            case ClusterDrawPipelineKind::ForwardTransparent:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                    ? "ClusterDraw.Transparent.DoubleSided"
                    : "ClusterDraw.Transparent.BackFace";
            case ClusterDrawPipelineKind::Shadow:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                    ? "ClusterDraw.Shadow.DoubleSided"
                    : "ClusterDraw.Shadow.BackFace";
            case ClusterDrawPipelineKind::ForwardOpaque:
            default:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
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
            L"HIKARI/Shaders/Render3D_GpuDrivenStaticFxPS.hlsl",
            "main",
            GFX::ShaderStage::Pixel,
            pixelShader.GetAddressOf())) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> depthAwarePixelShader;
        if (!GFX::CompileShaderFileSm6(
            L"HIKARI/Shaders/Render3D_GpuDrivenFxWaterPS.hlsl",
            "main",
            GFX::ShaderStage::Pixel,
            depthAwarePixelShader.GetAddressOf())) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> transparentPixelShader;
        if (!GFX::CompileShaderFileSm6(
            L"HIKARI/Shaders/Render3D_GpuDrivenStaticFxPS.hlsl",
            "main",
            GFX::ShaderStage::Pixel,
            transparentPixelShader.GetAddressOf())) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> shadowPixelShader;
        if (!GFX::CompileShaderFileSm6(
            L"HIKARI/Shaders/Render3D_MeshletShadowPS.hlsl",
            "main",
            GFX::ShaderStage::Pixel,
            shadowPixelShader.GetAddressOf())) {
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

        for (size_t bucketIndex = 0; bucketIndex < GPUDRIVEN::kGpuDrivenCommandBucketCount; ++bucketIndex) {
            const GPUDRIVEN::GpuDrivenCommandBucket bucket =
                static_cast<GPUDRIVEN::GpuDrivenCommandBucket>(bucketIndex);
            if (!CreateClusterPipelineState(
                device,
                rootSignature,
                vertexShader.Get(),
                pixelShader.Get(),
                CullModeForBucket(bucket),
                false,
                true,
                true,
                ForwardDebugName(bucket),
                forwardPipelineStates_[bucketIndex].GetAddressOf())) {
                forwardPipelineStates_[bucketIndex].Reset();
                return false;
            }
            if (!CreateClusterPipelineState(
                device,
                rootSignature,
                vertexShader.Get(),
                depthAwarePixelShader.Get(),
                CullModeForBucket(bucket),
                true,
                false,
                true,
                DepthAwareDebugName(bucket),
                depthAwarePipelineStates_[bucketIndex].GetAddressOf())) {
                depthAwarePipelineStates_[bucketIndex].Reset();
                return false;
            }
            if (!CreateClusterPipelineState(
                device,
                rootSignature,
                vertexShader.Get(),
                transparentPixelShader.Get(),
                CullModeForBucket(bucket),
                true,
                false,
                true,
                TransparentDebugName(bucket),
                transparentPipelineStates_[bucketIndex].GetAddressOf())) {
                transparentPipelineStates_[bucketIndex].Reset();
                return false;
            }
            if (!CreateClusterPipelineState(
                device,
                rootSignature,
                vertexShader.Get(),
                shadowPixelShader.Get(),
                CullModeForBucket(bucket),
                false,
                true,
                false,
                ShadowDebugName(bucket),
                shadowPipelineStates_[bucketIndex].GetAddressOf())) {
                shadowPipelineStates_[bucketIndex].Reset();
                return false;
            }
            if (!CreateClusterPipelineState(
                device,
                rootSignature,
                vertexShader.Get(),
                geometryPixelShader.Get(),
                CullModeForBucket(bucket),
                false,
                true,
                true,
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
        for (auto& pipeline : depthAwarePipelineStates_) {
            pipeline.Reset();
        }
        for (auto& pipeline : transparentPipelineStates_) {
            pipeline.Reset();
        }
        for (auto& pipeline : shadowPipelineStates_) {
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
        stats_.depthAwarePipelineReady = ArePipelinesReady(depthAwarePipelineStates_);
        stats_.transparentPipelineReady = ArePipelinesReady(transparentPipelineStates_);
        stats_.shadowPipelineReady = ArePipelinesReady(shadowPipelineStates_);
        stats_.geometryAuxPipelineReady = ArePipelinesReady(geometryAuxPipelineStates_);
        stats_.drawPipelineReady =
            stats_.forwardPipelineReady &&
            stats_.depthAwarePipelineReady &&
            stats_.transparentPipelineReady &&
            stats_.shadowPipelineReady &&
            stats_.geometryAuxPipelineReady;
    }

    bool ClusterDrawExecutor::Execute(const ClusterDrawExecutionContext& ctx) {
        if (ctx.visibility == nullptr || ctx.drawCommandRange == nullptr) {
            return false;
        }

        const GPUDRIVEN::GpuVisibilityResult& visibility = *ctx.visibility;
        const GPUDRIVEN::GpuDrivenDrawCommandRange& range =
            *ctx.drawCommandRange;
        if (!range.consumable ||
            range.backend != GPUDRIVEN::GeometryBackendKind::GpuDrivenClusterVS) {
            return false;
        }
        const size_t requestedDrawCount = range.commandCount;
        stats_.requestedDrawCount += requestedDrawCount;
        stats_.drawArgumentBufferReady = range.argumentBuffer != nullptr;
        stats_.drawCommandSignatureReady = range.commandSignature != nullptr;
        stats_.forwardPipelineReady = ArePipelinesReady(forwardPipelineStates_);
        stats_.depthAwarePipelineReady = ArePipelinesReady(depthAwarePipelineStates_);
        stats_.transparentPipelineReady = ArePipelinesReady(transparentPipelineStates_);
        stats_.shadowPipelineReady = ArePipelinesReady(shadowPipelineStates_);
        stats_.geometryAuxPipelineReady = ArePipelinesReady(geometryAuxPipelineStates_);
        stats_.drawPipelineReady =
            stats_.forwardPipelineReady &&
            stats_.depthAwarePipelineReady &&
            stats_.transparentPipelineReady &&
            stats_.shadowPipelineReady &&
            stats_.geometryAuxPipelineReady;

        if (requestedDrawCount == 0) {
            return false;
        }
        if (ctx.commandList == nullptr ||
            !range.HasGpuCommandLayout() ||
            !stats_.drawArgumentBufferReady ||
            !stats_.drawCommandSignatureReady) {
            stats_.skippedDrawCount += requestedDrawCount;
            return false;
        }

        ID3D12Resource* argumentBuffer = range.argumentBuffer;
        ID3D12Resource* countBuffer = visibility.counterBuffer;
        ID3D12CommandSignature* commandSignature = range.commandSignature;
        if (argumentBuffer == nullptr ||
            countBuffer == nullptr ||
            commandSignature == nullptr) {
            stats_.skippedDrawCount += requestedDrawCount;
            return false;
        }

        const GPUDRIVEN::GpuDrivenCommandPassLayout& layout =
            *range.gpuCommandLayout;
        const size_t bucketCapacity = layout.commandBucketCapacity;
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
        for (size_t bucketIndex = 0; bucketIndex < GPUDRIVEN::kGpuDrivenCommandBucketCount; ++bucketIndex) {
            const GPUDRIVEN::GpuDrivenCommandBucket bucket =
                static_cast<GPUDRIVEN::GpuDrivenCommandBucket>(bucketIndex);
            if (!HasSourceForBucket(visibility, ctx.pass, bucket)) {
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
            if (bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided) {
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
                DrawArgumentOffsetForBucket(layout, bucket),
                countBuffer,
                CounterOffsetForBucket(layout, bucket));
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

        return GetPipelineState(kind, GPUDRIVEN::GpuDrivenCommandBucket::BackFaceCulled);
    }

    ID3D12PipelineState* ClusterDrawExecutor::GetPipelineState(
        ClusterDrawPipelineKind kind,
        GPUDRIVEN::GpuDrivenCommandBucket bucket) const {

        const size_t bucketIndex = GPUDRIVEN::ToCommandBucketIndex(bucket);
        if (bucketIndex >= GPUDRIVEN::kGpuDrivenCommandBucketCount) {
            return nullptr;
        }
        switch (kind) {
        case ClusterDrawPipelineKind::GeometryAux:
            return geometryAuxPipelineStates_[bucketIndex].Get();
        case ClusterDrawPipelineKind::ForwardDepthAware:
            return depthAwarePipelineStates_[bucketIndex].Get();
        case ClusterDrawPipelineKind::ForwardTransparent:
            return transparentPipelineStates_[bucketIndex].Get();
        case ClusterDrawPipelineKind::Shadow:
            return shadowPipelineStates_[bucketIndex].Get();
        case ClusterDrawPipelineKind::ForwardOpaque:
        default:
            return forwardPipelineStates_[bucketIndex].Get();
        }
    }

} // namespace HIKARI::RENDER3D::CLUSTER
