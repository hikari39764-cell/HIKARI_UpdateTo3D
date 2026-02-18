#include "HIKARI_CollisionWorld.h"

namespace HIKARI {
    namespace COLLISION {

        void CollisionWorld::SetColliders(const std::vector<Collider>& colliders) {
            colliders_ = colliders;
        }

        void CollisionWorld::AddColliders(const std::vector<Collider>& colliders) {
            colliders_.insert(colliders_.end(), colliders.begin(), colliders.end());
        }

        void CollisionWorld::Clear() {
            colliders_.clear();
        }

        PointHit CollisionWorld::PointCast(const Vector2& p, int layerMask) const {
            PointHit hit{};

            for (const Collider& col : colliders_) {
                if (layerMask >= 0) {
                    if (((1 << col.layer) & layerMask) == 0) {
                        continue;
                    }
                }

                bool inside = false;

                if (col.shapeType == ShapeType::Rect) {
                    inside = PointInRect(p, col.rect);
                } else if (col.shapeType == ShapeType::Circle) {
                    inside = PointInCircle(p, col.circle);
                }

                if (inside) {
                    hit.collider = &col;
                    break;
                }
            }

            return hit;
        }

        OverlapResult CollisionWorld::OverlapRect(const Rect& r, int layerMask) const {
            OverlapResult result{};

            for (const Collider& col : colliders_) {
                if (layerMask >= 0) {
                    if (((1 << col.layer) & layerMask) == 0) {
                        continue;
                    }
                }

                bool overlap = false;

                if (col.shapeType == ShapeType::Rect) {
                    overlap = RectRect(r, col.rect);
                } else if (col.shapeType == ShapeType::Circle) {
                    overlap = RectCircle(r, col.circle);
                }

                if (overlap) {
                    result.colliders.push_back(&col);
                }
            }

            return result;
        }

        OverlapResult CollisionWorld::OverlapCircle(const Circle& c, int layerMask) const {
            OverlapResult result{};

            for (const Collider& col : colliders_) {
                if (layerMask >= 0) {
                    if (((1 << col.layer) & layerMask) == 0) {
                        continue;
                    }
                }

                bool overlap = false;

                if (col.shapeType == ShapeType::Rect) {
                    overlap = RectCircle(col.rect, c);
                } else if (col.shapeType == ShapeType::Circle) {
                    overlap = CircleCircle(col.circle, c);
                }

                if (overlap) {
                    result.colliders.push_back(&col);
                }
            }

            return result;
        }

    } // namespace COLLISION
} // namespace HIKARI
