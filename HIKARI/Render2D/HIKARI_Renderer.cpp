#include "HIKARI_Renderer.h"
#include <cmath>
#include "HIKARI_DxRenderer.h"
#include "HIKARI_DxTexture.h"
#include "HIKARI_Camera2_5D.h"
#include "Windows.h"
#include <algorithm>
#undef max
#undef min
// ===== 内部ツール =====
namespace {

    using namespace HIKARI;
    using namespace HIKARI::RENDERER;

    static unsigned int gDefaultColor = 0xFFFFFFFF; // RGBA
    static Vector2 gOcclusionScreenPos = { 0.0f, 0.0f };
    static float gOcclusionRadius = 0.0f;

    static int gWhiteTexHandle = -1;
    static const char* kDefaultWhiteTexPath = "./HIKARI/white1x1.png";
    static std::string gWhiteTexPath = kDefaultWhiteTexPath;

    enum class CommandType {
        Sprite,
        Line,
        Box,
        Triangle,
        Ellipse,
        MeshQuad,
        DeformGrid
    };

    struct RenderCommand {

        RenderLayer layer;
        float sortY;
        int orderIndex;
        bool enableYSort = true;

        CommandType type;
        CameraMode camMode;
        unsigned int rgba;
        HIKARI::RENDERER::FillMode fillMode;

        int dxHandle;

        HIKARI::Transform2D transform;
        float width, height;

        SpriteUV uv;

        float u_lt, v_lt, u_rt, v_rt, u_lb, v_lb, u_rb, v_rb;

        Vector2 p0, p1, p2, p3;

        float radiusX, radiusY;

        DeformGrid gridData;
        bool useMaskShader = false;
    };

    static std::vector<RenderCommand> gRenderList;
    static RenderLayer gCurrentLayer = RenderLayer::Entity;
    static int gSubmissionCount = 0;

    static inline int EnsureWhiteTexture() {
        if (gWhiteTexHandle < 0) {
            // 用 DX 纹理管理器载入 1x1 白贴图
            gWhiteTexHandle = HIKARI::DXTEX::DxTextureManager::LoadTexture(
                "Renderer_White1x1",
                gWhiteTexPath
            );
        }
        return gWhiteTexHandle;
    }


    static inline Vector2 TransformPoint(const Vector2& p, const Matrix3x3& m) {
        Vector2 o{};
        o.x = p.x * m.m[0][0] + p.y * m.m[1][0] + m.m[2][0];
        o.y = p.x * m.m[0][1] + p.y * m.m[1][1] + m.m[2][1];
        return o;
    }

    static inline Matrix3x3 ComposeWorldThenMaybeView(const Matrix3x3& world, CameraMode cam) {
        if (cam == CameraMode::Ignore) return world;
        const Matrix3x3& view = HIKARI::CAMERA::GetViewMatrix();
        return world * view;
    }

