#include "Render3D/Meshlet/HIKARI_MeshletRenderBackend.h"

#include <algorithm>
#include <limits>

#include <d3dcompiler.h>
#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenDrawCommandStream.h"
namespace HIKARI::RENDER3D::MESHLET {

    namespace {
        struct MeshletPipelineStateStream {
            CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
            CD3DX12_PIPELINE_STATE_STREAM_AS AS;
            CD3DX12_PIPELINE_STATE_STREAM_MS MS;
            CD3DX12_PIPELINE_STATE_STREAM_PS PS;
            CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC BlendState;
            CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencilState;
            CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
            CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER RasterizerState;
            CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
            CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC SampleDesc;
            CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK SampleMask;
        };

        bool ArePipelinesReady(const MeshletRenderBackend::PipelineBucketArray& pipelines) {
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

        UINT64 DispatchArgumentOffsetForBucket(
            const GPUDRIVEN::GpuDrivenCommandPassLayout& layout,
            GPUDRIVEN::GpuDrivenCommandBucket bucket) {

            return layout.GetBucket(bucket).meshDispatchArgumentOffset;
        }

        UINT64 CounterOffsetForBucket(
            const GPUDRIVEN::GpuDrivenCommandPassLayout& layout,
            GPUDRIVEN::GpuDrivenCommandBucket bucket) {

            return layout.GetBucket(bucket).counterOffset;
        }

        const wchar_t* ForwardDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Meshlet Forward DoubleSided PSO"
                : L"Meshlet Forward BackFace PSO";
        }

        const wchar_t* DepthAwareDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Meshlet DepthAware DoubleSided PSO"
                : L"Meshlet DepthAware BackFace PSO";
        }

