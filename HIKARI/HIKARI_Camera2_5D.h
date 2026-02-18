#pragma once
#include "HIKARI_Camera.h"
#include "HIKARI_Transform2D.h"
#include <algorithm>

namespace HIKARI {
    namespace CAMERA25 {

        // 2.5D 投影器：在不引入真正 3D 相机/4x4 矩阵的前提下，
        // 用一个“深度 z”来驱动：缩放（近大远小）/ 伪透视偏移 / 以及 post 用的 depth01。
        struct Params {
            // 绝对深度范围（用于 depth01 归一化）
            float zNear = 0.0f;
            float zFar = 10.0f;

            // “玩家稳定、背景缩放”的关键：以 focusZ 为中心计算相对深度 dz
            // dz = z - focusZ
            float focusZ = 0.0f;

            // 近大远小强度：scale = 1 / (1 + dz * perspective)
            float perspective = 0.18f;

            // 越往里，整体往上抬（舞台感）
            // world2D.y -= dz * depthToWorldYOffset
            float depthToWorldYOffset = 24.0f;

            // 可选：X 方向收敛（默认 0）
            // world2D.x -= dz * (worldX - cameraPosX) * depthToWorldXFactor
            float depthToWorldXFactor = 0.00f;

            float minScale = 0.20f;
            float maxScale = 2.50f;

            // sortKey = projectedY + (z * sortZBias)
            float sortZBias = 64.0f;
        };

        struct Projection {
            Vector2 world2D{};      // 套用 2.5D 后的“投影世界坐标”（仍然是2D世界）
            float   scale = 1.0f;   // 建议乘到 Transform2D.scale 上
            float   depth01 = 0.0f; // 0..1（给 post 做 fog/dof）
            float   sortKey = 0.0f; // 可用于绘制排序（越大越靠前）
        };

        void SetParams(const Params& p);
        const Params& GetParams();

        // 快捷：只改 focusZ（通常每帧设为 playerZ）
        void SetFocusZ(float z);
        float GetFocusZ();

        // 将 (worldXY, z) 投影为 “2D世界坐标 + 缩放 + depth01”
        Projection Project(const Vector2& worldXY, float z);

        // 仅计算 depth01（用绝对 zNear/zFar 归一化）
        float Depth01(float z);

        // 仅计算 scale（近大远小，使用相对深度 dz = z - focusZ）
        float Scale(float z);

        // 一步到位：把 world Transform2D + z 变成“投影后可直接绘制”的 Transform2D
        // - position：替换为投影后的 world2D
        // - scale：乘上投影 scale
        // - rotation/pivot：保持不变
        Transform2D ApplyToTransform(const Transform2D& inWorld, float z);

    } // namespace CAMERA25
} // namespace HIKARI