    static void ExecuteDrawCommand(const RenderCommand& cmd)
    {
        Matrix3x3 world = Matrix3x3::MakeIdentity();

        // [修复]：移除了 MeshQuad 的跳过逻辑
        // 以前：if (cmd.type != CommandType::Line && cmd.type != CommandType::MeshQuad ...
        // 现在：必须允许 MeshQuad 计算 World 矩阵，否则 Spine 无法跟随 Transform 移动
        if (cmd.type != CommandType::Line && cmd.type != CommandType::DeformGrid) {
            world = cmd.transform.ToWorld(cmd.width, cmd.height);
        }

        Matrix3x3 m = ComposeWorldThenMaybeView(world, cmd.camMode);

        switch (cmd.type)
        {
        case CommandType::Sprite:
        {
            if (cmd.dxHandle < 0) return;
            Vector2 pLT{ 0.0f, 0.0f };
            Vector2 pRT{ cmd.width, 0.0f };
            Vector2 pLB{ 0.0f, cmd.height };
            Vector2 pRB{ cmd.width, cmd.height };

            Vector2 lt = TransformPoint(pLT, m);
            Vector2 rt = TransformPoint(pRT, m);
            Vector2 lb = TransformPoint(pLB, m);
            Vector2 rb = TransformPoint(pRB, m);

            if (cmd.useMaskShader) {
                HIKARI::DX::DxRenderer::DrawMeshQuadMasked(
                    lt.x, lt.y, cmd.uv.u0, cmd.uv.v0,
                    rt.x, rt.y, cmd.uv.u1, cmd.uv.v0,
                    lb.x, lb.y, cmd.uv.u0, cmd.uv.v1,
                    rb.x, rb.y, cmd.uv.u1, cmd.uv.v1,
                    cmd.dxHandle, cmd.rgba
                );
            } else {
                HIKARI::DX::DxRenderer::DrawMeshQuad(
                    lt.x, lt.y, cmd.uv.u0, cmd.uv.v0,
                    rt.x, rt.y, cmd.uv.u1, cmd.uv.v0,
                    lb.x, lb.y, cmd.uv.u0, cmd.uv.v1,
                    rb.x, rb.y, cmd.uv.u1, cmd.uv.v1,
                    cmd.dxHandle, cmd.rgba
                );
            }


            break;
        }
        case CommandType::Line:
        {

            Vector2 a = TransformPoint(cmd.p0, m);
            Vector2 b = TransformPoint(cmd.p1, m);
            HIKARI::DX::DxRenderer::DrawLine((float)a.x, (float)a.y, (float)b.x, (float)b.y, cmd.rgba);
            break;
        }
        case CommandType::Box:
        {
            // Wireframe Box: 画4条线
            // Fill Box: 之前我们在 Push 阶段偷懒转成了 Sprite，所以这里只处理 Wireframe
            if (cmd.fillMode == HIKARI::RENDERER::FillMode::Wireframe) {
                Vector2 pLT{ 0.0f, 0.0f };
                Vector2 pRT{ cmd.width, 0.0f };
                Vector2 pLB{ 0.0f, cmd.height };
                Vector2 pRB{ cmd.width, cmd.height };

                Vector2 lt = TransformPoint(pLT, m);
                Vector2 rt = TransformPoint(pRT, m);
                Vector2 lb = TransformPoint(pLB, m);
                Vector2 rb = TransformPoint(pRB, m);

                HIKARI::DX::DxRenderer::DrawLine(lt.x, lt.y, rt.x, rt.y, cmd.rgba);
                HIKARI::DX::DxRenderer::DrawLine(rt.x, rt.y, rb.x, rb.y, cmd.rgba);
                HIKARI::DX::DxRenderer::DrawLine(rb.x, rb.y, lb.x, lb.y, cmd.rgba);
                HIKARI::DX::DxRenderer::DrawLine(lb.x, lb.y, lt.x, lt.y, cmd.rgba);
            }
            break;
        }
        case CommandType::Triangle:
        {
            // p0, p1, p2 是相对于 Transform 的局部坐标
            // 原接口是 DrawTriangle(t, p0, p1, p2)
            Vector2 a = TransformPoint(cmd.p0, m);
            Vector2 b = TransformPoint(cmd.p1, m);
            Vector2 c = TransformPoint(cmd.p2, m);

            if (cmd.fillMode == HIKARI::RENDERER::FillMode::Wireframe) {
                HIKARI::DX::DxRenderer::DrawLine(a.x, a.y, b.x, b.y, cmd.rgba);
                HIKARI::DX::DxRenderer::DrawLine(b.x, b.y, c.x, c.y, cmd.rgba);
                HIKARI::DX::DxRenderer::DrawLine(c.x, c.y, a.x, a.y, cmd.rgba);
            } else {
                // Fill Triangle: 使用白图画 Mesh
                if (cmd.dxHandle >= 0) {
                    // 利用简并四边形画三角形 (v3 = v2)
                    HIKARI::DX::DxRenderer::DrawMeshQuad(
                        a.x, a.y, 0, 0,
                        b.x, b.y, 1, 0,
                        c.x, c.y, 0, 1,
                        c.x, c.y, 1, 1, // 重复点
                        cmd.dxHandle, cmd.rgba
                    );
                }
            }
            break;
        }
        case CommandType::Ellipse:
        {
            const int kSegments = 32; // 稍微降低一点段数以提高性能
            float cx = cmd.width * 0.5f;
            float cy = cmd.height * 0.5f;
            float rx = cmd.radiusX;
            float ry = cmd.radiusY;

            std::vector<Vector2> pts;
            pts.reserve(kSegments + 1);

            for (int i = 0; i <= kSegments; ++i) {
                float theta = (float)i / (float)kSegments * 6.2831853f;
                Vector2 pLocal{ cx + rx * std::cos(theta), cy + ry * std::sin(theta) };
                pts.push_back(TransformPoint(pLocal, m));
            }

            if (cmd.fillMode == HIKARI::RENDERER::FillMode::Wireframe) {
                for (int i = 0; i < kSegments; ++i) {
                    HIKARI::DX::DxRenderer::DrawLine(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, cmd.rgba);
                }
            } else {
                if (cmd.dxHandle >= 0) {
                    Vector2 centerWorld = TransformPoint({ cx, cy }, m);
                    // Fill: Triangle Fan
                    for (int i = 0; i < kSegments; ++i) {
                        HIKARI::DX::DxRenderer::DrawMeshQuad(
                            centerWorld.x, centerWorld.y, 0.5f, 0.5f,
                            pts[i].x, pts[i].y, 1, 0,
                            pts[i + 1].x, pts[i + 1].y, 0, 1,
                            pts[i + 1].x, pts[i + 1].y, 0, 1,
                            cmd.dxHandle, cmd.rgba
                        );
                    }
                }
            }
            break;
        }
        case CommandType::MeshQuad:
        {
            if (cmd.dxHandle < 0) return;

            // 这里使用计算好的 m 矩阵 (World * View) 进行变换
            // DrawMeshQuadHandleUV_Local 传入了 transform，所以 m 包含了该 transform
            // DrawMeshQuadHandleUV_Vertices 传入了 Identity，所以 m 仅包含 View
            Vector2 lt = TransformPoint(cmd.p0, m); // p0=lt
            Vector2 rt = TransformPoint(cmd.p1, m); // p1=rt
            Vector2 lb = TransformPoint(cmd.p2, m); // p2=lb
            Vector2 rb = TransformPoint(cmd.p3, m); // p3=rb

            HIKARI::DX::DxRenderer::DrawMeshQuad(
                lt.x, lt.y, cmd.u_lt, cmd.v_lt,
                rt.x, rt.y, cmd.u_rt, cmd.v_rt,
                lb.x, lb.y, cmd.u_lb, cmd.v_lb,
                rb.x, rb.y, cmd.u_rb, cmd.v_rb,
                cmd.dxHandle,
                cmd.rgba
            );
            break;
        }

        case CommandType::DeformGrid:
        {
            if (cmd.dxHandle < 0) return;
            const auto& grid = cmd.gridData;
            if (grid.cols < 2 || grid.rows < 2) return;

            int vertexCount = grid.cols * grid.rows;
            if (grid.positions.size() != vertexCount) return;

            static std::vector<Vector2> tempTransformed; // 避免反复分配内存
            if (tempTransformed.size() < vertexCount) tempTransformed.resize(vertexCount);

            for (int i = 0; i < vertexCount; ++i) {
                tempTransformed[i] = TransformPoint(grid.positions[i], m);
            }

            bool hasUV = (grid.uvs.size() == vertexCount);

            for (int y = 0; y < grid.rows - 1; ++y) {
                for (int x = 0; x < grid.cols - 1; ++x) {
                    int i0 = y * grid.cols + x;
                    int i1 = y * grid.cols + (x + 1);
                    int i2 = (y + 1) * grid.cols + x;
                    int i3 = (y + 1) * grid.cols + (x + 1);

                    HIKARI::DX::MeshVertex verts[4]{};
                    verts[0].px = tempTransformed[i0].x; verts[0].py = tempTransformed[i0].y;
                    verts[1].px = tempTransformed[i1].x; verts[1].py = tempTransformed[i1].y;
                    verts[2].px = tempTransformed[i2].x; verts[2].py = tempTransformed[i2].y;
                    verts[3].px = tempTransformed[i3].x; verts[3].py = tempTransformed[i3].y;

                    if (hasUV) {
                        verts[0].u = grid.uvs[i0].x; verts[0].v = grid.uvs[i0].y;
                        verts[1].u = grid.uvs[i1].x; verts[1].v = grid.uvs[i1].y;
                        verts[2].u = grid.uvs[i2].x; verts[2].v = grid.uvs[i2].y;
                        verts[3].u = grid.uvs[i3].x; verts[3].v = grid.uvs[i3].y;
                    } else {
                        // fallback UV
                        float u0 = (float)x / (grid.cols - 1); float v0 = (float)y / (grid.rows - 1);
                        float u1 = (float)(x + 1) / (grid.cols - 1); float v1 = (float)(y + 1) / (grid.rows - 1);
                        verts[0].u = u0; verts[0].v = v0; verts[1].u = u1; verts[1].v = v0;
                        verts[2].u = u0; verts[2].v = v1; verts[3].u = u1; verts[3].v = v1;
                    }

                    HIKARI::DX::DxRenderer::DrawMesh(verts, 4, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, cmd.dxHandle, cmd.rgba);
                }
            }
            break;
        }
        }
    }



} // anonymous

