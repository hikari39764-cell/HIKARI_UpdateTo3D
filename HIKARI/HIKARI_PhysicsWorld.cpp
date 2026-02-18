#include "HIKARI_PhysicsWorld.h"

namespace HIKARI {
    namespace PHYSICS {

        using namespace HIKARI::COLLISION;

        void PhysicsWorld::SetTileColliders(const std::vector<Rect>& tiles) {
            tiles_ = tiles;
        }

        void PhysicsWorld::AddTileColliders(const std::vector<Rect>& tiles) {
            tiles_.insert(tiles_.end(), tiles.begin(), tiles.end());
        }

        void PhysicsWorld::Clear() {
            tiles_.clear();
        }

        void PhysicsWorld::MoveAabbWithTiles(Rect& inOutRect,
            Vector2& inOutVel,
            float dt,
            MoveHitInfo& outHit) const {

            outHit = MoveHitInfo{}; 

            Vector2 delta{
                inOutVel.x * dt,
                inOutVel.y * dt
            };

            inOutRect.x += delta.x;

            for (const Rect& tile : tiles_) {
                if (!RectRect(inOutRect, tile)) {
                    continue;
                }

                Vector2 mtv = ComputeMTV(inOutRect, tile);

                if (mtv.x != 0.0f) {
                    inOutRect.x += mtv.x;
                    inOutVel.x = 0.0f;

                    outHit.hit = true;
                    outHit.normal = { 0.0f, 0.0f };

                    if (mtv.x > 0.0f) {
                        outHit.hitLeft = true;
                        outHit.normal.x = 1.0f;
                    } else {
                        outHit.hitRight = true;
                        outHit.normal.x = -1.0f;
                    }
                }
            }

            // ---------- Y 方向 ----------
            inOutRect.y += delta.y;

            for (const Rect& tile : tiles_) {
                if (!RectRect(inOutRect, tile)) {
                    continue;
                }

                Vector2 mtv = ComputeMTV(inOutRect, tile);

                if (mtv.y != 0.0f) {
                    inOutRect.y += mtv.y;
                    inOutVel.y = 0.0f;

                    outHit.hit = true;

                    if (mtv.y < 0.0f) {
                        outHit.hitGround = true;
                        outHit.normal.y = -1.0f; 
                    } else {

                        outHit.hitCeil = true;
                        outHit.normal.y = 1.0f; 
                    }
                }
            }
        }

    } // namespace PHYSICS
} // namespace HIKARI
