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

        bool IsRecordCulledByCamera(
            const StaticDrawRecord& record,
            const StaticRecordSubmitOptions& options) {

            if (!options.enableFrustumCulling || !options.hasCameraViewProj) {
                return false;
            }

            // worldBounds は既にワールド空間なので、ViewProjection だけで判定する。
            return !BOUNDS::IntersectsClipFrustum(record.worldBounds, options.cameraViewProj);
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

            if (IsRecordCulledByCamera(record, options)) {
                ++outStats.culledRecordCount;
                continue;
            }

            if (options.useCachedStaticForward) {
                // cached record は primitive 単位で既存の MeshRenderer に渡す。
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
                ++outStats.submittedAfterCullCount;
            }

            if (options.useCachedStaticShadow) {
                ++outStats.skippedUnsupportedRecordCount;
            }
        }
    }

}
