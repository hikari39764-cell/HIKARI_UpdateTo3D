#pragma once
#include <functional>
#include <vector>
#include <cstdint>

#include "HIKARI_Transform2D.h"
#include "HIKARI_Camera2_5D.h"

namespace HIKARI {
namespace RENDERQUEUE25 {

    struct Item {
        Transform2D world{};
        float z = 0.0f;

        float baseYOffset = 0.0f;
        int layer = 0;
        float bias = 0.0f;

        float sortKey = 0.0f;

        std::function<void(const Transform2D& drawT, float z)> draw;
    };

    struct Config {
        float layerStride = 1000000.0f;
        float zBias = 48.0f;
        bool useProjectedYForSort = false;
    };

    class WorldDrawQueue25 {
    public:
        void SetConfig(const Config& cfg);

        void BeginFrame();
        void Submit(Item item);
        void Flush();

        void SubmitBox(const Transform2D& world, float z, float w, float h, uint32_t rgba,
                       float baseYOffset = 0.0f, int layer = 0, float bias = 0.0f);

        void SubmitEllipse(const Transform2D& world, float z, float rx, float ry, uint32_t rgba,
                           float baseYOffset = 0.0f, int layer = 0, float bias = 0.0f);

    private:
        float ComputeSortKey_(const Item& it) const;

        Config cfg_{};
        std::vector<Item> items_{};
    };

} // namespace RENDERQUEUE25
} // namespace HIKARI
