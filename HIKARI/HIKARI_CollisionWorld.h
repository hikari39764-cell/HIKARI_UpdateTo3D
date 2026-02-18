#pragma once
#include <vector>
#include "HIKARI_Collision.h"

namespace HIKARI {
    namespace COLLISION {

        struct PointHit {
            const Collider* collider = nullptr;
        };

        struct OverlapResult {
            std::vector<const Collider*> colliders;
        };

        class CollisionWorld {
        public:
            CollisionWorld() = default;

            void SetColliders(const std::vector<Collider>& colliders);

            void AddColliders(const std::vector<Collider>& colliders);

            void Clear();

            const std::vector<Collider>& GetColliders() const { return colliders_; }

            PointHit PointCast(const Vector2& p,
                int layerMask = -1) const;

            OverlapResult OverlapRect(const Rect& r,
                int layerMask = -1) const;

            OverlapResult OverlapCircle(const Circle& c,
                int layerMask = -1) const;

        private:
            std::vector<Collider> colliders_;
        };

    } // namespace COLLISION
} // namespace HIKARI
