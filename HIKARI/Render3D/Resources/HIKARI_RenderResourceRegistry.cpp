#include "Render3D/Resources/HIKARI_RenderResourceRegistry.h"

namespace HIKARI::RENDER3D {

    namespace {
        uint32_t AllocateId(uint32_t& nextId) {
            const uint32_t id = nextId++;
            if (nextId == 0) {
                nextId = 1;
            }
            return id;
        }
    }

    void RenderResourceRegistry::Clear() {
        textures_.clear();
        renderTargets_.clear();
        depthTargets_.clear();
        nextId_ = 1;
    }

    TextureHandle RenderResourceRegistry::RegisterTexture(ID3D12Resource* resource) {
        const uint32_t id = AllocateId(nextId_);
        textures_[id] = resource;
        return { id };
    }

    RenderTargetHandle RenderResourceRegistry::RegisterRenderTarget(ID3D12Resource* resource) {
        const uint32_t id = AllocateId(nextId_);
        renderTargets_[id] = resource;
        return { id };
    }

    DepthTargetHandle RenderResourceRegistry::RegisterDepthTarget(ID3D12Resource* resource) {
        const uint32_t id = AllocateId(nextId_);
        depthTargets_[id] = resource;
        return { id };
    }

    ID3D12Resource* RenderResourceRegistry::Get(TextureHandle handle) const {
        const auto found = textures_.find(handle.id);
        return found != textures_.end() ? found->second : nullptr;
    }

    ID3D12Resource* RenderResourceRegistry::Get(RenderTargetHandle handle) const {
        const auto found = renderTargets_.find(handle.id);
        return found != renderTargets_.end() ? found->second : nullptr;
    }

    ID3D12Resource* RenderResourceRegistry::Get(DepthTargetHandle handle) const {
        const auto found = depthTargets_.find(handle.id);
        return found != depthTargets_.end() ? found->second : nullptr;
    }

} // namespace HIKARI::RENDER3D
