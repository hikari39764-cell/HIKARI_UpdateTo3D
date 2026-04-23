#include "HIKARI_RenderTarget2D.h"
#include <cassert>

using Microsoft::WRL::ComPtr;

namespace HIKARI {

    bool RenderTarget2D::Init(
        int width,
        int height,
        DXGI_FORMAT format,
        bool withDepth,
        const std::array<float, 4>& optimizedClearColor)
    {
        if (initialized_) {
            return true;
        }

        width_ = width;
        height_ = height;
        format_ = format;
        hasDepth_ = withDepth;
        optimizedClearColor_ = optimizedClearColor;

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
        depthTex_.Reset();
        rtvHeap_.Reset();
        srvHeap_.Reset();
        dsvHeap_.Reset();

        initialized_ = false;
        hasDepth_ = false;
    }

bool RenderTarget2D::CreateResources()
{
    ID3D12Device* device = context_.device;
    if (!device) { return false; }

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        format_,
        static_cast<UINT64>(width_),
        static_cast<UINT>(height_),
        1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
    );

    D3D12_CLEAR_VALUE colorClearValue{};
    colorClearValue.Format = format_;
    colorClearValue.Color[0] = optimizedClearColor_[0];
    colorClearValue.Color[1] = optimizedClearColor_[1];
    colorClearValue.Color[2] = optimizedClearColor_[2];
    colorClearValue.Color[3] = optimizedClearColor_[3];

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &colorClearValue,
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

    if (hasDepth_) {
        D3D12_CLEAR_VALUE depthClear{};
        depthClear.Format = DXGI_FORMAT_D32_FLOAT;
        depthClear.DepthStencil.Depth = 1.0f;
        depthClear.DepthStencil.Stencil = 0;

        CD3DX12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_D32_FLOAT,
            static_cast<UINT64>(width_),
            static_cast<UINT>(height_),
            1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
        );

        hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthClear,
            IID_PPV_ARGS(&depthTex_)
        );
        assert(SUCCEEDED(hr));
        depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

        D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
        dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvDesc.NumDescriptors = 1;
        dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_));
        assert(SUCCEEDED(hr));
        dsvHandle_ = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvView{};
        dsvView.Format = DXGI_FORMAT_D32_FLOAT;
        dsvView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvView.Flags = D3D12_DSV_FLAG_NONE;
        device->CreateDepthStencilView(depthTex_.Get(), &dsvView, dsvHandle_);
    }

    return true;
}
    
    void RenderTarget2D::BeginCapture(float r, float g, float b, float a, float depthClear)
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

        if (hasDepth_) {
            cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, &dsvHandle_);
        } else {
            cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, nullptr);
        }

        float clearColor[4] = { r, g, b, a };
        cmd->ClearRenderTargetView(rtvHandle_, clearColor, 0, nullptr);

        if (hasDepth_) {
            cmd->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, depthClear, 0, 0, nullptr);
        }

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

        if (hasDepth_) {
            cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, &dsvHandle_);
        } else {
            cmd->OMSetRenderTargets(1, &rtvHandle_, FALSE, nullptr);
        }
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
