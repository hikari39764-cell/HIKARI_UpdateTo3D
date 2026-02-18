#pragma once
#include <vector>
#include "HIKARI_Collision.h"  // 里面有 COLLISION::Rect / ComputeMTV

namespace HIKARI {
    namespace PHYSICS {

        /// 一次带碰撞移动的结果
        struct MoveHitInfo {
            bool hit = false;           // 本帧是否发生过任意碰撞
            bool hitGround = false;           // 是否踩到地面（脚下）
            bool hitCeil = false;           // 是否撞到天花板
            bool hitLeft = false;           // 是否撞到左墙
            bool hitRight = false;           // 是否撞到右墙

            Vector2 normal{ 0.0f, 0.0f };
        };


        class PhysicsWorld {
        public:
            PhysicsWorld() = default;

            void SetTileColliders(const std::vector<COLLISION::Rect>& tiles);

            void AddTileColliders(const std::vector<COLLISION::Rect>& tiles);

            void Clear();

            const std::vector<COLLISION::Rect>& GetTileColliders() const { return tiles_; }


            void MoveAabbWithTiles(COLLISION::Rect& inOutRect,
                Vector2& inOutVel,
                float dt,
                MoveHitInfo& outHit) const;

        private:
            std::vector<COLLISION::Rect> tiles_;
        };

    } // namespace PHYSICS
} // namespace HIKARI
