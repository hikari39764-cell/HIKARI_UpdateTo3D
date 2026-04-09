#include "HIKARI_RenderQueue25.h"

#include <algorithm>

#include "HIKARI_Renderer.h"

namespace HIKARI {
namespace RENDERQUEUE25 {

    static uint32_t PackRGBA(uint32_t rgb, uint32_t a) {
        return (rgb & 0x00FFFFFFu) | ((a & 0xFFu) << 24);
    }

    static uint32_t DepthColor(uint32_t rgba, float depth01) {
        if (depth01 < 0.0f) depth01 = 0.0f;
        if (depth01 > 1.0f) depth01 = 1.0f;
        uint32_t a = (uint32_t)(depth01 * 255.0f + 0.5f);
        return PackRGBA(rgba, a);
    }

    void WorldDrawQueue25::SetConfig(const Config& cfg) { cfg_ = cfg; }

    void WorldDrawQueue25::BeginFrame() { items_.clear(); }

    float WorldDrawQueue25::ComputeSortKey_(const Item& it) const
    {

        HIKARI::CAMERA25::Projection pr = HIKARI::CAMERA25::Project(it.world.position, it.z);

        float feetY = pr.world2D.y + it.baseYOffset * pr.scale;

        float depthOrder = -it.z * cfg_.zBias;

        return (float)it.layer * cfg_.layerStride + feetY + depthOrder + it.bias;
    }

    void WorldDrawQueue25::Submit(Item item)
    {
        item.sortKey = ComputeSortKey_(item);
        items_.push_back(std::move(item));
    }

    void WorldDrawQueue25::Flush()
    {
        std::sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) {
            return a.sortKey < b.sortKey;
        });

        for (const Item& it : items_) {
            Transform2D drawT = CAMERA25::ApplyToTransform(it.world, it.z);
            if (it.draw) {
                it.draw(drawT, it.z);
            }
        }
    }

    void WorldDrawQueue25::SubmitBox(const Transform2D& world, float z, float w, float h, uint32_t rgba,
                                     float baseYOffset, int layer, float bias)
    {
        Item it{};
        it.world = world;
        it.z = z;
        it.baseYOffset = baseYOffset;
        it.layer = layer;
        it.bias = bias;

        it.draw = [=](const Transform2D& inDrawT, float zIn) {
            Transform2D drawT = inDrawT;
            drawT.pivotPx = { w * 0.5f, h * 0.5f };
            float d01 = CAMERA25::Depth01(zIn);
            uint32_t col = DepthColor(rgba, d01);
            RENDERER::DrawBox(drawT, w, h, RENDERER::FillMode::Fill, RENDERER::CameraMode::Inherit, col);
        };

        Submit(std::move(it));
    }

    void WorldDrawQueue25::SubmitEllipse(const Transform2D& world, float z, float rx, float ry, uint32_t rgba,
                                         float baseYOffset, int layer, float bias)
    {
        Item it{};
        it.world = world;
        it.z = z;
        it.baseYOffset = baseYOffset;
        it.layer = layer;
        it.bias = bias;

        it.draw = [=](const Transform2D& inDrawT, float zIn) {
            Transform2D drawT = inDrawT;
            drawT.pivotPx = { rx, ry };
            float d01 = CAMERA25::Depth01(zIn);
            uint32_t col = DepthColor(rgba, d01);
            RENDERER::DrawEllipse(drawT, rx, ry, RENDERER::FillMode::Fill, RENDERER::CameraMode::Inherit, col);
        };

        Submit(std::move(it));
    }

} // namespace RENDERQUEUE25
} // namespace HIKARI
