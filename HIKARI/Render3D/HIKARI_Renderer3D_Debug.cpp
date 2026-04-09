#include "HIKARI_Renderer3D_Debug.h"
#include <vector>
#include "Render2D/HIKARI_Renderer.h"

namespace HIKARI::RENDERER3D::DEBUG {

    namespace {
        std::vector<WireCube> g_cubes;
        std::vector<Line3D> g_lines;
        std::vector<Axis3D> g_axes;
        std::vector<Grid3D> g_grids;

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

        bool ProjectWorldPoint(const MATH::Mat4& vp, const MATH::Vec3& p, float screenW, float screenH, Vector2& out) {
            const MATH::Vec4 clip = vp.TransformPoint({ p.x, p.y, p.z, 1.0f });
            return ProjectToScreen(clip, screenW, screenH, out);
        }

        void DrawProjectedLine(const MATH::Mat4& vp, const MATH::Vec3& a, const MATH::Vec3& b, float screenW, float screenH, unsigned int rgba) {
            Vector2 sa{};
            Vector2 sb{};
            if (!ProjectWorldPoint(vp, a, screenW, screenH, sa) || !ProjectWorldPoint(vp, b, screenW, screenH, sb)) {
                return;
            }
            RENDERER::DrawLine(sa, sb, RENDERER::CameraMode::Ignore, rgba);
        }
    }

    void Reset() {
        g_cubes.clear();
        g_lines.clear();
        g_axes.clear();
        g_grids.clear();
    }

    void SubmitWireCube(const WireCube& cube) {
        g_cubes.push_back(cube);
    }

    void SubmitLine3D(const Line3D& line) {
        g_lines.push_back(line);
    }

    void SubmitAxis3D(const Axis3D& axis) {
        g_axes.push_back(axis);
    }

    void SubmitGrid3D(const Grid3D& grid) {
        g_grids.push_back(grid);
    }

    void RenderAll(const Camera3D& camera, float screenW, float screenH) {
        const MATH::Mat4 vp = camera.GetViewProj();

        for (const auto& line : g_lines) {
            DrawProjectedLine(vp, line.from, line.to, screenW, screenH, line.rgba);
        }

        for (const auto& axis : g_axes) {
            const MATH::Mat4 world = axis.transform.GetWorldMatrix();
            const MATH::Vec4 origin4 = world.TransformPoint({ 0.0f, 0.0f, 0.0f, 1.0f });
            const MATH::Vec4 x4 = world.TransformPoint({ axis.length, 0.0f, 0.0f, 1.0f });
            const MATH::Vec4 y4 = world.TransformPoint({ 0.0f, axis.length, 0.0f, 1.0f });
            const MATH::Vec4 z4 = world.TransformPoint({ 0.0f, 0.0f, axis.length, 1.0f });
            const MATH::Vec3 origin{ origin4.x, origin4.y, origin4.z };
            DrawProjectedLine(vp, origin, { x4.x, x4.y, x4.z }, screenW, screenH, axis.xColor);
            DrawProjectedLine(vp, origin, { y4.x, y4.y, y4.z }, screenW, screenH, axis.yColor);
            DrawProjectedLine(vp, origin, { z4.x, z4.y, z4.z }, screenW, screenH, axis.zColor);
        }

        for (const auto& grid : g_grids) {
            const int n = (grid.halfCount < 1) ? 1 : grid.halfCount;
            const float step = (grid.spacing <= 0.0f) ? 1.0f : grid.spacing;
            const float extent = static_cast<float>(n) * step;
            for (int i = -n; i <= n; ++i) {
                const float pos = static_cast<float>(i) * step;
                DrawProjectedLine(vp, { -extent, 0.0f, pos }, { extent, 0.0f, pos }, screenW, screenH, grid.rgba);
                DrawProjectedLine(vp, { pos, 0.0f, -extent }, { pos, 0.0f, extent }, screenW, screenH, grid.rgba);
            }
        }

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

} // namespace HIKARI::RENDERER3D::DEBUG
