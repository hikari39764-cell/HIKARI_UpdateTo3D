#pragma once

#include <vector>

#include "Render3D/Pipeline/HIKARI_RenderPhase.h"

namespace HIKARI::MESHRENDERER {
    struct DrawItem;
}

namespace HIKARI::RENDER3D {

    class RenderQueue {
    public:
        void Clear();
        void Build(const std::vector<MESHRENDERER::DrawItem>& items);

        const std::vector<const MESHRENDERER::DrawItem*>& GetPhase(RenderPhase phase) const;
        bool HasPhase(RenderPhase phase) const;

    private:
        std::vector<const MESHRENDERER::DrawItem*> opaque_;
        std::vector<const MESHRENDERER::DrawItem*> depthAware_;
        std::vector<const MESHRENDERER::DrawItem*> transparent_;
    };

} // namespace HIKARI::RENDER3D
