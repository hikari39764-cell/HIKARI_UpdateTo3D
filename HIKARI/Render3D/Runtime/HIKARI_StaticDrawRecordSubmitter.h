#pragma once

#include <cstdint>

#include "Render3D/Runtime/HIKARI_StaticDrawRecordCache.h"

namespace HIKARI::RENDER3D::RUNTIME {

    struct StaticRecordSubmitOptions {
        bool useCachedStaticForward = false;
        bool skipOldStaticForwardSubmit = false;

        bool useCachedStaticShadow = false;
        bool skipOldStaticShadowSubmit = false;
    };

    struct StaticDrawRecordSubmitStats {
        uint32_t submittedRecordCount = 0;
        uint32_t skippedInvalidRecordCount = 0;
        uint32_t skippedUnsupportedRecordCount = 0;
        uint32_t submittedShadowRecordCount = 0;
    };

    class StaticDrawRecordSubmitter {
    public:
        void Submit(
            const StaticDrawRecordCache& cache,
            const StaticRecordSubmitOptions& options,
            StaticDrawRecordSubmitStats& outStats);
    };

}