namespace HIKARI {
    namespace RENDERER {


        void SetCurrentLayer(RenderLayer layer) { gCurrentLayer = layer; }
        RenderLayer GetCurrentLayer() { return gCurrentLayer; }

        void BeginFrame() {
            gRenderList.clear();
            if (gRenderList.capacity() < 1024) {
                gRenderList.reserve(1024);
            }
            gSubmissionCount = 0;
            gCurrentLayer = RenderLayer::Entity;
        }


        void RenderAll() {
            RenderLayerRange(RenderLayer::Background, RenderLayer::Debug, true);
        }

        void RenderLayerRange(RenderLayer minLayer, RenderLayer maxLayer, bool clearAfter) {
            if (gRenderList.empty()) return;

            std::sort(gRenderList.begin(), gRenderList.end(),
                [](const RenderCommand& a, const RenderCommand& b) {
                    if (a.layer != b.layer) return (int)a.layer < (int)b.layer;
                    const bool useY = a.enableYSort && b.enableYSort;
                    if (useY && std::abs(a.sortY - b.sortY) > 0.1f) {
                        return a.sortY < b.sortY;
                    }
                    return a.orderIndex < b.orderIndex;
                });

            for (const auto& cmd : gRenderList) {
                if ((int)cmd.layer < (int)minLayer || (int)cmd.layer > (int)maxLayer) {
                    continue;
                }
                ExecuteDrawCommand(cmd);
            }

            if (clearAfter) {
                gRenderList.clear();
                gSubmissionCount = 0;
            }
        }

        void ClearSubmittedCommands() {
            gRenderList.clear();
            gSubmissionCount = 0;
        }

        void SetDefaultColor(unsigned int rgba) { gDefaultColor = rgba; }
        unsigned int GetDefaultColor() { return gDefaultColor; }
        void ReserveRenderCommands(size_t count) { gRenderList.reserve(count); }

        void SetBlendMode(BlendMode mode)
        {
            HIKARI::DX::DxRenderer::SetBlendMode(static_cast<HIKARI::DX::BlendMode>(mode));
        }

        void SetWhiteTexturePath(const char* pathRGBA1x1) {
            if (pathRGBA1x1 && pathRGBA1x1[0] != '\0') {
                gWhiteTexPath = pathRGBA1x1;
                gWhiteTexHandle = -1; // 次回利用時に再ロードさせる
            }
        }

        void SetOcclusionSource(Vector2 screenPos, float radius) {
            gOcclusionScreenPos = screenPos;
            gOcclusionRadius = radius;
            // 同时更新底层
            HIKARI::DX::DxRenderer::UpdateMaskParams(screenPos.x, screenPos.y, radius, 40.0f);
        }

