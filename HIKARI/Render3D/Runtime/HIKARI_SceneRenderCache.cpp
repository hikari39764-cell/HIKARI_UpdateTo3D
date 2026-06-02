#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"

namespace HIKARI::RENDER3D::RUNTIME {

    void SceneRenderCache::Clear() {
        stats_ = {};
    }

    void SceneRenderCache::OnSceneLoaded() {
        Clear();
    }

    void SceneRenderCache::OnSceneUnloaded() {
        Clear();
    }

    void SceneRenderCache::MarkAllDirty() {
        stats_.dirtyObjectCount = stats_.renderObjectCount;
    }

    void SceneRenderCache::PreRenderSync() {
        // R0.2 で scene instance を render cache に同期する。
    }

    const SceneRenderCache::Stats& SceneRenderCache::GetStats() const {
        return stats_;
    }

}
