#pragma once
#include <vector>
#include <string>
#include "HIKARI_Utility.h"

namespace HIKARI {
    namespace COLLISION {

        enum class ShapeType {
            Rect,
            Circle,
        };

        struct Rect {
            float x{ 0.0f };
            float y{ 0.0f };
            float width{ 0.0f };
            float height{ 0.0f };
        };

        struct Circle {
            Vector2 center{ 0.0f, 0.0f };
            float   radius{ 0.0f };
        };

        struct Collider {
            ShapeType shapeType = ShapeType::Rect;

            Rect   rect{};
            Circle circle{};

            std::string tag;
            int         layer = 0;
            unsigned    mask = 0xFFFFFFFF;
        };


        inline bool RectRect(const Rect& a, const Rect& b) {
            if (a.x + a.width <= b.x) { return false; }
            if (b.x + b.width <= a.x) { return false; }
            if (a.y + a.height <= b.y) { return false; }
            if (b.y + b.height <= a.y) { return false; }
            return true;
        }

        inline bool CircleCircle(const Circle& a, const Circle& b) {
            float dx = a.center.x - b.center.x;
            float dy = a.center.y - b.center.y;
            float r = a.radius + b.radius;
            return (dx * dx + dy * dy) <= (r * r);
        }


        inline bool PointInRect(const Vector2& p, const Rect& r) {
            return (p.x >= r.x && p.x <= r.x + r.width &&
                p.y >= r.y && p.y <= r.y + r.height);
        }

        inline bool PointInCircle(const Vector2& p, const Circle& c) {
            float dx = p.x - c.center.x;
            float dy = p.y - c.center.y;
            return (dx * dx + dy * dy) <= (c.radius * c.radius);
        }

        inline bool RectCircle(const Rect& r, const Circle& c) {
            float closestX = c.center.x;
            if (closestX < r.x) { closestX = r.x; } else if (closestX > r.x + r.width) { closestX = r.x + r.width; }

            float closestY = c.center.y;
            if (closestY < r.y) { closestY = r.y; } else if (closestY > r.y + r.height) { closestY = r.y + r.height; }

            float dx = c.center.x - closestX;
            float dy = c.center.y - closestY;
            return (dx * dx + dy * dy) <= (c.radius * c.radius);
        }

        inline Vector2 ComputeMTV(const Rect& a, const Rect& b) {
            float aMinX = a.x;
            float aMaxX = a.x + a.width;
            float aMinY = a.y;
            float aMaxY = a.y + a.height;

            float bMinX = b.x;
            float bMaxX = b.x + b.width;
            float bMinY = b.y;
            float bMaxY = b.y + b.height;

            float left = aMaxX - bMinX;
            float right = bMaxX - aMinX;
            float top = aMaxY - bMinY;
            float bottom = bMaxY - aMinY;

            float minXPen = (left < right) ? left : right;
            float minYPen = (top < bottom) ? top : bottom;

            Vector2 mtv{ 0.0f, 0.0f };

            if (minXPen < minYPen) {

                if (left < right) {
                    mtv.x = -left;
                } else {
                    mtv.x = right;
                }
            } else {

                if (top < bottom) {
                    mtv.y = -top;
                } else {
                    mtv.y = bottom;
                }
            }

            return mtv;
        }

    } // namespace COLLISION
} // namespace HIKARI
