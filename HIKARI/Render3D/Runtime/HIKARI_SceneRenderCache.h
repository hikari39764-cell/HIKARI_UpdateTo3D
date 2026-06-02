#pragma once

#include <cstdint>

namespace HIKARI::RENDER3D::RUNTIME {

    class SceneRenderCache {
    public:
        struct Stats {
            uint32_t renderObjectCount = 0;
            uint32_t dirtyObjectCount = 0;
            uint32_t staticObjectCount = 0;
            uint32_t dynamicObjectCount = 0;
        };

        void Clear();
        void OnSceneLoaded();
        void OnSceneUnloaded();
        void MarkAllDirty();
        void PreRenderSync();

        const Stats& GetStats() const;

    private:
        Stats stats_{};
    };

}
