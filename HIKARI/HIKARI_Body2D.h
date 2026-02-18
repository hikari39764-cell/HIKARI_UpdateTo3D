#pragma once
#include "HIKARI_Transform2D.h"
#include "HIKARI_PhysicsWorld.h"

namespace HIKARI {
    namespace PHYSICS {

        struct Body2D {
            HIKARI::Transform2D* transform = nullptr; 

            Vector2 velocity{ 0.0f, 0.0f };

            float width{ 32.0f };
            float height{ 32.0f };

            bool  useGravity{ true };
            float gravityScale{ 1.0f }; 

            bool grounded{ false };
            bool hitCeil{ false };
            bool hitLeft{ false };
            bool hitRight{ false };

            void AttachTransform(HIKARI::Transform2D* t) {
                transform = t;
            }

            void SetSize(float w, float h) {
                width = w;
                height = h;
            }

            HIKARI::COLLISION::Rect ToRect() const {
                HIKARI::COLLISION::Rect r{};
                if (transform != nullptr) {
                    r.x = transform->position.x;
                    r.y = transform->position.y;
                }
                r.width = width;
                r.height = height;
                return r;
            }

            void ApplyRect(const HIKARI::COLLISION::Rect& r) {
                if (transform != nullptr) {
                    transform->position.x = r.x;
                    transform->position.y = r.y;
                }
            }

            void Step(PhysicsWorld& world, float dt, float baseGravity) {

                if (useGravity) {
                    velocity.y += baseGravity * gravityScale * dt;
                }

                HIKARI::COLLISION::Rect r = ToRect();

                MoveHitInfo hit{};
                world.MoveAabbWithTiles(r, velocity, dt, hit);

                ApplyRect(r);

                grounded = hit.hitGround;
                hitCeil = hit.hitCeil;
                hitLeft = hit.hitLeft;
                hitRight = hit.hitRight;
            }
        };

    } // namespace PHYSICS
} // namespace HIKARI
