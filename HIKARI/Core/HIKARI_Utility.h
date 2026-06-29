#pragma once
#include <cstdint>

static inline uint32_t PackRGBA(int r, int g, int b, int a) {
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    if (a < 0) a = 0; if (a > 255) a = 255;
    return (static_cast<uint32_t>(r) << 24) |
        (static_cast<uint32_t>(g) << 16) |
        (static_cast<uint32_t>(b) << 8) |
        static_cast<uint32_t>(a);
}

// legacy fixed-step default / fallback value.
// not the runtime frame delta source.
static const float kDt = 1.0f / 60.0f;

struct Vector2 { float x, y; };

const int kScreenW = 1920;
const int kScreenH = 1080;
