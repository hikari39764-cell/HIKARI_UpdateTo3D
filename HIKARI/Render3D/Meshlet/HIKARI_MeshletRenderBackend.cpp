#include "Render3D/Meshlet/HIKARI_MeshletRenderBackend.h"

#include <algorithm>
#include <limits>

#include <d3dcompiler.h>
#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/Cluster/HIKARI_ClusterGpuCullingPass.h"

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
            CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
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

        D3D12_CULL_MODE CullModeForBucket(MeshletCullModeBucket bucket) {
            return bucket == MeshletCullModeBucket::DoubleSided
                ? D3D12_CULL_MODE_NONE
                : D3D12_CULL_MODE_BACK;
        }

        CLUSTER::ClusterDrawCullModeBucket ToClusterBucket(MeshletCullModeBucket bucket) {
            return bucket == MeshletCullModeBucket::DoubleSided
                ? CLUSTER::ClusterDrawCullModeBucket::DoubleSided
                : CLUSTER::ClusterDrawCullModeBucket::BackFace;
        }

        bool HasSourceForBucket(
            const CLUSTER::ClusterGpuCullingPassStats& stats,
            MeshletCullModeBucket bucket) {

            const size_t knownBucketSourceCount =
                stats.sourceSingleSidedInstanceCount +
                stats.sourceDoubleSidedInstanceCount;
            if (knownBucketSourceCount == 0) {
                return true;
            }
            return bucket == MeshletCullModeBucket::DoubleSided
                ? stats.sourceDoubleSidedInstanceCount > 0
                : stats.sourceSingleSidedInstanceCount > 0;
        }

        const wchar_t* ForwardDebugName(MeshletCullModeBucket bucket) {
            return bucket == MeshletCullModeBucket::DoubleSided
                ? L"Meshlet Forward DoubleSided PSO"
                : L"Meshlet Forward BackFace PSO";
        }

        const wchar_t* GeometryDebugName(MeshletCullModeBucket bucket) {
            return bucket == MeshletCullModeBucket::DoubleSided
                ? L"Meshlet GeometryBuffer DoubleSided PSO"
                : L"Meshlet GeometryBuffer BackFace PSO";
        }

        const char* PipelineEventName(
            MeshletPipelineKind kind,
            MeshletCullModeBucket bucket) {

            switch (kind) {
            case MeshletPipelineKind::GeometryBuffer:
                return bucket == MeshletCullModeBucket::DoubleSided
                    ? "MeshletDraw.GeometryBuffer.DoubleSided"
                    : "MeshletDraw.GeometryBuffer.BackFace";
            case MeshletPipelineKind::ForwardOpaque:
            default:
                return bucket == MeshletCullModeBucket::DoubleSided
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
            renderTargets.NumRenderTargets = 1;
            renderTargets.RTFormats[0] = renderTargetFormat;

            CD3DX12_RASTERIZER_DESC rasterizer(D3D12_DEFAULT);
            rasterizer.CullMode = cullMode;

            CD3DX12_DEPTH_STENCIL_DESC depthStencil(D3D12_DEFAULT);
            depthStencil.DepthEnable = TRUE;
            depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

            MeshletPipelineStateStream stream{};
            stream.RootSignature = rootSignature;
            stream.AS = CD3DX12_SHADER_BYTECODE(amplificationShader);
            stream.MS = CD3DX12_SHADER_BYTECODE(meshShader);
            stream.PS = CD3DX12_SHADER_BYTECODE(pixelShader);
            stream.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            stream.DepthStencilState = depthStencil;
            stream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            stream.RasterizerState = rasterizer;
            stream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
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

            hr = device2->CreatePipelineState(
                &desc,
                IID_PPV_ARGS(outPipelineState));
            if (!HIKARI_DX_CHECK(hr, "MeshletRenderBackend::CreatePipelineState")) {
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
                L"HIKARI/Shaders/Render3D_StaticPS.hlsl",
                "main",
                GFX::ShaderStage::Pixel,
                forwardPixelShader.GetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_GeometryBufferPS.hlsl",
                "main",
                GFX::ShaderStage::Pixel,
                geometryPixelShader.GetAddressOf())) {
            return false;
        }
        stats_.shaderCompileReady = true;

        for (size_t bucketIndex = 0; bucketIndex < kMeshletCullModeBucketCount; ++bucketIndex) {
            const MeshletCullModeBucket bucket =
                static_cast<MeshletCullModeBucket>(bucketIndex);

            ++stats_.pipelineCreateRequestCount;
            if (CreateMeshletPipelineState(
                    device,
                    rootSignature,
                    amplificationShader.Get(),
                    meshShader.Get(),
                    forwardPixelShader.Get(),
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    CullModeForBucket(bucket),
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
                    geometryPixelShader.Get(),
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    CullModeForBucket(bucket),
                    GeometryDebugName(bucket),
                    geometryBufferPipelineStates_[bucketIndex].GetAddressOf())) {
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
        for (auto& pipeline : geometryBufferPipelineStates_) {
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
        const bool geometryReady = ArePipelinesReady(geometryBufferPipelineStates_);
        stats_.forwardPipelineReady = forwardReady;
        stats_.geometryBufferPipelineReady = geometryReady;
        stats_.pipelineReady = forwardReady && geometryReady;
    }

    bool MeshletRenderBackend::Execute(const MeshletRenderExecutionContext& ctx) {
        if (ctx.cullingPass == nullptr) {
            return false;
        }

        const CLUSTER::ClusterGpuCullingPassStats& cullStats =
            ctx.cullingPass->GetStats();
        const size_t requestedDispatchCount = cullStats.submittedDrawSeedCount;
        stats_.requestedDispatchCount += requestedDispatchCount;
        stats_.dispatchArgumentBufferReady =
            ctx.cullingPass->GetMeshletDispatchArgumentBuffer() != nullptr;
        stats_.dispatchCommandSignatureReady =
            ctx.cullingPass->GetMeshletDispatchCommandSignature() != nullptr;
        stats_.forwardPipelineReady = ArePipelinesReady(forwardPipelineStates_);
        stats_.geometryBufferPipelineReady = ArePipelinesReady(geometryBufferPipelineStates_);
        stats_.pipelineReady =
            stats_.forwardPipelineReady && stats_.geometryBufferPipelineReady;
        const bool requestedPipelineReady =
            ctx.pipelineKind == MeshletPipelineKind::GeometryBuffer
                ? stats_.geometryBufferPipelineReady
                : stats_.forwardPipelineReady;

        if (requestedDispatchCount == 0) {
            return false;
        }
        if (ctx.commandList == nullptr ||
            !stats_.dispatchArgumentBufferReady ||
            !stats_.dispatchCommandSignatureReady ||
            !requestedPipelineReady ||
            ctx.cullingPass->GetVisibleRangeBuffer() == nullptr) {
            stats_.skippedDispatchCount += requestedDispatchCount;
            return false;
        }

        ID3D12Resource* argumentBuffer =
            ctx.cullingPass->GetMeshletDispatchArgumentBuffer();
        ID3D12Resource* countBuffer = ctx.cullingPass->GetCounterBuffer();
        ID3D12CommandSignature* commandSignature =
            ctx.cullingPass->GetMeshletDispatchCommandSignature();
        if (argumentBuffer == nullptr ||
            countBuffer == nullptr ||
            commandSignature == nullptr) {
            stats_.skippedDispatchCount += requestedDispatchCount;
            return false;
        }

        const size_t bucketCapacity = ctx.cullingPass->GetDrawArgumentBucketCapacity();
        const UINT maxCommandCount = static_cast<UINT>((std::min)(
            bucketCapacity,
            static_cast<size_t>((std::numeric_limits<UINT>::max)())));
        if (maxCommandCount == 0) {
            stats_.skippedDispatchCount += requestedDispatchCount;
            return false;
        }

        bool submittedAnyBucket = false;
        GFX::GPU_PROFILE::ScopedGpuTimer gpuDraw(
            ctx.commandList,
            ctx.pipelineKind == MeshletPipelineKind::GeometryBuffer
                ? GFX::GPU_PROFILE::Pass::ClusterDrawGeometry
                : GFX::GPU_PROFILE::Pass::ClusterDrawForward);
        for (size_t bucketIndex = 0; bucketIndex < kMeshletCullModeBucketCount; ++bucketIndex) {
            const MeshletCullModeBucket bucket =
                static_cast<MeshletCullModeBucket>(bucketIndex);
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
            if (bucket == MeshletCullModeBucket::DoubleSided) {
                ++stats_.doubleSidedSubmitCallCount;
            } else {
                ++stats_.backFaceSubmitCallCount;
            }

            const CLUSTER::ClusterDrawCullModeBucket clusterBucket =
                ToClusterBucket(bucket);
            GFX::PIX::ScopedGpuEvent pix(
                ctx.commandList,
                GFX::PIX::kColorRender,
                PipelineEventName(ctx.pipelineKind, bucket));

            ctx.commandList->SetPipelineState(selectedPipeline);
            ctx.commandList->ExecuteIndirect(
                commandSignature,
                maxCommandCount,
                argumentBuffer,
                ctx.cullingPass->GetMeshletDispatchArgumentBufferOffset(clusterBucket),
                countBuffer,
                ctx.cullingPass->GetDrawCommandCounterOffset(clusterBucket));
            submittedAnyBucket = true;
        }

        if (!submittedAnyBucket) {
            stats_.skippedDispatchCount += requestedDispatchCount;
            return false;
        }

        stats_.submittedDispatchCount += requestedDispatchCount;
        if (ctx.pipelineKind == MeshletPipelineKind::GeometryBuffer) {
            stats_.geometryBufferSubmittedDispatchCount += requestedDispatchCount;
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
        MeshletCullModeBucket bucket) const {

        const size_t index = static_cast<size_t>(bucket);
        if (index >= kMeshletCullModeBucketCount) {
            return nullptr;
        }
        return kind == MeshletPipelineKind::GeometryBuffer
            ? geometryBufferPipelineStates_[index].Get()
            : forwardPipelineStates_[index].Get();
    }

} // namespace HIKARI::RENDER3D::MESHLET