        // ---- 基本的な線描画 ----
        void DrawLine(Vector2 p0, Vector2 p1, CameraMode cam, unsigned int rgba) {
            RenderCommand cmd;
            cmd.type = CommandType::Line;
            cmd.layer = gCurrentLayer;
            cmd.sortY = (p0.y + p1.y) * 0.5f;
            cmd.orderIndex = gSubmissionCount++;

            cmd.p0 = p0;
            cmd.p1 = p1;
            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        // ---- Quad----
        void DrawQuad(const HIKARI::Transform2D& t,
            float width, float height,
            CameraMode cam,
            unsigned int rgba,
            bool billboard) {
            DrawBox(t, width, height, FillMode::Fill, cam, rgba, billboard);
        }

        // ---- Box ----
        void DrawBox(const HIKARI::Transform2D& t, float width, float height, FillMode mode, CameraMode cam, unsigned int rgba, bool billboard) {
            unsigned int c = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            HIKARI::Transform2D tt = t;
            if (billboard && cam == CameraMode::Inherit) {
                tt.rotation -= HIKARI::CAMERA::GetRotation();
                tt.scale.y *= HIKARI::CAMERA::GetBillboardScaleY();
            }

            if (mode == FillMode::Fill) {
                int h = EnsureWhiteTexture();
                if (h >= 0) {
                    RenderCommand cmd;
                    cmd.type = CommandType::Sprite;
                    cmd.layer = gCurrentLayer;
                    cmd.sortY = t.position.y;
                    cmd.orderIndex = gSubmissionCount++;
                    cmd.dxHandle = h;
                    cmd.transform = tt;
                    cmd.width = width; cmd.height = height;
                    cmd.uv = { 0,0,1,1 };
                    cmd.camMode = cam;
                    cmd.rgba = c;
                    gRenderList.push_back(cmd);
                    return;
                }
                mode = FillMode::Wireframe;
            }

            // Wireframe
            RenderCommand cmd;
            cmd.type = CommandType::Box;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;
            cmd.transform = tt;
            cmd.width = width;
            cmd.height = height;
            cmd.fillMode = FillMode::Wireframe;
            cmd.camMode = cam;
            cmd.rgba = c;
            gRenderList.push_back(cmd);
        }

        void DrawTriangle(const HIKARI::Transform2D& t, Vector2 p0, Vector2 p1, Vector2 p2, FillMode mode, CameraMode cam, unsigned int rgba, bool billboard)
        {
            HIKARI::Transform2D tt = t;
            if (billboard && cam == CameraMode::Inherit) {
                tt.rotation -= HIKARI::CAMERA::GetRotation();
                tt.scale.y *= HIKARI::CAMERA::GetBillboardScaleY();
            }

            RenderCommand cmd;
            cmd.type = CommandType::Triangle;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.transform = tt;
            cmd.width = 1.0f; cmd.height = 1.0f;

            cmd.p0 = p0; cmd.p1 = p1; cmd.p2 = p2;
            cmd.fillMode = mode;
            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            if (mode == FillMode::Fill) {
                cmd.dxHandle = EnsureWhiteTexture();
            }
            gRenderList.push_back(cmd);
        }


        void DrawEllipse(const HIKARI::Transform2D& t, float radiusX, float radiusY, FillMode mode, CameraMode cam, unsigned int rgba, bool billboard)
        {
            HIKARI::Transform2D tt = t;
            if (billboard && cam == CameraMode::Inherit) {
                tt.rotation -= HIKARI::CAMERA::GetRotation();
                tt.scale.y *= HIKARI::CAMERA::GetBillboardScaleY();
            }

            RenderCommand cmd;
            cmd.type = CommandType::Ellipse;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.transform = tt;
            cmd.width = radiusX * 2.0f;
            cmd.height = radiusY * 2.0f;
            cmd.radiusX = radiusX;
            cmd.radiusY = radiusY;
            cmd.fillMode = mode;
            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            if (mode == FillMode::Fill) {
                cmd.dxHandle = EnsureWhiteTexture();
            }
            gRenderList.push_back(cmd);
        }

        // ---- スプライト：名前指定

        void DrawSprite(const std::string& textureName, const HIKARI::Transform2D& t, float width, float height, CameraMode cam, unsigned int rgba)
        {
            int handle = HIKARI::TEXTURE::GetDxHandle(textureName);
            if (handle < 0) return;

            RenderCommand cmd;
            cmd.type = CommandType::Sprite; // <--- 标记类型
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = handle;
            cmd.transform = t;
            cmd.width = width;
            cmd.height = height;
            SpriteUV uv;
            uv.u0 = 0.0f;
            uv.v0 = 0.0f;
            uv.u1 = 1.0f;
            uv.v1 = 1.0f;
            cmd.uv = uv;
            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        // ---- スプライト：名前指定 + UV ----
        void DrawSprite(const std::string& textureName, const HIKARI::Transform2D& t, float width, float height, const SpriteUV& uv, CameraMode cam, unsigned int rgba)
        {
            int handle = HIKARI::TEXTURE::GetDxHandle(textureName);
            if (handle < 0) return;

            RenderCommand cmd;
            cmd.type = CommandType::Sprite; // <--- 标记类型
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = handle;
            cmd.transform = t;
            cmd.width = width;
            cmd.height = height;
            cmd.uv = uv;
            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        void DrawSpriteRect(const std::string& textureName,
            int srcX, int srcY, int srcW, int srcH,
            const HIKARI::Transform2D& t,
            float dstW, float dstH,
            CameraMode cam,
            unsigned int rgba)
        {
            int handle = HIKARI::TEXTURE::GetDxHandle(textureName);
            if (handle < 0) return;

            // 获取纹理尺寸以计算 UV
            UINT texW = 0, texH = 0;
            HIKARI::DXTEX::DxTextureManager::GetTextureSize(handle, texW, texH);
            if (texW == 0 || texH == 0) return;

            RenderCommand cmd;
            cmd.type = CommandType::Sprite;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = handle;
            cmd.transform = t;
            cmd.width = dstW;
            cmd.height = dstH;

            // 计算 UV
            cmd.uv.u0 = (float)srcX / texW;
            cmd.uv.v0 = (float)srcY / texH;
            cmd.uv.u1 = (float)(srcX + srcW) / texW;
            cmd.uv.v1 = (float)(srcY + srcH) / texH;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        void PushSpriteRectHandleEx(
            int textureHandle,
            int srcX, int srcY, int srcW, int srcH,
            const HIKARI::Transform2D& t,
            float dstW, float dstH,
            CameraMode cam,
            unsigned int rgba,
            float sortYOverride)
        {
            if (textureHandle < 0) {
                return;
            }

            UINT texW = 0, texH = 0;
            HIKARI::DXTEX::DxTextureManager::GetTextureSize(textureHandle, texW, texH);
            if (texW == 0 || texH == 0) {
                return;
            }

            RenderCommand cmd;
            cmd.type = CommandType::Sprite;
            cmd.layer = gCurrentLayer;
            cmd.sortY = sortYOverride;

            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = textureHandle;
            cmd.transform = t;
            cmd.width = dstW;
            cmd.height = dstH;

            cmd.uv.u0 = static_cast<float>(srcX) / texW;
            cmd.uv.v0 = static_cast<float>(srcY) / texH;
            cmd.uv.u1 = static_cast<float>(srcX + srcW) / texW;
            cmd.uv.v1 = static_cast<float>(srcY + srcH) / texH;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        void DrawSpriteBill(const std::string& textureName,
            const HIKARI::Transform2D& t,
            float width, float height,
            CameraMode cam,
            unsigned int rgba,
            bool billboard, bool useMaskShader)
        {
            int handle = HIKARI::TEXTURE::GetDxHandle(textureName);
            if (handle < 0) return;

            // Transformのコピーを作成し、ビルボード計算を適用
            HIKARI::Transform2D tt = t;

            if (billboard && cam == CameraMode::Inherit) {
                // 1) カメラの回転を打ち消す（常に正面を向く）
                tt.rotation -= HIKARI::CAMERA::GetRotation();
                // 2) カメラのピッチ角によるY軸の縮みを補正
                tt.scale.y *= HIKARI::CAMERA::GetBillboardScaleY();
            }

            RenderCommand cmd;
            cmd.type = CommandType::Sprite;
            cmd.layer = gCurrentLayer;
            // ソート順は足元のY座標
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = handle;
            cmd.transform = tt; // 補正後のTransformを渡す
            cmd.width = width;
            cmd.height = height;

            // UVは全体
            cmd.uv.u0 = 0.0f; cmd.uv.v0 = 0.0f;
            cmd.uv.u1 = 1.0f; cmd.uv.v1 = 1.0f;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);
            cmd.useMaskShader = useMaskShader;

            gRenderList.push_back(cmd);
        }

        void DrawSpriteRectBill(const std::string& textureName,
            int srcX, int srcY, int srcW, int srcH,
            const HIKARI::Transform2D& t,
            float dstW, float dstH,
            CameraMode cam,
            unsigned int rgba,
            bool billboard,
            bool useMaskShader)
        {
            int handle = HIKARI::TEXTURE::GetDxHandle(textureName);
            if (handle < 0) return;

            UINT texW = 0, texH = 0;
            HIKARI::DXTEX::DxTextureManager::GetTextureSize(handle, texW, texH);
            if (texW == 0 || texH == 0) return;

            // Transformのコピーを作成し、ビルボード計算を適用
            HIKARI::Transform2D tt = t;

            if (billboard && cam == CameraMode::Inherit) {
                tt.rotation -= HIKARI::CAMERA::GetRotation();
                tt.scale.y *= HIKARI::CAMERA::GetBillboardScaleY();
            }

            RenderCommand cmd;
            cmd.type = CommandType::Sprite;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = handle;
            cmd.transform = tt; // 補正後のTransform
            cmd.width = dstW;
            cmd.height = dstH;

            // UV計算
            cmd.uv.u0 = (float)srcX / texW;
            cmd.uv.v0 = (float)srcY / texH;
            cmd.uv.u1 = (float)(srcX + srcW) / texW;
            cmd.uv.v1 = (float)(srcY + srcH) / texH;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            // シェーダー切り替え用フラグを設定
            cmd.useMaskShader = useMaskShader;

            gRenderList.push_back(cmd);
        }


        void DrawSpritePileStrip(
            const SpritePileStrip& strip,
            const SpritePileConfig& cfg,
            const HIKARI::Transform2D& baseT,
            float baseZ,
            CameraMode cam)
        {
            if (strip.textureName.empty()) { return; }
            if (strip.sliceW <= 0 || strip.sliceH <= 0) { return; }
            if (strip.sliceCount <= 0) { return; }

            const float dstW = (cfg.dstW > 0.0f) ? cfg.dstW : (float)strip.sliceW;
            const float dstH = (cfg.dstH > 0.0f) ? cfg.dstH : (float)strip.sliceH;
            Vector2 pivotPx{ dstW * cfg.pivotX01, dstH * cfg.pivotY01 };

            float camRot = 0.0f;
            float camScaleY = 1.0f;
            float s = 0.0f;
            float c = 1.0f;

            if (cam == CameraMode::Inherit) {
                camRot = HIKARI::CAMERA::GetRotation();
                camScaleY = HIKARI::CAMERA::GetBillboardScaleY();
                s = std::sin(camRot);
                c = std::cos(camRot);
            }

            // 辅助函数
            auto calculateOffset = [&](int i) -> Vector2 {
                Vector2 offset{ 0.0f, 0.0f };

                // 厚度偏移 
                float totalYOffset = 0.0f;
                if (cfg.depthMode == SpritePileDepthMode::Pure2D_YStep) {
                    if (cfg.yStepPx != 0.0f) {
                        totalYOffset += (float)i * cfg.yStepPx * camScaleY;
                    }
                } else if (cfg.extraYStepPx != 0.0f) {
                    totalYOffset += (float)i * cfg.extraYStepPx * camScaleY;
                }

                if (totalYOffset != 0.0f) {

                    offset.x -= totalYOffset * s;
                    offset.y -= totalYOffset * c;
                }

                if (cfg.depthMode == SpritePileDepthMode::Pure2D_YStep) {

                    if (cfg.stepXPerSlicePx != 0.0f) offset.x += (float)i * cfg.stepXPerSlicePx;
                    if (cfg.stepYPerSlicePx != 0.0f) offset.y += (float)i * cfg.stepYPerSlicePx;
                }

                return offset;
                };

            // 实际绘制函数
            auto drawCmd = [&](int srcX, int srcY, const HIKARI::Transform2D& drawT) {
                int handle = HIKARI::TEXTURE::GetDxHandle(strip.textureName);
                if (handle < 0) return;

                // 排序永远用物体脚底的 Y。
                // baseZ 用作“排序补偿”（例如跳跃：视觉上 y 往上抬，但排序仍按脚底 y）。
                float pileSortY = baseT.position.y + baseZ;

                PushSpriteRectHandleEx(
                    handle,
                    srcX, srcY, strip.sliceW, strip.sliceH,
                    drawT,
                    dstW, dstH,
                    cam,
                    cfg.rgba,
                    pileSortY
                );
                };

            struct DrawInfo {
                int sliceIndex;
                Vector2 finalPos;
            };
            static std::vector<DrawInfo> drawQueue;
            drawQueue.clear();
            drawQueue.reserve(strip.sliceCount * (cfg.gapFillSteps + 1));

            int steps = (cfg.gapFillSteps < 0) ? 0 : cfg.gapFillSteps;
            float stepRecip = 1.0f / (float)(steps + 1);

            for (int i = 0; i < strip.sliceCount; ++i)
            {
                Vector2 currentOffset = calculateOffset(i);


                Vector2 nextOffset = calculateOffset(i + 1);


                Vector2 delta = { nextOffset.x - currentOffset.x, nextOffset.y - currentOffset.y };


                int kMax = (i == strip.sliceCount - 1) ? 0 : steps;

                for (int k = 0; k <= kMax; ++k)
                {

                    float t = (float)k * stepRecip;
                    Vector2 interpOffset = {
                        currentOffset.x + delta.x * t,
                        currentOffset.y + delta.y * t
                    };

                    Vector2 finalPos = {
                        baseT.position.x + interpOffset.x,
                        baseT.position.y + interpOffset.y
                    };

                    drawQueue.push_back({ i, finalPos });
                }
            }


            auto submit = [&](const DrawInfo& info) {
                HIKARI::Transform2D t = baseT;
                t.pivotPx = pivotPx;
                t.position = info.finalPos;

                if (cam == CameraMode::Inherit && cfg.isBillboard) {
                    t.rotation -= camRot;
                }

                drawCmd(info.sliceIndex * strip.sliceW, 0, t);
                };

            if (strip.reverseOrder) {
                for (int i = (int)drawQueue.size() - 1; i >= 0; --i) submit(drawQueue[i]);
            } else {
                for (size_t i = 0; i < drawQueue.size(); ++i) submit(drawQueue[i]);
            }
        }

        // ---- スプライト：ハンドル指定 ----
        void DrawSpriteHandle(int textureHandle,
            const HIKARI::Transform2D& t,
            float width, float height,
            CameraMode cam,
            unsigned int rgba)
        {
            if (textureHandle < 0) return;

            RenderCommand cmd;
            cmd.type = CommandType::Sprite;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = textureHandle;
            cmd.transform = t;
            cmd.width = width;
            cmd.height = height;
            // 默认 UV 全图
            cmd.uv = { 0.0f, 0.0f, 1.0f, 1.0f };

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        // ---- スプライト：ハンドル指定 ----
        void DrawSpriteRectHandle(int textureHandle,
            int srcX, int srcY, int srcW, int srcH,
            const HIKARI::Transform2D& t,
            float dstW, float dstH,
            CameraMode cam,
            unsigned int rgba)
        {
            if (textureHandle < 0) return;

            UINT texW = 0, texH = 0;
            HIKARI::DXTEX::DxTextureManager::GetTextureSize(textureHandle, texW, texH);
            if (texW == 0 || texH == 0) return;

            RenderCommand cmd;
            cmd.type = CommandType::Sprite;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = textureHandle;
            cmd.transform = t;
            cmd.width = dstW;
            cmd.height = dstH;

            // UV 计算
            cmd.uv.u0 = (float)srcX / texW;
            cmd.uv.v0 = (float)srcY / texH;
            cmd.uv.u1 = (float)(srcX + srcW) / texW;
            cmd.uv.v1 = (float)(srcY + srcH) / texH;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        // =========================================================
        // DeformGrid 描画（DXハンドル版）
        // =========================================================
        void DrawDeformGridHandle(
            int dxHandle,
            const DeformGrid& grid,
            CameraMode cam,
            unsigned int rgba)
        {
            if (dxHandle < 0) return;
            if (grid.cols < 2 || grid.rows < 2) return;
            if (grid.positions.empty()) return;

            RenderCommand cmd;
            cmd.type = CommandType::DeformGrid;
            cmd.layer = gCurrentLayer;
            // 排序深度：简单取第一个点的 Y 坐标
            cmd.sortY = grid.positions[0].y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = dxHandle;
            // DeformGrid 的 positions 通常是世界坐标，Transform 设为 Identity
            cmd.transform = HIKARI::Transform2D();
            cmd.width = 0;
            cmd.height = 0;

            // 拷贝网格数据
            cmd.gridData = grid;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            gRenderList.push_back(cmd);
        }

        // =========================================================
        // DeformGrid 描画（テクスチャ名版）
        // =========================================================
        void DrawDeformGrid(const std::string& textureName, const DeformGrid& grid, CameraMode cam, unsigned int rgba)
        {
            int h = HIKARI::TEXTURE::GetDxHandle(textureName);
            if (h >= 0) DrawDeformGridHandle(h, grid, cam, rgba);
        }

        // =========================================================
        // MeshQuad 
        // =========================================================

        void DrawMeshQuadHandleUV_Local(
            int textureHandle,
            const HIKARI::Transform2D& t,
            const Vector2& ltLocal, const Vector2& rtLocal,
            const Vector2& lbLocal, const Vector2& rbLocal,
            float u_lt, float v_lt, float u_rt, float v_rt,
            float u_lb, float v_lb, float u_rb, float v_rb,
            CameraMode cam,
            unsigned int rgba,
            bool billboard // <--- 新增参数
        )
        {
            if (textureHandle < 0) return;

            // [核心修改] 处理广告牌逻辑
            HIKARI::Transform2D tt = t;
            if (billboard && cam == CameraMode::Inherit) {
                // 1. 抵消摄像机旋转，始终面向屏幕
                tt.rotation -= HIKARI::CAMERA::GetRotation();
                // 2. 修正 2.5D 透视下的 Y 轴压扁
                tt.scale.y *= HIKARI::CAMERA::GetBillboardScaleY();
            }

            RenderCommand cmd;
            cmd.type = CommandType::MeshQuad;
            cmd.layer = gCurrentLayer;
            cmd.sortY = t.position.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = textureHandle;

            // 将修正后的 Transform 存入指令
            cmd.transform = tt;

            // 存入4个局部顶点 (相对于 Spine 原点的坐标)
            cmd.p0 = ltLocal; cmd.p1 = rtLocal; cmd.p2 = lbLocal; cmd.p3 = rbLocal;

            // 存入4组UV
            cmd.u_lt = u_lt; cmd.v_lt = v_lt;
            cmd.u_rt = u_rt; cmd.v_rt = v_rt;
            cmd.u_lb = u_lb; cmd.v_lb = v_lb;
            cmd.u_rb = u_rb; cmd.v_rb = v_rb;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            // 这里的 width/height 仅用于 pivot 计算，Mesh 模式下通常不重要，设为 1 即可
            cmd.width = 1.0f;
            cmd.height = 1.0f;

            gRenderList.push_back(cmd);
        }


        // ---- SPINE ----

        void DrawSpriteRectHandleVertices(
            int textureHandle,
            int srcX, int srcY, int srcW, int srcH,
            const Vector2& ltIn, const Vector2& rtIn,
            const Vector2& lbIn, const Vector2& rbIn,
            CameraMode cam,
            unsigned int rgba
        ) {
            if (textureHandle < 0) return;
            UINT texW = 0, texH = 0;
            HIKARI::DXTEX::DxTextureManager::GetTextureSize(textureHandle, texW, texH);
            if (texW == 0 || texH == 0) return;

            float u0 = (float)srcX / texW;
            float v0 = (float)srcY / texH;
            float u1 = (float)(srcX + srcW) / texW;
            float v1 = (float)(srcY + srcH) / texH;

            DrawMeshQuadHandleUV_Vertices(
                textureHandle,
                ltIn, rtIn, lbIn, rbIn,
                u0, v0, u1, v0,
                u0, v1, u1, v1,
                cam, rgba
            );
        }



        void DrawMeshQuadHandleUV_Vertices(
            int textureHandle,
            const Vector2& lt, const Vector2& rt,
            const Vector2& lb, const Vector2& rb,
            float u_lt, float v_lt, float u_rt, float v_rt,
            float u_lb, float v_lb, float u_rb, float v_rb,
            CameraMode cam,
            unsigned int rgba)
        {
            if (textureHandle < 0) return;

            RenderCommand cmd;
            cmd.type = CommandType::MeshQuad;
            cmd.layer = gCurrentLayer;
            // 排序深度：取底部两点的最大 Y (最靠近相机的点)
            cmd.sortY = (lb.y > rb.y) ? lb.y : rb.y;
            cmd.orderIndex = gSubmissionCount++;

            cmd.dxHandle = textureHandle;
            // 顶点已经是世界坐标了，不需要再变换，Transform 设为 Identity
            cmd.transform = HIKARI::Transform2D();

            // 存入4个顶点
            cmd.p0 = lt; cmd.p1 = rt; cmd.p2 = lb; cmd.p3 = rb;

            // 存入4组UV
            cmd.u_lt = u_lt; cmd.v_lt = v_lt;
            cmd.u_rt = u_rt; cmd.v_rt = v_rt;
            cmd.u_lb = u_lb; cmd.v_lb = v_lb;
            cmd.u_rb = u_rb; cmd.v_rb = v_rb;

            cmd.camMode = cam;
            cmd.rgba = (rgba == 0xFFFFFFFF ? gDefaultColor : rgba);

            cmd.width = 0; cmd.height = 0;

            gRenderList.push_back(cmd);
        }



        // ---- ANIMATION ----
        void DrawSpriteFrame(
            const std::string& textureName,
            const SpriteSheetInfo& sheet,
            int frameIndex,
            const HIKARI::Transform2D& t,
            CameraMode cam,
            unsigned int rgba)
        {
            if (sheet.frameWidth <= 0 || sheet.frameHeight <= 0 || sheet.columns <= 0) {
                return;
            }

            if (frameIndex < 0) {
                return;
            }

            int col = frameIndex % sheet.columns;
            int row = frameIndex / sheet.columns;

            int srcX = col * sheet.frameWidth;
            int srcY = row * sheet.frameHeight;
            int srcW = sheet.frameWidth;
            int srcH = sheet.frameHeight;

            DrawSpriteRect(
                textureName,
                srcX, srcY, srcW, srcH,
                t,
                static_cast<float>(srcW),
                static_cast<float>(srcH),
                cam,
                rgba
            );
        }

        // フレーム + MeshQuad 変形版
        void DrawSpriteFrameEx(
            const std::string& textureName,
            const SpriteSheetInfo& sheet,
            int frameIndex,
            const HIKARI::Transform2D& t,
            const SpriteFrameDeform& deform,
            CameraMode cam,
            unsigned int rgba)
        {
            if (sheet.frameWidth <= 0 || sheet.frameHeight <= 0 || sheet.columns <= 0) {
                return;
            }
            if (frameIndex < 0) {
                return;
            }

            // 1) まずは通常のフレーム計算（どのコマを使うか）
            int col = frameIndex % sheet.columns;
            int row = frameIndex / sheet.columns;

            int srcX = col * sheet.frameWidth;
            int srcY = row * sheet.frameHeight;
            int srcW = sheet.frameWidth;
            int srcH = sheet.frameHeight;

            // 2) DX テクスチャハンドル＆サイズ取得
            int dxHandle = HIKARI::TEXTURE::GetDxHandle(textureName);
            if (dxHandle < 0) {
                return;
            }

            UINT texW = 0, texH = 0;
            HIKARI::DXTEX::DxTextureManager::GetTextureSize(dxHandle, texW, texH);
            if (texW == 0 || texH == 0) {
                return;
            }

            // 3) このフレームがテクスチャ全体のどの範囲か（ベース UV）
            float baseU0 = static_cast<float>(srcX) / static_cast<float>(texW);
            float baseV0 = static_cast<float>(srcY) / static_cast<float>(texH);
            float baseU1 = static_cast<float>(srcX + srcW) / static_cast<float>(texW);
            float baseV1 = static_cast<float>(srcY + srcH) / static_cast<float>(texH);

            // 4) deform.uv は「フレーム内」でのローカル UV（0～1）として解釈
            //    例: uv={0,0,1,1} → フレーム全体
            auto lerp = [](float a, float b, float t) {
                return a + (b - a) * t;
                };

            float u0 = lerp(baseU0, baseU1, deform.uv.u0);
            float v0 = lerp(baseV0, baseV1, deform.uv.v0);
            float u1 = lerp(baseU0, baseU1, deform.uv.u1);
            float v1 = lerp(baseV0, baseV1, deform.uv.v1);

            // 5) ローカル頂点（フレームサイズを基準にした矩形）＋オフセット
            float w = static_cast<float>(srcW);
            float h = static_cast<float>(srcH);

            Vector2 ltLocal{ 0.0f, 0.0f };
            Vector2 rtLocal{ w,    0.0f };
            Vector2 lbLocal{ 0.0f, h };
            Vector2 rbLocal{ w,    h };

            ltLocal.x += deform.offsetLT.x;
            ltLocal.y += deform.offsetLT.y;
            rtLocal.x += deform.offsetRT.x;
            rtLocal.y += deform.offsetRT.y;
            lbLocal.x += deform.offsetLB.x;
            lbLocal.y += deform.offsetLB.y;
            rbLocal.x += deform.offsetRB.x;
            rbLocal.y += deform.offsetRB.y;

            // 6) Transform2D + Camera を使って MeshQuad 描画
            unsigned int c = (rgba == 0xFFFFFFFF ? GetDefaultColor() : rgba);

            DrawMeshQuadHandleUV_Local(
                dxHandle,
                t,
                ltLocal, rtLocal, lbLocal, rbLocal,
                u0, v0,   // 左上
                u1, v0,   // 右上
                u0, v1,   // 左下
                u1, v1,   // 右下
                cam,
                c
            );
        }


    } // namespace RENDERER
} // namespace HIKARI
