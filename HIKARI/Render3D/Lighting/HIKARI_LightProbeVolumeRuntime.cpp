#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <utility>
#include <vector>

#include <d3dx12.h>
#include <wrl/client.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include "Assets/Lighting/HIKARI_LightProbeVolumeFormat.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "HIKARI_Services.h"

namespace HIKARI::RENDER3D::LIGHTPROBE {

    namespace {

        struct GpuShCoeff {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 0.0f;
        };

        LightProbeVolumeRuntimeData gData{};
        Microsoft::WRL::ComPtr<ID3D12Resource> gShBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> gUploadBuffer{};
        D3D12_GPU_DESCRIPTOR_HANDLE gShBufferSrv{};
        int gSamplingSuppressDepth = 0;

        void SetMessage(std::string* outMessage, std::string message) {
            if (outMessage != nullptr) {
                *outMessage = std::move(message);
            }
        }

        D3D12_CPU_DESCRIPTOR_HANDLE LightProbeSrvCpu() {
            ID3D12Device* device = SERVICES::gCtx.device;
            ID3D12DescriptorHeap* heap = SERVICES::gCtx.srvHeap;
            if (device == nullptr || heap == nullptr) {
                return {};
            }

            const UINT descriptorSize =
                device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return GFX::DESCRIPTOR::CpuAt(
                heap,
                descriptorSize,
                GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::LightProbeSh));
        }

