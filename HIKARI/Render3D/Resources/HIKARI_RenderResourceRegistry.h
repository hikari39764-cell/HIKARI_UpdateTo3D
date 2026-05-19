#pragma once

#include <cstdint>
#include <unordered_map>

#include <d3d12.h>

#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::RENDER3D {

    class RenderResourceRegistry {
    public:
        void Clear();

        TextureHandle RegisterTexture(ID3D12Resource* resource);
        RenderTargetHandle RegisterRenderTarget(ID3D12Resource* resource);
        DepthTargetHandle RegisterDepthTarget(ID3D12Resource* resource);

        ID3D12Resource* Get(TextureHandle handle) const;
        ID3D12Resource* Get(RenderTargetHandle handle) const;
        ID3D12Resource* Get(DepthTargetHandle handle) const;

    private:
        uint32_t nextId_ = 1;
        std::unordered_map<uint32_t, ID3D12Resource*> textures_;
        std::unordered_map<uint32_t, ID3D12Resource*> renderTargets_;
        std::unordered_map<uint32_t, ID3D12Resource*> depthTargets_;
    };

} // namespace HIKARI::RENDER3D
