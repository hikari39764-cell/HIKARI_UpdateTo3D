#include "Render3D/GpuDriven/HIKARI_GpuDrivenPassPlanner.h"

#include <array>

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        constexpr GpuDrivenPassDesc kFallbackDesc{
            GpuDrivenPassKind::Debug,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
        };

        constexpr std::array<GpuDrivenPassDesc, kGpuDrivenPassCount> kPassDescs{ {
            { GpuDrivenPassKind::ForwardOpaque, true, false, true, true, false, false, false, false },
            { GpuDrivenPassKind::DepthAware, true, false, false, false, false, false, true, false },
            { GpuDrivenPassKind::Transparent, true, false, false, false, true, false, true, true },
            { GpuDrivenPassKind::Shadow, false, true, false, false, false, false, false, false },
            { GpuDrivenPassKind::GeometryAux, true, false, true, true, false, false, false, false },
            { GpuDrivenPassKind::ReflectionCapture, true, false, true, true, false, false, false, false },
            { GpuDrivenPassKind::DepthPrepass, true, false, true, true, false, false, false, false },
            { GpuDrivenPassKind::Debug, false, false, false, false, false, false, false, false },
        } };
    }

    const GpuDrivenPassDesc& GetGpuDrivenPassDesc(GpuDrivenPassKind pass) {
        const size_t index = ToPassIndex(pass);
        if (index >= kPassDescs.size()) {
            return kFallbackDesc;
        }
        return kPassDescs[index];
    }

    GpuDrivenView BuildGpuDrivenView(
        GpuDrivenPassKind pass,
        const MATH::Mat4& viewProj,
        const MATH::Vec3& viewPosition,
        uint32_t flags) {

        GpuDrivenView view{};
        view.pass = pass;
        view.viewProj = viewProj;
        view.viewPosition = viewPosition;
        view.passMask = MakeGpuDrivenPassMask(pass);
        view.flags = flags;
        return view;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
