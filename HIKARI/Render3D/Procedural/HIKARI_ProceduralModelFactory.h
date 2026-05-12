#pragma once

#include <cstddef>
#include <cstdint>

#include "Scene/Components/HIKARI_ModelComponent.h"

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::PROCEDURAL {

    struct ProceduralModelDebugStats {
        size_t cacheHitCount = 0;
        size_t cacheMissCount = 0;
        size_t generatedModelCount = 0;
        size_t generatedVertexCount = 0;
        size_t generatedIndexCount = 0;
    };

    const ModelAsset* GetOrCreateModel(const ProceduralModelSettings& settings);
    void ClearCache();
    const ProceduralModelDebugStats& GetDebugStats();

} // namespace HIKARI::PROCEDURAL
