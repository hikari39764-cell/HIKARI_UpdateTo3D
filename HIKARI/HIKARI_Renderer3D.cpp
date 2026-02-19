#include "HIKARI_Renderer3D.h"
#include <vector>

namespace HIKARI::RENDERER3D {

    namespace {
        std::vector<WireCube> g_cubes;

        bool ProjectToScreen(const MATH::Vec4& clip, float screenW, float screenH, Vector2& out) {
            if (clip.w <= 1e-5f) {
                return false;
            }
            const float ndcX = clip.x / clip.w;
            const float ndcY = clip.y / clip.w;
            out.x = (ndcX * 0.5f + 0.5f) * screenW;
            out.y = (1.0f - (ndcY * 0.5f + 0.5f)) * screenH;
            return true;
        }
    }

    void Reset() {
        g_cubes.clear();
    }

    void SubmitWireCube(const WireCube& cube) {
        g_cubes.push_back(cube);
    }

    void RenderAll(const Camera3D& camera, float screenW, float screenH) {
        const MATH::Mat4 vp = camera.GetViewProj();

        static const int kEdges[12][2] = {
            {0,1},{1,2},{2,3},{3,0},
            {4,5},{5,6},{6,7},{7,4},
            {0,4},{1,5},{2,6},{3,7}
        };

        for (const auto& c : g_cubes) {
            const float hs = c.size * 0.5f;
            const std::array<MATH::Vec4, 8> local = {{
                {-hs,-hs,-hs,1.0f}, {hs,-hs,-hs,1.0f}, {hs,hs,-hs,1.0f}, {-hs,hs,-hs,1.0f},
                {-hs,-hs,hs,1.0f},  {hs,-hs,hs,1.0f},  {hs,hs,hs,1.0f},  {-hs,hs,hs,1.0f}
            }};

            const MATH::Mat4 mvp = vp * c.transform.GetWorldMatrix();
            std::array<Vector2, 8> screen{};
            std::array<bool, 8> valid{};

            for (int i = 0; i < 8; ++i) {
                const MATH::Vec4 clip = mvp.TransformPoint(local[i]);
                valid[i] = ProjectToScreen(clip, screenW, screenH, screen[i]);
            }

            for (const auto& e : kEdges) {
                if (!valid[e[0]] || !valid[e[1]]) {
                    continue;
                }
                RENDERER::DrawLine(screen[e[0]], screen[e[1]], RENDERER::CameraMode::Ignore, c.rgba);
            }
        }
    }

} // namespace HIKARI::RENDERER3D
