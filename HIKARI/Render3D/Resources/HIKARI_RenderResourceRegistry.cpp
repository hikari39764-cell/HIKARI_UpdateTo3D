#include "Render3D/Resources/HIKARI_RenderResourceRegistry.h"

namespace HIKARI::RENDER3D {

    namespace {
		// 新しい ID を割り当てる。ID は 1 から始まり、0 は無効なハンドルを表すために予約される。ID がオーバーフローして 0 になった場合は、再び 1 に戻る。
        uint32_t AllocateId(uint32_t& nextId) {
            const uint32_t id = nextId++;
            if (nextId == 0) {
                nextId = 1;
            }
            return id;
        }
    }
	// すべてのリソースをクリアし、ID の管理をリセットする。これにより、すべてのハンドルが無効になる。
    void RenderResourceRegistry::Clear() {
        textures_.clear();
        renderTargets_.clear();
        depthTargets_.clear();
        nextId_ = 1;
    }
	// テクスチャを登録し、そのハンドルを返す。ID は内部で管理され、リソースと関連付けられる。
    TextureHandle RenderResourceRegistry::RegisterTexture(ID3D12Resource* resource) {
        const uint32_t id = AllocateId(nextId_);
        textures_[id] = resource;
        return { id };
    }
	// レンダーターゲットを登録し、そのハンドルを返す。ID は内部で管理され、リソースと関連付けられる。
    RenderTargetHandle RenderResourceRegistry::RegisterRenderTarget(ID3D12Resource* resource) {
        const uint32_t id = AllocateId(nextId_);
        renderTargets_[id] = resource;
        return { id };
    }
	// 深度ターゲットを登録し、そのハンドルを返す。ID は内部で管理され、リソースと関連付けられる。
    DepthTargetHandle RenderResourceRegistry::RegisterDepthTarget(ID3D12Resource* resource) {
        const uint32_t id = AllocateId(nextId_);
        depthTargets_[id] = resource;
        return { id };
    }
	// ハンドルからテクスチャリソースを取得する。ハンドルが無効な場合は nullptr を返す。
    ID3D12Resource* RenderResourceRegistry::Get(TextureHandle handle) const {
        const auto found = textures_.find(handle.id);
        return found != textures_.end() ? found->second : nullptr;
    }
	// ハンドルからレンダーターゲットリソースを取得する。ハンドルが無効な場合は nullptr を返す。
    ID3D12Resource* RenderResourceRegistry::Get(RenderTargetHandle handle) const {
        const auto found = renderTargets_.find(handle.id);
        return found != renderTargets_.end() ? found->second : nullptr;
    }
	// ハンドルから深度ターゲットリソースを取得する。ハンドルが無効な場合は nullptr を返す。
    ID3D12Resource* RenderResourceRegistry::Get(DepthTargetHandle handle) const {
        const auto found = depthTargets_.find(handle.id);
        return found != depthTargets_.end() ? found->second : nullptr;
    }

} // namespace HIKARI::RENDER3D
