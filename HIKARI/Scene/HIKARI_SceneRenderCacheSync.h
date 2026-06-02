#pragma once

#include <cstdint>

namespace HIKARI {

    class World;

    namespace RENDER3D::RUNTIME {
        class RenderModelCache;
        class SceneRenderCache;
    }

    class SceneRenderCacheSync {
    public:
        void Sync(
            World& world,
            RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
            RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
            uint64_t frameIndex);
    };

}
