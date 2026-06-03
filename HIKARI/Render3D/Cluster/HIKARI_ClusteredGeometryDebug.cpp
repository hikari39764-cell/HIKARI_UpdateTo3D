#include "Render3D/Cluster/HIKARI_ClusteredGeometryDebug.h"

#include <array>
#include <algorithm>
#include <cmath>

#include "Render3D/Cluster/HIKARI_ClusteredGeometryAsset.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"

namespace HIKARI::RENDER3D::CLUSTER {

    namespace {
        MATH::Vec3 TransformPoint(const MATH::Mat4& matrix, const MATH::Vec3& point) {
            const MATH::Vec4 result = matrix.TransformPoint({ point.x, point.y, point.z, 1.0f });
            if (std::fabs(result.w) <= 1.0e-6f) {
                return { result.x, result.y, result.z };
            }
            const float invW = 1.0f / result.w;
            return { result.x * invW, result.y * invW, result.z * invW };
        }

        std::array<MATH::Vec3, 8> BuildCorners(const Bounds& bounds, const MATH::Mat4& matrix) {
            const MATH::Vec3 min = bounds.min;
            const MATH::Vec3 max = bounds.max;
            return {
                TransformPoint(matrix, { min.x, min.y, min.z }),
                TransformPoint(matrix, { max.x, min.y, min.z }),
                TransformPoint(matrix, { min.x, max.y, min.z }),
                TransformPoint(matrix, { max.x, max.y, min.z }),
                TransformPoint(matrix, { min.x, min.y, max.z }),
                TransformPoint(matrix, { max.x, min.y, max.z }),
                TransformPoint(matrix, { min.x, max.y, max.z }),
                TransformPoint(matrix, { max.x, max.y, max.z }),
            };
        }

        void SubmitLine(const MATH::Vec3& from, const MATH::Vec3& to, unsigned int color) {
            RENDERER3D::DEBUG::SubmitLine3D({
                from,
                to,
                color,
                RENDERER3D::DEBUG::DebugDepthMode::DepthTest
            });
        }

        void SubmitBounds(const Bounds& bounds, const MATH::Mat4& matrix, unsigned int color) {
            const std::array<MATH::Vec3, 8> p = BuildCorners(bounds, matrix);
            SubmitLine(p[0], p[1], color);
            SubmitLine(p[1], p[3], color);
            SubmitLine(p[3], p[2], color);
            SubmitLine(p[2], p[0], color);
            SubmitLine(p[4], p[5], color);
            SubmitLine(p[5], p[7], color);
            SubmitLine(p[7], p[6], color);
            SubmitLine(p[6], p[4], color);
            SubmitLine(p[0], p[4], color);
            SubmitLine(p[1], p[5], color);
            SubmitLine(p[3], p[7], color);
            SubmitLine(p[2], p[6], color);
        }

        MATH::Vec3 BoundsCenter(const Bounds& bounds) {
            return (bounds.min + bounds.max) * 0.5f;
        }

        void SubmitCross(const Bounds& bounds, const MATH::Mat4& matrix, unsigned int color) {
            const MATH::Vec3 center = BoundsCenter(bounds);
            const MATH::Vec3 size = bounds.max - bounds.min;
            const float radius = (std::max)(0.05f, (std::max)((std::max)(size.x, size.y), size.z) * 0.03f);
            SubmitLine(
                TransformPoint(matrix, { center.x - radius, center.y, center.z }),
                TransformPoint(matrix, { center.x + radius, center.y, center.z }),
                color);
            SubmitLine(
                TransformPoint(matrix, { center.x, center.y - radius, center.z }),
                TransformPoint(matrix, { center.x, center.y + radius, center.z }),
                color);
            SubmitLine(
                TransformPoint(matrix, { center.x, center.y, center.z - radius }),
                TransformPoint(matrix, { center.x, center.y, center.z + radius }),
                color);
        }
    }

    const char* ToString(ClusterDebugViewMode mode) {
        switch (mode) {
        case ClusterDebugViewMode::SelectedObjectSummary: return "Selected Object Summary";
        case ClusterDebugViewMode::SelectedSurfaceBounds: return "Selected Surface Bounds";
        case ClusterDebugViewMode::FirstNClusterBounds: return "First N Cluster Bounds";
        case ClusterDebugViewMode::ClusterPageBounds: return "Cluster Page Bounds";
        case ClusterDebugViewMode::Off:
        default:
            return "Off";
        }
    }

    void SubmitClusterDebugOverlay(
        const ClusteredGeometryAsset& asset,
        const Transform3D& transform,
        const ClusterDebugOptions& options) {

        if (!asset.valid || options.mode == ClusterDebugViewMode::Off) {
            return;
        }

        const MATH::Mat4 matrix = transform.GetWorldMatrix();
        if (options.mode == ClusterDebugViewMode::SelectedObjectSummary) {
            SubmitBounds(asset.localBounds, matrix, 0x61E6A8FF);
            SubmitCross(asset.localBounds, matrix, 0xFFFFFFFF);
            return;
        }

        if (options.mode == ClusterDebugViewMode::SelectedSurfaceBounds) {
            if (asset.surfaces.empty()) {
                return;
            }
            const uint32_t surfaceIndex =
                (std::min)(options.selectedSurfaceIndex, static_cast<uint32_t>(asset.surfaces.size() - 1u));
            SubmitBounds(asset.surfaces[surfaceIndex].localBounds, matrix, 0x4CBFFFFF);
            SubmitCross(asset.surfaces[surfaceIndex].localBounds, matrix, 0xFFFFFFFF);
            return;
        }

        if (options.mode == ClusterDebugViewMode::FirstNClusterBounds) {
            const uint32_t limit = (std::min)(
                options.firstClusterLimit,
                static_cast<uint32_t>(asset.clusters.size()));
            for (uint32_t i = 0; i < limit; ++i) {
                SubmitBounds(asset.clusters[i].localBounds, matrix, 0xFFCC4CFF);
            }
            return;
        }

        if (options.mode == ClusterDebugViewMode::ClusterPageBounds) {
            const uint32_t limit = (std::min)(
                options.pageLimit,
                static_cast<uint32_t>(asset.pages.size()));
            for (uint32_t i = 0; i < limit; ++i) {
                SubmitBounds(asset.pages[i].localBounds, matrix, 0xB36CFFFF);
            }
        }
    }

} // namespace HIKARI::RENDER3D::CLUSTER