        const wchar_t* TransparentDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Meshlet Transparent DoubleSided PSO"
                : L"Meshlet Transparent BackFace PSO";
        }

        const wchar_t* ShadowDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Meshlet Shadow DoubleSided PSO"
                : L"Meshlet Shadow BackFace PSO";
        }

        const wchar_t* GeometryDebugName(GPUDRIVEN::GpuDrivenCommandBucket bucket) {
            return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                ? L"Meshlet GeometryAux DoubleSided PSO"
                : L"Meshlet GeometryAux BackFace PSO";
        }

        const char* PipelineEventName(
            MeshletPipelineKind kind,
            GPUDRIVEN::GpuDrivenCommandBucket bucket) {

            switch (kind) {
            case MeshletPipelineKind::GeometryAux:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                    ? "MeshletDraw.GeometryAux.DoubleSided"
                    : "MeshletDraw.GeometryAux.BackFace";
            case MeshletPipelineKind::ForwardDepthAware:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                    ? "MeshletDraw.DepthAware.DoubleSided"
                    : "MeshletDraw.DepthAware.BackFace";
            case MeshletPipelineKind::ForwardTransparent:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                    ? "MeshletDraw.Transparent.DoubleSided"
                    : "MeshletDraw.Transparent.BackFace";
            case MeshletPipelineKind::Shadow:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                    ? "MeshletDraw.Shadow.DoubleSided"
                    : "MeshletDraw.Shadow.BackFace";
            case MeshletPipelineKind::ForwardOpaque:
            default:
                return bucket == GPUDRIVEN::GpuDrivenCommandBucket::DoubleSided
                    ? "MeshletDraw.ForwardOpaque.DoubleSided"
                    : "MeshletDraw.ForwardOpaque.BackFace";
            }
        }

        bool QueryMeshShaderCapabilities(
            ID3D12Device* device,
            MeshletRenderBackendStats& stats) {

            if (device == nullptr) {
                return false;
            }

            stats.shaderModel65Supported =
                GFX::SupportsShaderModel(device, D3D_SHADER_MODEL_6_5);

            D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
            HRESULT hr = device->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS7,
                &options7,
                sizeof(options7));
            if (SUCCEEDED(hr)) {
                stats.meshShaderTier = static_cast<uint32_t>(options7.MeshShaderTier);
                stats.meshShaderSupported =
                    options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
            }

            D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9{};
            hr = device->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS9,
                &options9,
                sizeof(options9));
            if (SUCCEEDED(hr)) {
                stats.meshShaderPipelineStatsSupported =
                    options9.MeshShaderPipelineStatsSupported != FALSE;
            }

            return stats.shaderModel65Supported && stats.meshShaderSupported;
        }

        bool CreateMeshletPipelineState(
            ID3D12Device* device,
            ID3D12RootSignature* rootSignature,
            ID3DBlob* amplificationShader,
            ID3DBlob* meshShader,
            ID3DBlob* pixelShader,
            DXGI_FORMAT renderTargetFormat,
            D3D12_CULL_MODE cullMode,
            bool alphaBlend,
            bool depthWrite,
            bool hasRenderTarget,
            const wchar_t* debugName,
            ID3D12PipelineState** outPipelineState) {

            if (device == nullptr ||
                rootSignature == nullptr ||
                amplificationShader == nullptr ||
                meshShader == nullptr ||
                pixelShader == nullptr ||
                outPipelineState == nullptr) {
                return false;
            }

            D3D12_RT_FORMAT_ARRAY renderTargets{};
            renderTargets.NumRenderTargets = hasRenderTarget ? 1u : 0u;
            if (hasRenderTarget) {
                renderTargets.RTFormats[0] = renderTargetFormat;
            }

            CD3DX12_RASTERIZER_DESC rasterizer(D3D12_DEFAULT);
            rasterizer.CullMode = cullMode;

            CD3DX12_DEPTH_STENCIL_DESC depthStencil(D3D12_DEFAULT);
            depthStencil.DepthEnable = TRUE;
            depthStencil.DepthWriteMask =
                depthWrite
                    ? D3D12_DEPTH_WRITE_MASK_ALL
                    : D3D12_DEPTH_WRITE_MASK_ZERO;
            depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            CD3DX12_BLEND_DESC blend(D3D12_DEFAULT);
            if (alphaBlend && hasRenderTarget) {
                D3D12_RENDER_TARGET_BLEND_DESC& rt0 = blend.RenderTarget[0];
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
                blend.AlphaToCoverageEnable = FALSE;
                blend.IndependentBlendEnable = FALSE;
                for (D3D12_RENDER_TARGET_BLEND_DESC& rt : blend.RenderTarget) {
                    rt.BlendEnable = FALSE;
                    rt.LogicOpEnable = FALSE;
                    rt.RenderTargetWriteMask = 0;
                }
            }

            MeshletPipelineStateStream stream{};
            stream.RootSignature = rootSignature;
            stream.AS = CD3DX12_SHADER_BYTECODE(amplificationShader);
            stream.MS = CD3DX12_SHADER_BYTECODE(meshShader);
            stream.PS = CD3DX12_SHADER_BYTECODE(pixelShader);
            stream.BlendState = blend;
            stream.DepthStencilState = depthStencil;
            stream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            stream.RasterizerState = rasterizer;
            stream.RTVFormats = renderTargets;
            stream.SampleDesc = DXGI_SAMPLE_DESC{ 1, 0 };
            stream.SampleMask = (std::numeric_limits<UINT>::max)();

            D3D12_PIPELINE_STATE_STREAM_DESC desc{};
            desc.SizeInBytes = sizeof(stream);
            desc.pPipelineStateSubobjectStream = &stream;

            Microsoft::WRL::ComPtr<ID3D12Device2> device2;
            HRESULT hr = device->QueryInterface(IID_PPV_ARGS(device2.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "MeshletRenderBackend::QueryDevice2")) {
                return false;
            }

            GFX::ClearD3D12InfoQueue(device);
            hr = device2->CreatePipelineState(
                &desc,
                IID_PPV_ARGS(outPipelineState));
            if (!HIKARI_DX_CHECK(hr, "MeshletRenderBackend::CreatePipelineState")) {
                GFX::DumpD3D12InfoQueue(device, "MeshletRenderBackend::CreatePipelineState");
                return false;
            }
            GFX::SetD3D12Name(*outPipelineState, debugName);
            return true;
        }
    }

    bool MeshletRenderBackend::Initialize(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature) {

        Reset();
        stats_.initialized = device != nullptr && rootSignature != nullptr;
        if (!stats_.initialized) {
            return false;
        }

        if (!QueryMeshShaderCapabilities(device, stats_)) {
            DEBUGLOG::PushRenderError(
                "[MeshletBackend][WARN] Mesh Shader backend is not supported by this device/runtime.");
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> amplificationShader;
        Microsoft::WRL::ComPtr<ID3DBlob> meshShader;
        Microsoft::WRL::ComPtr<ID3DBlob> forwardPixelShader;
        Microsoft::WRL::ComPtr<ID3DBlob> depthAwarePixelShader;
        Microsoft::WRL::ComPtr<ID3DBlob> transparentPixelShader;
        Microsoft::WRL::ComPtr<ID3DBlob> shadowPixelShader;
        Microsoft::WRL::ComPtr<ID3DBlob> geometryPixelShader;
        if (!GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_MeshletAS.hlsl",
                "main",
                GFX::ShaderStage::Amplification,
                amplificationShader.GetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_MeshletMS.hlsl",
                "main",
                GFX::ShaderStage::Mesh,
                meshShader.GetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_GpuDrivenStaticFxPS.hlsl",
                "main",
                GFX::ShaderStage::Pixel,
                forwardPixelShader.GetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_GpuDrivenFxWaterPS.hlsl",
                "main",
                GFX::ShaderStage::Pixel,
                depthAwarePixelShader.GetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_GpuDrivenStaticFxPS.hlsl",
                "main",
                GFX::ShaderStage::Pixel,
                transparentPixelShader.GetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_MeshletShadowPS.hlsl",
                "main",
                GFX::ShaderStage::Pixel,
                shadowPixelShader.GetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_GeometryAuxPS.hlsl",
                "main",
                GFX::ShaderStage::Pixel,
                geometryPixelShader.GetAddressOf())) {
            return false;
        }
        stats_.shaderCompileReady = true;

        for (size_t bucketIndex = 0; bucketIndex < GPUDRIVEN::kGpuDrivenCommandBucketCount; ++bucketIndex) {
            const GPUDRIVEN::GpuDrivenCommandBucket bucket =
                static_cast<GPUDRIVEN::GpuDrivenCommandBucket>(bucketIndex);

            ++stats_.pipelineCreateRequestCount;
            if (CreateMeshletPipelineState(
                    device,
                    rootSignature,
                    amplificationShader.Get(),
                    meshShader.Get(),
                    forwardPixelShader.Get(),
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    CullModeForBucket(bucket),
                    false,
                    true,
                    true,
                    ForwardDebugName(bucket),
                    forwardPipelineStates_[bucketIndex].GetAddressOf())) {
                ++stats_.pipelineCreateReadyCount;
            }

            ++stats_.pipelineCreateRequestCount;
            if (CreateMeshletPipelineState(
                    device,
                    rootSignature,
                    amplificationShader.Get(),
                    meshShader.Get(),
                    depthAwarePixelShader.Get(),
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    CullModeForBucket(bucket),
                    true,
                    false,
                    true,
                    DepthAwareDebugName(bucket),
                    depthAwarePipelineStates_[bucketIndex].GetAddressOf())) {
                ++stats_.pipelineCreateReadyCount;
            }

            ++stats_.pipelineCreateRequestCount;
            if (CreateMeshletPipelineState(
                    device,
                    rootSignature,
                    amplificationShader.Get(),
                    meshShader.Get(),
                    transparentPixelShader.Get(),
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    CullModeForBucket(bucket),
                    true,
                    false,
                    true,
                    TransparentDebugName(bucket),
                    transparentPipelineStates_[bucketIndex].GetAddressOf())) {
                ++stats_.pipelineCreateReadyCount;
            }

            ++stats_.pipelineCreateRequestCount;
            if (CreateMeshletPipelineState(
                    device,
                    rootSignature,
                    amplificationShader.Get(),
                    meshShader.Get(),
                    shadowPixelShader.Get(),
                    DXGI_FORMAT_UNKNOWN,
                    CullModeForBucket(bucket),
                    false,
                    true,
                    false,
                    ShadowDebugName(bucket),
                    shadowPipelineStates_[bucketIndex].GetAddressOf())) {
                ++stats_.pipelineCreateReadyCount;
            }

            ++stats_.pipelineCreateRequestCount;
            if (CreateMeshletPipelineState(
                    device,
                    rootSignature,
                    amplificationShader.Get(),
                    meshShader.Get(),
                    geometryPixelShader.Get(),
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    CullModeForBucket(bucket),
                    false,
                    true,
                    true,
                    GeometryDebugName(bucket),
                    geometryAuxPipelineStates_[bucketIndex].GetAddressOf())) {
                ++stats_.pipelineCreateReadyCount;
            }
        }

        ResetFrame();
        return stats_.pipelineReady;
    }

    void MeshletRenderBackend::Reset() {
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

    void MeshletRenderBackend::ResetFrame() {
        const MeshletRenderBackendStats persistent = stats_;
        stats_ = {};
        stats_.initialized = persistent.initialized;
        stats_.shaderModel65Supported = persistent.shaderModel65Supported;
        stats_.meshShaderSupported = persistent.meshShaderSupported;
        stats_.meshShaderPipelineStatsSupported =
            persistent.meshShaderPipelineStatsSupported;
        stats_.shaderCompileReady = persistent.shaderCompileReady;
        stats_.meshShaderTier = persistent.meshShaderTier;
        stats_.pipelineCreateRequestCount =
            persistent.pipelineCreateRequestCount;
        stats_.pipelineCreateReadyCount =
            persistent.pipelineCreateReadyCount;

        const bool forwardReady = ArePipelinesReady(forwardPipelineStates_);
        const bool depthAwareReady = ArePipelinesReady(depthAwarePipelineStates_);
        const bool transparentReady = ArePipelinesReady(transparentPipelineStates_);
        const bool shadowReady = ArePipelinesReady(shadowPipelineStates_);
        const bool geometryReady = ArePipelinesReady(geometryAuxPipelineStates_);
        stats_.forwardPipelineReady = forwardReady;
        stats_.depthAwarePipelineReady = depthAwareReady;
        stats_.transparentPipelineReady = transparentReady;
        stats_.shadowPipelineReady = shadowReady;
        stats_.geometryAuxPipelineReady = geometryReady;
        stats_.pipelineReady =
            forwardReady &&
            depthAwareReady &&
            transparentReady &&
            shadowReady &&
            geometryReady;
    }

    bool MeshletRenderBackend::Execute(const MeshletRenderExecutionContext& ctx) {
        if (ctx.visibility == nullptr || ctx.drawCommandRange == nullptr) {
            return false;
        }

        const GPUDRIVEN::GpuVisibilityResult& visibility = *ctx.visibility;
        const GPUDRIVEN::GpuDrivenDrawCommandRange& range =
            *ctx.drawCommandRange;
        if (!range.consumable ||
            range.backend != GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader) {
            return false;
        }
        const size_t requestedDispatchCount = range.commandCount;
        stats_.requestedDispatchCount += requestedDispatchCount;
        stats_.dispatchArgumentBufferReady =
            range.argumentBuffer != nullptr;
        stats_.dispatchCommandSignatureReady =
            range.commandSignature != nullptr;
        stats_.forwardPipelineReady = ArePipelinesReady(forwardPipelineStates_);
        stats_.depthAwarePipelineReady = ArePipelinesReady(depthAwarePipelineStates_);
        stats_.transparentPipelineReady = ArePipelinesReady(transparentPipelineStates_);
        stats_.shadowPipelineReady = ArePipelinesReady(shadowPipelineStates_);
        stats_.geometryAuxPipelineReady = ArePipelinesReady(geometryAuxPipelineStates_);
        stats_.pipelineReady =
            stats_.forwardPipelineReady &&
            stats_.depthAwarePipelineReady &&
            stats_.transparentPipelineReady &&
            stats_.shadowPipelineReady &&
            stats_.geometryAuxPipelineReady;
        const bool requestedPipelineReady =
            ctx.pipelineKind == MeshletPipelineKind::GeometryAux
                ? stats_.geometryAuxPipelineReady
                : ctx.pipelineKind == MeshletPipelineKind::ForwardDepthAware
                    ? stats_.depthAwarePipelineReady
                    : ctx.pipelineKind == MeshletPipelineKind::ForwardTransparent
                        ? stats_.transparentPipelineReady
                        : ctx.pipelineKind == MeshletPipelineKind::Shadow
                            ? stats_.shadowPipelineReady
                            : stats_.forwardPipelineReady;

        if (requestedDispatchCount == 0) {
            return false;
        }
        if (ctx.commandList == nullptr ||
            !range.HasGpuCommandLayout() ||
            !stats_.dispatchArgumentBufferReady ||
            !stats_.dispatchCommandSignatureReady ||
            !requestedPipelineReady ||
            visibility.visibleMeshletRangeBuffer == nullptr) {
            stats_.skippedDispatchCount += requestedDispatchCount;
            return false;
        }

        ID3D12Resource* argumentBuffer = range.argumentBuffer;
        ID3D12Resource* countBuffer = visibility.counterBuffer;
        ID3D12CommandSignature* commandSignature =
            range.commandSignature;
        if (argumentBuffer == nullptr ||
            countBuffer == nullptr ||
            commandSignature == nullptr) {
            stats_.skippedDispatchCount += requestedDispatchCount;
            return false;
        }

        const GPUDRIVEN::GpuDrivenCommandPassLayout& layout =
            *range.gpuCommandLayout;
        const size_t bucketCapacity = layout.commandBucketCapacity;
        if (bucketCapacity == 0) {
            stats_.skippedDispatchCount += requestedDispatchCount;
            return false;
        }
        constexpr UINT maxCommandCount = 1u;

        bool submittedAnyBucket = false;
        GFX::GPU_PROFILE::ScopedGpuTimer gpuDraw(
            ctx.commandList,
            ctx.pipelineKind == MeshletPipelineKind::GeometryAux
                ? GFX::GPU_PROFILE::Pass::MeshletDrawGeometryAux
                : GFX::GPU_PROFILE::Pass::MeshletDrawForward);
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
            ctx.commandList->ExecuteIndirect(
                commandSignature,
                maxCommandCount,
                argumentBuffer,
                DispatchArgumentOffsetForBucket(layout, bucket),
                countBuffer,
                CounterOffsetForBucket(layout, bucket));
            submittedAnyBucket = true;
        }

        if (!submittedAnyBucket) {
            stats_.skippedDispatchCount += requestedDispatchCount;
            return false;
        }

        stats_.submittedDispatchCount += requestedDispatchCount;
        if (ctx.pipelineKind == MeshletPipelineKind::GeometryAux) {
            stats_.geometryAuxSubmittedDispatchCount += requestedDispatchCount;
        } else {
            stats_.forwardSubmittedDispatchCount += requestedDispatchCount;
        }
        return true;
    }

    const MeshletRenderBackendStats& MeshletRenderBackend::GetStats() const {
        return stats_;
    }

    ID3D12PipelineState* MeshletRenderBackend::GetPipelineState(
        MeshletPipelineKind kind,
        GPUDRIVEN::GpuDrivenCommandBucket bucket) const {

        const size_t index = GPUDRIVEN::ToCommandBucketIndex(bucket);
        if (index >= GPUDRIVEN::kGpuDrivenCommandBucketCount) {
            return nullptr;
        }
        switch (kind) {
        case MeshletPipelineKind::GeometryAux:
            return geometryAuxPipelineStates_[index].Get();
        case MeshletPipelineKind::ForwardDepthAware:
            return depthAwarePipelineStates_[index].Get();
        case MeshletPipelineKind::ForwardTransparent:
            return transparentPipelineStates_[index].Get();
        case MeshletPipelineKind::Shadow:
            return shadowPipelineStates_[index].Get();
        case MeshletPipelineKind::ForwardOpaque:
        default:
            return forwardPipelineStates_[index].Get();
        }
    }

} // namespace HIKARI::RENDER3D::MESHLET