        D3D12_GPU_DESCRIPTOR_HANDLE LightProbeSrvGpu() {
            ID3D12Device* device = SERVICES::gCtx.device;
            ID3D12DescriptorHeap* heap = SERVICES::gCtx.srvHeap;
            if (device == nullptr || heap == nullptr) {
                return {};
            }

            const UINT descriptorSize =
                device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return GFX::DESCRIPTOR::GpuAt(
                heap,
                descriptorSize,
                GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::LightProbeSh));
        }

        void WriteNullSrv() {
            ID3D12Device* device = SERVICES::gCtx.device;
            const D3D12_CPU_DESCRIPTOR_HANDLE cpu = LightProbeSrvCpu();
            if (device == nullptr || cpu.ptr == 0) {
                gShBufferSrv = {};
                return;
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = DXGI_FORMAT_UNKNOWN;
            srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Buffer.NumElements = 1;
            srv.Buffer.StructureByteStride = sizeof(GpuShCoeff);
            srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            device->CreateShaderResourceView(nullptr, &srv, cpu);
            gShBufferSrv = LightProbeSrvGpu();
        }

        std::vector<GpuShCoeff> BuildGpuPayload(
            const ASSETS::LIGHTING::LightProbeVolumeFileData& fileData) {

            std::vector<GpuShCoeff> payload{};
            payload.reserve(fileData.probes.size() * ASSETS::LIGHTING::kLightProbeShCoeffCount);
            for (const ASSETS::LIGHTING::LightProbeSh9& probe : fileData.probes) {
                for (const MATH::Vec3& coeff : probe.coeffs) {
                    payload.push_back({ coeff.x, coeff.y, coeff.z, 0.0f });
                }
            }
            return payload;
        }

        bool UploadPayload(
            const std::vector<GpuShCoeff>& payload,
            std::string* outMessage) {

            ID3D12Device* device = SERVICES::gCtx.device;
            ID3D12CommandQueue* queue = SERVICES::gCtx.queue;
            if (device == nullptr || queue == nullptr || payload.empty()) {
                SetMessage(outMessage, "Light probe GPU upload context is missing.");
                return false;
            }

            const UINT64 bytes = static_cast<UINT64>(payload.size() * sizeof(GpuShCoeff));
            const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
            const CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bytes);

            Microsoft::WRL::ComPtr<ID3D12Resource> shBuffer{};
            HRESULT hr = device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(shBuffer.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "LightProbeVolumeRuntime::CreateShBuffer")) {
                SetMessage(outMessage, "Failed to create light probe SH buffer.");
                return false;
            }

            const CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
            Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer{};
            hr = device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(uploadBuffer.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "LightProbeVolumeRuntime::CreateUploadBuffer")) {
                SetMessage(outMessage, "Failed to create light probe upload buffer.");
                return false;
            }

            void* mapped = nullptr;
            hr = uploadBuffer->Map(0, nullptr, &mapped);
            if (FAILED(hr) || mapped == nullptr) {
                SetMessage(outMessage, "Failed to map light probe upload buffer.");
                return false;
            }
            std::memcpy(mapped, payload.data(), static_cast<size_t>(bytes));
            uploadBuffer->Unmap(0, nullptr);

            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator{};
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmd{};
            Microsoft::WRL::ComPtr<ID3D12Fence> fence{};
            hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.GetAddressOf()));
            if (FAILED(hr)) {
                SetMessage(outMessage, "Failed to create light probe upload allocator.");
                return false;
            }
            hr = device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(),
                nullptr,
                IID_PPV_ARGS(cmd.GetAddressOf()));
            if (FAILED(hr)) {
                SetMessage(outMessage, "Failed to create light probe upload command list.");
                return false;
            }
            cmd->CopyBufferRegion(shBuffer.Get(), 0, uploadBuffer.Get(), 0, bytes);
            const CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                shBuffer.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            cmd->ResourceBarrier(1, &barrier);
            hr = cmd->Close();
            if (FAILED(hr)) {
                SetMessage(outMessage, "Failed to close light probe upload command list.");
                return false;
            }

            ID3D12CommandList* lists[] = { cmd.Get() };
            queue->ExecuteCommandLists(1, lists);

            hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
            if (FAILED(hr)) {
                SetMessage(outMessage, "Failed to create light probe upload fence.");
                return false;
            }

            constexpr uint64_t kFenceValue = 1;
            hr = queue->Signal(fence.Get(), kFenceValue);
            if (FAILED(hr)) {
                SetMessage(outMessage, "Failed to signal light probe upload fence.");
                return false;
            }
            if (fence->GetCompletedValue() < kFenceValue) {
                HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (eventHandle == nullptr) {
                    SetMessage(outMessage, "Failed to create light probe upload fence event.");
                    return false;
                }
                hr = fence->SetEventOnCompletion(kFenceValue, eventHandle);
                if (SUCCEEDED(hr)) {
                    WaitForSingleObject(eventHandle, INFINITE);
                }
                CloseHandle(eventHandle);
                if (FAILED(hr)) {
                    SetMessage(outMessage, "Failed to wait light probe upload fence.");
                    return false;
                }
            }

            const D3D12_CPU_DESCRIPTOR_HANDLE cpu = LightProbeSrvCpu();
            if (cpu.ptr == 0) {
                SetMessage(outMessage, "Light probe SRV descriptor is missing.");
                return false;
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = DXGI_FORMAT_UNKNOWN;
            srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Buffer.NumElements = static_cast<UINT>(payload.size());
            srv.Buffer.StructureByteStride = sizeof(GpuShCoeff);
            srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            device->CreateShaderResourceView(shBuffer.Get(), &srv, cpu);

            gShBuffer = std::move(shBuffer);
            gUploadBuffer = std::move(uploadBuffer);
            gShBufferSrv = LightProbeSrvGpu();
            return gShBufferSrv.ptr != 0;
        }

        void RefreshValidity() {
            gData.valid = gData.enabled &&
                gSamplingSuppressDepth <= 0 &&
                gData.probeCount > 0u &&
                gData.countX >= 2u &&
                gData.countY >= 1u &&
                gData.countZ >= 2u &&
                gShBufferSrv.ptr != 0 &&
                gData.intensity > 0.0f;
        }

        void LogState(const char* reason) {
            std::ostringstream oss{};
            oss << "[Environment][LightProbe]"
                << " reason=" << (reason ? reason : "state")
                << " enabled=" << (gData.enabled ? "true" : "false")
                << " valid=" << (gData.valid ? "true" : "false")
                << " probes=" << gData.probeCount
                << " grid=" << gData.countX << "x" << gData.countY << "x" << gData.countZ
                << " intensity=" << gData.intensity
                << " path=" << gData.sourcePath;
            HIKARI_LOG_INFO(oss.str());
        }

    } // namespace

    void Reset() {
        gData = {};
        gShBuffer.Reset();
        gUploadBuffer.Reset();
        WriteNullSrv();
        RefreshValidity();
    }

    bool LoadLightProbeVolume(
        const std::filesystem::path& path,
        std::string* outMessage) {

        ASSETS::LIGHTING::LightProbeVolumeFileData fileData{};
        std::string loadMessage{};
        if (!ASSETS::LIGHTING::LoadLightProbeVolumeFile(path, fileData, &loadMessage)) {
            Reset();
            SetMessage(outMessage, loadMessage);
            return false;
        }

        const std::vector<GpuShCoeff> payload = BuildGpuPayload(fileData);
        if (!UploadPayload(payload, outMessage)) {
            Reset();
            return false;
        }

        gData.enabled = true;
        gData.origin = fileData.origin;
        gData.size = fileData.size;
        gData.spacing = fileData.spacing;
        gData.countX = fileData.countX;
        gData.countY = fileData.countY;
        gData.countZ = fileData.countZ;
        gData.probeCount = static_cast<uint32_t>(fileData.probes.size());
        gData.sourcePath = path.lexically_normal().generic_string();
        RefreshValidity();
        LogState("Load");
        SetMessage(outMessage, loadMessage);
        return gData.valid;
    }

    void SetLightProbeVolumeEnabled(bool enabled) {
        gData.enabled = enabled;
        RefreshValidity();
    }

    void SetLightProbeVolumeIntensity(float intensity) {
        gData.intensity = (std::max)(0.0f, intensity);
        RefreshValidity();
    }

    const LightProbeVolumeRuntimeData& GetRuntimeData() {
        RefreshValidity();
        return gData;
    }

    bool IsLightProbeVolumeSamplingSuppressed() {
        return gSamplingSuppressDepth > 0;
    }

    ScopedLightProbeVolumeSamplingSuppress::ScopedLightProbeVolumeSamplingSuppress() {
        ++gSamplingSuppressDepth;
    }

    ScopedLightProbeVolumeSamplingSuppress::~ScopedLightProbeVolumeSamplingSuppress() {
        gSamplingSuppressDepth = (std::max)(0, gSamplingSuppressDepth - 1);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetShBufferSrv() {
        if (gShBufferSrv.ptr == 0) {
            WriteNullSrv();
        }
        return gShBufferSrv;
    }

    bool IsValid() {
        RefreshValidity();
        return gData.valid;
    }

} // namespace HIKARI::RENDER3D::LIGHTPROBE
