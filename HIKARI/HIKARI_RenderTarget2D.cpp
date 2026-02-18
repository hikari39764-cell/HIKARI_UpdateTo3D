#include "HIKARI_RenderTarget2D.h"
#include <cassert>

using Microsoft::WRL::ComPtr;

namespace HIKARI {

    bool RenderTarget2D::Init(int width, int height, DXGI_FORMAT format)
    {
        if (initialized_) {
            return true;
        }

        width_ = width;
        height_ = height;
        format_ = format;

        if (!CreateResources()) {
            return false;
        }

        viewport_.TopLeftX = 0.0f;
        viewport_.TopLeftY = 0.0f;
        viewport_.Width = static_cast<float>(width_);
        viewport_.Height = static_cast<float>(height_);
        viewport_.MinDepth = 0.0f;
        viewport_.MaxDepth = 1.0f;

        scissorRect_.left = 0;
        scissorRect_.top = 0;
        scissorRect_.right = width_;
        scissorRect_.bottom = height_;

        initialized_ = true;
        return true;
    }

    void RenderTarget2D::UpdateContext(const HIKARI::GFX::Context& ctx)
    {
        context_ = ctx;
    }

    void RenderTarget2D::Finalize()
    {
        if (!initialized_) {
            return;
        }

        colorTex_.Reset();
        rtvHeap_.Reset();
        srvHeap_.Reset();

        initialized_ = false;
    }

    bool RenderTarget2D::CreateResources()
    {
        ID3D12Device* device = context_.device;
        if (!device) { return false; }

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = format_;
        clearValue.Color[0] = 0.0f;
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 0.0f;

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            format_,
            static_cast<UINT64>(width_),
            static_cast<UINT>(height_),
            1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
        );

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearValue,
            IID_PPV_ARGS(&colorTex_)
        );
        assert(SUCCEEDED(hr));
        colorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;


        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = 1;
        rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        hr = device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap_));
        assert(SUCCEEDED(hr));
        rtvHandle_ = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(colorTex_.Get(), nullptr, rtvHandle_);


        D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
        srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.NumDescriptors = 1;
        srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        hr = device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap_));
        assert(SUCCEEDED(hr));

        srvCpuHandle_ = srvHeap_->GetCPUDescriptorHandleForHeapStart();
        srvGpuHandle_ = srvHeap_->GetGPUDescriptorHandleForHeapStart();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvView{};
        srvView.Format = format_;
        srvView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvView.Texture2D.MipLevels = 1;

        device->CreateShaderResourceView(colorTex_.Get(), &srvView, srvCpuHandle_);

        return true;
    }

    void RenderTarget2D::BeginCapture(float r, float g, float b, float a)
    {
        if (!initialized_) {
            return;
        }

        ID3D12GraphicsCommandList* cmd = context_.cmdList;
        if (!cmd) { return; }

        if (colorState_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                colorTex_.Get(),
                colorState_,
                D3D12_RESOURCE_STATE_RENDER_TARGET
            );
            cmd->ResourceBarrier(1, &barrier);
            colorState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, nullptr);

        float clearColor[4] = { r, g, b, a };
        cmd->ClearRenderTargetView(rtvHandle_, clearColor, 0, nullptr);

        cmd->RSSetViewports(1, &viewport_);
        cmd->RSSetScissorRects(1, &scissorRect_);
    }

    void RenderTarget2D::Rebind()
    {
        if (!initialized_) return;

        ID3D12GraphicsCommandList* cmd = context_.cmdList;
        if (!cmd) { return; }

        if (colorState_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                colorTex_.Get(),
                colorState_,
                D3D12_RESOURCE_STATE_RENDER_TARGET
            );
            cmd->ResourceBarrier(1, &barrier);
            colorState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, nullptr);
        cmd->RSSetViewports(1, &viewport_);
        cmd->RSSetScissorRects(1, &scissorRect_);
    }

    void RenderTarget2D::EndCapture()
    {
        if (!initialized_) {
            return;
        }

        ID3D12GraphicsCommandList* cmd = context_.cmdList;
        if (!cmd) { return; }

        if (colorState_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                colorTex_.Get(),
                colorState_,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
            );
            cmd->ResourceBarrier(1, &barrier);
            colorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
    }

} // namespace HIKARI
