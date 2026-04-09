#include "HIKARI_Camera2_5D.h"
#include <cmath>

namespace HIKARI {
    namespace CAMERA25 {

        static Params gParams{};

        static float Clamp(float v, float a, float b) {
            return (v < a) ? a : (v > b) ? b : v;
        }

        void SetParams(const Params& p) { gParams = p; }
        const Params& GetParams() { return gParams; }

        void SetFocusZ(float z) { gParams.focusZ = z; }
        float GetFocusZ() { return gParams.focusZ; }

        float Depth01(float z)
        {
            float denom = (gParams.zFar - gParams.zNear);
            if (std::fabs(denom) < 1e-6f) {
                return 0.0f;
            }
            float t = (z - gParams.zNear) / denom;
            return Clamp(t, 0.0f, 1.0f);
        }

        float Scale(float z)
        {

            float dz = (z - gParams.focusZ);
            float s = 1.0f / (1.0f + dz * gParams.perspective);
            return Clamp(s, gParams.minScale, gParams.maxScale);
        }

        Projection Project(const Vector2& worldXY, float z)
        {
            Projection o{};
            float dz = (z - gParams.focusZ);

            o.scale = Scale(z);
            o.depth01 = Depth01(z);

            Vector2 camPos = HIKARI::CAMERA::GetPosition();
            float s = o.scale;

            Vector2 rel{ worldXY.x - camPos.x, worldXY.y - camPos.y };
            Vector2 w{ camPos.x + rel.x * s, camPos.y + rel.y * s };

            w.y -= dz * gParams.depthToWorldYOffset;

            if (std::fabs(gParams.depthToWorldXFactor) > 1e-6f) {
                Vector2 camPos = HIKARI::CAMERA::GetPosition();
                float relX = (worldXY.x - camPos.x);
                w.x -= dz * relX * gParams.depthToWorldXFactor;
            }

            o.world2D = w;
            o.sortKey = w.y + z * gParams.sortZBias;
            return o;
        }

        Transform2D ApplyToTransform(const Transform2D& inWorld, float z)
        {
            Projection p = Project(inWorld.position, z);

            Transform2D out = inWorld;
            out.position = p.world2D;
            out.scale.x *= p.scale;
            out.scale.y *= p.scale;

            return out;
        }

    } // namespace CAMERA25
} // namespace HIKARI
