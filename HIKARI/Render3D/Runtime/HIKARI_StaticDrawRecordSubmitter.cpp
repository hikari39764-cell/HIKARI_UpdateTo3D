#include "Render3D/Runtime/HIKARI_StaticDrawRecordSubmitter.h"

#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"

namespace HIKARI::RENDER3D::RUNTIME {

    namespace {
        bool HasValidPrimitiveTarget(const StaticDrawRecord& record) {
            if (!record.objectId.IsValid() ||
                record.model == nullptr ||
                record.renderModel == nullptr ||
                !record.renderModel->valid ||
                !record.hasDrawWorldMatrix ||
                !BOUNDS::IsUsable(record.worldBounds)) {
                return false;
            }
            if (record.meshIndex >= record.model->meshes.size()) {
                return false;
            }
            const MeshAsset& mesh = record.model->meshes[record.meshIndex];
            return record.primitiveIndex < mesh.primitives.size();
        }
    }

    void StaticDrawRecordSubmitter::Submit(
        const StaticDrawRecordCache& cache,
        const StaticRecordSubmitOptions& options,
        StaticDrawRecordSubmitStats& outStats) {

        outStats = {};
        if (!options.useCachedStaticForward && !options.useCachedStaticShadow) {
            return;
        }

        for (const StaticDrawRecord& record : cache.GetRecords()) {
            if (!HasValidPrimitiveTarget(record)) {
                ++outStats.skippedInvalidRecordCount;
                continue;
            }

            if (options.useCachedStaticForward) {
                // cached record は primitive 単位で現在の MeshRenderer に渡す。
                MESHRENDERER::SubmitStaticSubmesh(
                    *record.model,
                    record.drawTransform,
                    record.meshIndex,
                    record.primitiveIndex,
                    record.materialFxProfileId,
                    record.postGroupMask,
                    record.materialFxParamValues,
                    record.materialFxValuesInitialized,
                    record.receiveShadow,
                    MESHRENDERER::MeshRenderDebugMode::Normal,
                    record.materialOverride);
                ++outStats.submittedRecordCount;
            }

            if (options.useCachedStaticShadow) {
                ++outStats.skippedUnsupportedRecordCount;
            }
        }
    }

}
