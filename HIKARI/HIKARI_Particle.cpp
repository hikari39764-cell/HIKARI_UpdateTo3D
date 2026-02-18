#include "HIKARI_Particle.h"
#include <cstdlib>
#include <cmath>

namespace HIKARI {
    namespace PARTICLE {

        namespace {

            float RandomRange(float minV, float maxV) {
                float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
                return minV + (maxV - minV) * r;
            }

            float Clamp01(float t) {
                if (t < 0.0f) {
                    return 0.0f;
                }
                if (t > 1.0f) {
                    return 1.0f;
                }
                return t;
            }

            float Lerp(float a, float b, float t) {
                return a + (b - a) * t;
            }

            unsigned int LerpColor(unsigned int c0, unsigned int c1, float t) {
                t = Clamp01(t);

                unsigned int r0 = (c0 >> 24) & 0xFF;
                unsigned int g0 = (c0 >> 16) & 0xFF;
                unsigned int b0 = (c0 >> 8) & 0xFF;
                unsigned int a0 = (c0) & 0xFF;

                unsigned int r1 = (c1 >> 24) & 0xFF;
                unsigned int g1 = (c1 >> 16) & 0xFF;
                unsigned int b1 = (c1 >> 8) & 0xFF;
                unsigned int a1 = (c1) & 0xFF;

                unsigned int r = static_cast<unsigned int>(Lerp(static_cast<float>(r0), static_cast<float>(r1), t));
                unsigned int g = static_cast<unsigned int>(Lerp(static_cast<float>(g0), static_cast<float>(g1), t));
                unsigned int b = static_cast<unsigned int>(Lerp(static_cast<float>(b0), static_cast<float>(b1), t));
                unsigned int a = static_cast<unsigned int>(Lerp(static_cast<float>(a0), static_cast<float>(a1), t));

                return (r << 24) | (g << 16) | (b << 8) | a;
            }

            Vector2 Normalize(const Vector2& v) {
                float lenSq = v.x * v.x + v.y * v.y;
                if (lenSq <= 0.0f) {
                    return Vector2{ 0.0f, 0.0f };
                }
                float invLen = 1.0f / std::sqrt(lenSq);
                return Vector2{ v.x * invLen, v.y * invLen };
            }

        } // anonymous namespace

        // ===== Emitter =====

        Emitter::Emitter(const EmitterConfig& cfg, DrawFunc draw, SpawnFunc spawn)
            : config(cfg),
            particles(cfg.maxParticles),
            drawFunc(draw),
            spawnFunc(spawn),
            emitTimer(0.0f),
            active(true) {
        }

        void Emitter::SpawnOne(const Vector2& emitterPos) {
            // 空いているスロットを探す
            Particle* slot = nullptr;
            for (int i = 0; i < static_cast<int>(particles.size()); ++i) {
                if (!particles[i].alive) {
                    slot = &particles[i];
                    break;
                }
            }
            if (slot == nullptr) {
                return;
            }

            *slot = Particle{};
            slot->alive = true;

            slot->transform.scale = Vector2{ 1.0f, 1.0f };
            slot->transform.rotation = 0.0f;
            slot->transform.pivotPx = Vector2{ 0.0f, 0.0f };

            Vector2 pos = emitterPos;
            spawnFunc(config, *slot, pos);
        }

        void Emitter::EmitBurst(int count) {
            Vector2 emitterPos = config.originPosition;
            if (config.followTransform != nullptr) {
                emitterPos = config.followTransform->position;
                emitterPos.x += config.localOffset.x;
                emitterPos.y += config.localOffset.y;
            }

            for (int i = 0; i < count; ++i) {
                SpawnOne(emitterPos);
            }
        }

        void Emitter::Update(float dt) {

            Vector2 emitterPos = config.originPosition;
            if (config.followTransform != nullptr) {
                emitterPos = config.followTransform->position;
                emitterPos.x += config.localOffset.x;
                emitterPos.y += config.localOffset.y;
            }

            // active==true のときだけ新しいパーティクルを生成する
            if (active && config.emitRate > 0.0f) {
                float interval = 1.0f / config.emitRate;
                emitTimer += dt;
                while (emitTimer >= interval) {
                    emitTimer -= interval;
                    SpawnOne(emitterPos);
                }
            }

            for (int i = 0; i < static_cast<int>(particles.size()); ++i) {
                Particle& p = particles[i];
                if (!p.alive) {
                    continue;
                }

                // 寿命
                p.age += dt;
                if (p.age >= p.lifetime) {
                    p.alive = false;
                    continue;
                }

                p.t = Clamp01(p.age / p.lifetime);

                // 物理：加速度＋重力＋ターゲットへの吸引＋減衰
                Vector2 acc = p.acceleration;
                if (config.physics.gravity.x != 0.0f || config.physics.gravity.y != 0.0f) {
                    acc.x += config.physics.gravity.x;
                    acc.y += config.physics.gravity.y;
                }

                if (config.targetTransform != nullptr && config.attractStrength > 0.0f) {
                    Vector2 targetPos = config.targetTransform->position;
                    Vector2 toTarget{
                        targetPos.x - p.transform.position.x,
                        targetPos.y - p.transform.position.y
                    };
                    Vector2 dir = Normalize(toTarget);
                    acc.x += dir.x * config.attractStrength;
                    acc.y += dir.y * config.attractStrength;
                }

                p.velocity.x += acc.x * dt;
                p.velocity.y += acc.y * dt;

                if (config.physics.useDamping && config.physics.damping > 0.0f) {
                    float factor = 1.0f - config.physics.damping * dt;
                    if (factor < 0.0f) { factor = 0.0f; }
                    p.velocity.x *= factor;
                    p.velocity.y *= factor;
                }

                p.transform.position.x += p.velocity.x * dt;
                p.transform.position.y += p.velocity.y * dt;
                p.transform.rotation += p.angularVelocity * dt;

                p.color = LerpColor(config.startColor, config.endColor, p.t);

                float sizeFrom = config.sizeMax;
                float sizeTo = config.sizeMin;
                p.size = Lerp(sizeFrom, sizeTo, p.t);
            }
        }


        void Emitter::Draw() const {
            if (!drawFunc) {
                return;
            }

            for (int i = 0; i < static_cast<int>(particles.size()); ++i) {
                const Particle& p = particles[i];
                if (!p.alive) {
                    continue;
                }
                drawFunc(p);
            }
        }


        Emitter* ParticleSystem::CreateEmitter(const EmitterConfig& cfg, DrawFunc draw, SpawnFunc spawn) {
            Emitter* e = new Emitter(cfg, draw, spawn);
            emitters.push_back(e);
            return e;
        }

        void ParticleSystem::DestroyEmitter(Emitter* emitter) {
            if (emitter == nullptr) {
                return;
            }
            for (int i = 0; i < static_cast<int>(emitters.size()); ++i) {
                if (emitters[i] == emitter) {
                    emitters.erase(emitters.begin() + i);
                    break;
                }
            }

            for (auto it = effectEmitters.begin(); it != effectEmitters.end(); ) {
                if (it->second == emitter) {
                    it = effectEmitters.erase(it);
                } else {
                    ++it;
                }
            }
            delete emitter;
        }

        void ParticleSystem::RegisterEffect(const std::string& name, const EffectPrototype& proto) {
            effectTable[name] = proto;
        }

        Emitter* ParticleSystem::GetEffectEmitter(const std::string& name) {

            auto itEmitter = effectEmitters.find(name);
            if (itEmitter != effectEmitters.end()) {
                return itEmitter->second;
            }

            // 如果没有则用模板新建一个
            auto itProto = effectTable.find(name);
            if (itProto == effectTable.end()) {
                return nullptr;
            }

            const EffectPrototype& proto = itProto->second;
            Emitter* e = CreateEmitter(proto.config, proto.drawFunc, proto.spawnFunc);
            effectEmitters[name] = e;
            return e;
        }

        void ParticleSystem::PlayOneShot(const std::string& name, const Vector2& pos, int burstCount) {
            Emitter* e = GetEffectEmitter(name);
            if (e == nullptr) {
                return;
            }
            EmitterConfig& cfg = e->GetConfig();
            cfg.originPosition = pos;

            int count = burstCount;
            if (count <= 0) {
                count = cfg.burstCount;
            }
            if (count <= 0) {
                count = 1;
            }
            e->EmitBurst(count);
        }

        void ParticleSystem::Update(float dt) {
            for (int i = 0; i < static_cast<int>(emitters.size()); ++i) {
                if (emitters[i] != nullptr) {
                    emitters[i]->Update(dt);
                }
            }
        }

        void ParticleSystem::Draw() const {
            for (int i = 0; i < static_cast<int>(emitters.size()); ++i) {
                if (emitters[i] != nullptr) {
                    emitters[i]->Draw();
                }
            }
        }

        // ===== PRESET =====
        namespace PRESET {

            using namespace HIKARI::RENDERER;

#pragma region $DrawFunc$
            // 円形パーティクル
            DrawFunc MakeCircleDrawer(CameraMode camMode) {
                return [camMode](const Particle& p) {
                    HIKARI::Transform2D t = p.transform;
                    float r = p.size * 0.5f;

                    DrawEllipse(
                        t,
                        r, r,
                        FillMode::Fill,
                        camMode,
                        p.color
                    );
                    };
            }

            // 矩形パーティクル（枠線オプションあり）
            DrawFunc MakeBoxDrawer(bool outline, CameraMode camMode) {
                return [outline, camMode](const Particle& p) {
                    HIKARI::Transform2D t = p.transform;
                    float w = p.size;
                    float h = p.size;

                    if (!outline) {
                        DrawBox(
                            t,
                            w, h,
                            FillMode::Fill,
                            camMode,
                            p.color
                        );
                    } else {
                        // 塗りつぶし
                        DrawBox(
                            t,
                            w, h,
                            FillMode::Fill,
                            camMode,
                            p.color
                        );
                        // 枠線
                        unsigned int border = 0x000000FF;
                        DrawBox(
                            t,
                            w, h,
                            FillMode::Wireframe,
                            camMode,
                            border
                        );
                    }
                    };
            }

            // === 三角形パーティクル ===
            DrawFunc MakeTriangleDrawer(bool outline, CameraMode camMode) {
                return [outline, camMode](const Particle& p) {
                    HIKARI::Transform2D t = p.transform;

                    float s = p.size;

                    Vector2 p0{ 0.0f, -s * 0.5f };
                    Vector2 p1{ -s * 0.5f, s * 0.5f };
                    Vector2 p2{ s * 0.5f, s * 0.5f };

                    if (!outline) {
                        DrawTriangle(
                            t,
                            p0, p1, p2,
                            FillMode::Fill,
                            camMode,
                            p.color
                        );
                    } else {
                        // 塗りつぶし
                        DrawTriangle(
                            t,
                            p0, p1, p2,
                            FillMode::Fill,
                            camMode,
                            p.color
                        );
                        // 枠線
                        unsigned int border = 0x000000FF;
                        DrawTriangle(
                            t,
                            p0, p1, p2,
                            FillMode::Wireframe,
                            camMode,
                            border
                        );
                    }
                    };
            }


            // === 1枚のテクスチャ全体をパーティクルとして使う ===
            DrawFunc MakeSpriteDrawer(
                const std::string& textureName,
                float width,
                float height,
                CameraMode camMode
            ) {
                return [textureName, width, height, camMode](const Particle& p) {
                    HIKARI::Transform2D t = p.transform;

                    DrawSprite(
                        textureName,
                        t,
                        width,
                        height,
                        camMode,
                        p.color
                    );
                    };
            }

            // === テクスチャアニメーションのパーティクル ===
            DrawFunc MakeSpriteAnimDrawer(
                const std::string& textureName,
                int frameWidth,
                int frameHeight,
                int columns,
                int rows,
                float fps,
                CameraMode camMode
            ) {
                int totalFrames = columns * rows;
                if (totalFrames <= 0) {
                    totalFrames = 1;
                }

                return [=](const Particle& p) {
                    HIKARI::Transform2D t = p.transform;

                    // 根据年龄和 fps 算当前帧
                    float frameFloat = p.age * fps;
                    int frameIndex = static_cast<int>(frameFloat) % totalFrames;
                    if (frameIndex < 0) {
                        frameIndex += totalFrames;
                    }

                    int col = frameIndex % columns;
                    int row = frameIndex / columns;

                    int srcX = col * frameWidth;
                    int srcY = row * frameHeight;

                    float dstW = static_cast<float>(frameWidth) * p.transform.scale.x;
                    float dstH = static_cast<float>(frameHeight) * p.transform.scale.y;

                    DrawSpriteRect(
                        textureName,
                        srcX, srcY, frameWidth, frameHeight,
                        t,
                        dstW, dstH,
                        camMode,
                        p.color
                    );
                    };
            }


            // === 速度方向に伸びる矩形パーティクル===
            DrawFunc MakeVelocityStretchBoxDrawer(CameraMode camMode) {
                return [camMode](const Particle& p) {
                    HIKARI::Transform2D t = p.transform;

                    float vx = p.velocity.x;
                    float vy = p.velocity.y;
                    float speedSq = vx * vx + vy * vy;

                    if (speedSq < 1e-4f) {
                        float s = p.size;
                        DrawBox(
                            t,
                            s, s,
                            FillMode::Fill,
                            camMode,
                            p.color
                        );
                        return;
                    }

                    float speed = std::sqrt(speedSq);

                    float thickness = p.size * 0.4f;
                    float length = p.size + speed * 0.03f;

                    float angle = std::atan2(vy, vx);
                    t.rotation = angle;

                    t.pivotPx = Vector2{ length * 0.5f, thickness * 0.5f };

                    DrawBox(
                        t,
                        length,
                        thickness,
                        FillMode::Fill,
                        camMode,
                        p.color
                    );
                    };
            }

            // === 輪郭だけ描くリングパーティクル===
            DrawFunc MakeRingDrawer(CameraMode camMode) {
                return [camMode](const Particle& p) {
                    HIKARI::Transform2D t = p.transform;

                    float rOuter = p.size * 0.5f;
                    float rInner = rOuter * 0.8f;

                    DrawEllipse(
                        t,
                        rOuter, rOuter,
                        FillMode::Wireframe,
                        camMode,
                        p.color
                    );

                    DrawEllipse(
                        t,
                        rInner, rInner,
                        FillMode::Wireframe,
                        camMode,
                        p.color
                    );
                    };
            }

            // === 速度ベースのラインパーティクル===
            DrawFunc MakeVelocityLineDrawer(CameraMode camMode) {
                return [camMode](const Particle& p) {
                    Vector2 pos = p.transform.position;

                    float vx = p.velocity.x;
                    float vy = p.velocity.y;
                    float lenSq = vx * vx + vy * vy;

                    if (lenSq <= 1e-4f) {
                        float half = p.size * 0.5f;
                        Vector2 p0{ pos.x - half, pos.y };
                        Vector2 p1{ pos.x + half, pos.y };
                        DrawLine(p0, p1, camMode, p.color);
                        return;
                    }

                    float len = std::sqrt(lenSq);
                    float tailLength = p.size * 0.5f + len * 0.03f;

                    float invLen = 1.0f / len;
                    Vector2 dir{ vx * invLen, vy * invLen };

                    Vector2 p0 = pos;
                    Vector2 p1{
                        pos.x + dir.x * tailLength,
                        pos.y + dir.y * tailLength
                    };

                    DrawLine(p0, p1, camMode, p.color);
                    };
            }

            // === 霧状パーティクル ===
            DrawFunc MakeFogDiscDrawer(CameraMode camMode) {
                return [camMode](const Particle& p) {
                    HIKARI::Transform2D t = p.transform;

                    float rBase = p.size * 0.5f;


                    float fade = 1.0f - Clamp01(p.t);

                    auto ScaleAlpha = [](unsigned int rgba, float factor) -> unsigned int {
                        factor = Clamp01(factor);
                        unsigned int r = (rgba >> 24) & 0xFF;
                        unsigned int g = (rgba >> 16) & 0xFF;
                        unsigned int b = (rgba >> 8) & 0xFF;
                        unsigned int a = (rgba) & 0xFF;
                        a = static_cast<unsigned int>(a * factor);
                        return (r << 24) | (g << 16) | (b << 8) | (a);
                        };

                    float rOuter = rBase * 1.4f;
                    float rMid = rBase * 1.0f;
                    float rInner = rBase * 0.6f;

                    unsigned int cOuter = ScaleAlpha(p.color, 0.25f * fade);
                    unsigned int cMid = ScaleAlpha(p.color, 0.5f * fade);
                    unsigned int cInner = ScaleAlpha(p.color, 0.9f * fade);

                    DrawEllipse(
                        t,
                        rOuter, rOuter,
                        FillMode::Fill,
                        camMode,
                        cOuter
                    );

                    DrawEllipse(
                        t,
                        rMid, rMid,
                        FillMode::Fill,
                        camMode,
                        cMid
                    );


                    DrawEllipse(
                        t,
                        rInner, rInner,
                        FillMode::Fill,
                        camMode,
                        cInner
                    );

                    };
            }

            // === 十字の星パーティクル===
            DrawFunc MakeCrossStarDrawer(bool withDiagonal, CameraMode camMode) {
                return [withDiagonal, camMode](const Particle& p) {
                    Vector2 center = p.transform.position;

                    const float pi = 3.14159265358979323846f;
                    float twinkle = 0.7f + 0.3f * std::sin(p.t * 4.0f * pi);

                    float halfLen = (p.size * 0.5f) * twinkle;

                    Vector2 hx0{ center.x - halfLen, center.y };
                    Vector2 hx1{ center.x + halfLen, center.y };
                    Vector2 vy0{ center.x, center.y - halfLen };
                    Vector2 vy1{ center.x, center.y + halfLen };

                    DrawLine(hx0, hx1, camMode, p.color);
                    DrawLine(vy0, vy1, camMode, p.color);

                    if (withDiagonal) {
                        float diagLen = halfLen * 0.7f;
                        Vector2 d1_0{ center.x - diagLen, center.y - diagLen };
                        Vector2 d1_1{ center.x + diagLen, center.y + diagLen };
                        Vector2 d2_0{ center.x - diagLen, center.y + diagLen };
                        Vector2 d2_1{ center.x + diagLen, center.y - diagLen };

                        DrawLine(d1_0, d1_1, camMode, p.color);
                        DrawLine(d2_0, d2_1, camMode, p.color);
                    }
                    };
            }

            // === 稲妻（雷）っぽいポリラインパーティクル ===
            DrawFunc MakeLightningBoltDrawer(int segments, float amplitude, CameraMode camMode) {
                if (segments < 2) {
                    segments = 2;
                }
                if (amplitude < 0.0f) {
                    amplitude = -amplitude;
                }

                const int kMaxSegments = 64;
                if (segments > kMaxSegments) {
                    segments = kMaxSegments;
                }

                return [segments, amplitude, camMode](const Particle& p) {
                    Vector2 start = p.transform.position;
                    float length = (p.user0 > 0.0f) ? p.user0 : p.size;
                    if (length <= 0.0f) {
                        return;
                    }

                    float thickness = (p.user1 > 0.0f) ? p.user1 : (p.size * 0.2f);
                    if (thickness < 1.0f) {
                        thickness = 1.0f;
                    }

                    float fade = 1.0f - Clamp01(p.t);

                    auto ScaleAlpha = [](unsigned int rgba, float factor) -> unsigned int {
                        factor = Clamp01(factor);
                        unsigned int r = (rgba >> 24) & 0xFF;
                        unsigned int g = (rgba >> 16) & 0xFF;
                        unsigned int b = (rgba >> 8) & 0xFF;
                        unsigned int a = (rgba) & 0xFF;
                        a = static_cast<unsigned int>(a * factor);
                        return (r << 24) | (g << 16) | (b << 8) | a;
                        };

                    Vector2 pts[kMaxSegments];

                    float phase = p.age * 25.0f; 

                    for (int i = 0; i < segments; ++i) {
                        float t = (segments > 1)
                            ? (static_cast<float>(i) / static_cast<float>(segments - 1))
                            : 0.0f;

                        float y = length * t;

                        float centerWeight = 1.0f - std::fabs(2.0f * t - 1.0f); 

                        float n = std::sin(t * 12.9898f + phase)
                            + std::sin(t * 78.233f + phase * 1.3f);
                        n *= 0.5f; 

                        float offsetX = n * amplitude * (0.3f + 0.7f * centerWeight);

                        pts[i] = Vector2{
                            start.x + offsetX,
                            start.y + y
                        };
                    }

                    unsigned int mainColor = ScaleAlpha(p.color, 0.9f * fade);
                    unsigned int glowColor = ScaleAlpha(p.color, 0.4f * fade);

                    for (int i = 0; i < segments - 1; ++i) {
                        DrawLine(pts[i], pts[i + 1], camMode, mainColor);
                    }

                    float halfThick = thickness * 0.5f;

                    for (int i = 0; i < segments - 1; ++i) {
                        Vector2 d{
                            pts[i + 1].x - pts[i].x,
                            pts[i + 1].y - pts[i].y
                        };
                        float lenSq = d.x * d.x + d.y * d.y;
                        if (lenSq <= 1e-4f) {
                            continue;
                        }
                        float invLen = 1.0f / std::sqrt(lenSq);
                        d.x *= invLen;
                        d.y *= invLen;

                        Vector2 nrm{ -d.y, d.x };
                        Vector2 offset0{ nrm.x * halfThick, nrm.y * halfThick };
                        Vector2 offset1{ -offset0.x, -offset0.y };

                        Vector2 a0{ pts[i].x + offset0.x, pts[i].y + offset0.y };
                        Vector2 b0{ pts[i + 1].x + offset0.x, pts[i + 1].y + offset0.y };
                        Vector2 a1{ pts[i].x + offset1.x, pts[i].y + offset1.y };
                        Vector2 b1{ pts[i + 1].x + offset1.x, pts[i + 1].y + offset1.y };

                        DrawLine(a0, b0, camMode, glowColor);
                        DrawLine(a1, b1, camMode, glowColor);
                    }
                    };
            }

            // === 速度ベースのラインパーティクル===
            DrawFunc MakeSpeedLineDrawer(CameraMode camMode) {
                return [camMode](const Particle& p) {
                    Vector2 pos = p.transform.position;

                    float vx = p.velocity.x;
                    float vy = p.velocity.y;
                    float lenSq = vx * vx + vy * vy;

                    if (lenSq <= 1e-4f) {
                        float half = p.size * 0.5f;
                        Vector2 p0{ pos.x - half, pos.y };
                        Vector2 p1{ pos.x + half, pos.y };
                        DrawLine(p0, p1, camMode, p.color);
                        return;
                    }

                    float speed = std::sqrt(lenSq);
                    float invLen = 1.0f / speed;
                    Vector2 dir{ vx * invLen, vy * invLen };

                    float baseLen = p.size + speed * 0.03f;

                    float life = 1.0f - Clamp01(p.t);
                    float lenScale = 0.3f + 0.7f * life;
                    float curLen = baseLen * lenScale;
                    float halfLen = curLen * 0.5f;

                    auto ScaleAlpha = [](unsigned int rgba, float factor) -> unsigned int {
                        factor = Clamp01(factor);
                        unsigned int r = (rgba >> 24) & 0xFF;
                        unsigned int g = (rgba >> 16) & 0xFF;
                        unsigned int b = (rgba >> 8) & 0xFF;
                        unsigned int a = (rgba) & 0xFF;
                        a = static_cast<unsigned int>(a * factor);
                        return (r << 24) | (g << 16) | (b << 8) | a;
                        };

                    unsigned int col = ScaleAlpha(p.color, life);

                    Vector2 p0{
                        pos.x - dir.x * halfLen,
                        pos.y - dir.y * halfLen
                    };
                    Vector2 p1{
                        pos.x + dir.x * halfLen,
                        pos.y + dir.y * halfLen
                    };

                    DrawLine(p0, p1, camMode, col);
                    };
            }

#pragma endregion

#pragma region $SpawnFunc$
            // 拡散：一点から四方へ飛び散る
            SpawnFunc MakeRadialBurstSpawn() {
                return [](const EmitterConfig& cfg, Particle& out, const Vector2& emitterPos) {
                    out.transform.position = emitterPos;

                    out.lifetime = RandomRange(cfg.lifeMin, cfg.lifeMax);
                    out.age = 0.0f;
                    out.t = 0.0f;

                    out.size = RandomRange(cfg.sizeMin, cfg.sizeMax);

                    const float pi = 3.14159265358979323846f;
                    float angle = RandomRange(0.0f, 2.0f * pi);
                    float speed = RandomRange(cfg.speedMin, cfg.speedMax);

                    out.velocity.x = std::cos(angle) * speed;
                    out.velocity.y = std::sin(angle) * speed;

                    out.acceleration = Vector2{ 0.0f, 0.0f };
                    out.color = cfg.startColor;
                    out.alive = true;
                    };
            }

            // 集中噴射
            SpawnFunc MakeConeStreamSpawn() {
                return [](const EmitterConfig& cfg, Particle& out, const Vector2& emitterPos) {
                    out.transform.position = emitterPos;

                    out.lifetime = RandomRange(cfg.lifeMin, cfg.lifeMax);
                    out.age = 0.0f;
                    out.t = 0.0f;

                    out.size = RandomRange(cfg.sizeMin, cfg.sizeMax);

                    Vector2 dir = Normalize(cfg.baseDirection);
                    const float pi = 3.14159265358979323846f;
                    float baseAngle = std::atan2(dir.y, dir.x);
                    float halfDeg = cfg.spreadDeg * 0.5f;
                    float offsetDeg = RandomRange(-halfDeg, halfDeg);
                    float offsetRad = offsetDeg * (pi / 180.0f);
                    float angle = baseAngle + offsetRad;

                    float speed = RandomRange(cfg.speedMin, cfg.speedMax);
                    out.velocity.x = std::cos(angle) * speed;
                    out.velocity.y = std::sin(angle) * speed;

                    out.acceleration = Vector2{ 0.0f, 0.0f };
                    out.color = cfg.startColor;
                    out.alive = true;
                    };
            }

            // 軌跡（トレイル）
            SpawnFunc MakeTrailSpawn() {
                return [](const EmitterConfig& cfg, Particle& out, const Vector2& emitterPos) {
                    out.transform.position = emitterPos;

                    out.lifetime = RandomRange(cfg.lifeMin, cfg.lifeMax);
                    out.age = 0.0f;
                    out.t = 0.0f;

                    out.size = RandomRange(cfg.sizeMin, cfg.sizeMax);

                    float vx = RandomRange(cfg.velMin.x, cfg.velMax.x);
                    float vy = RandomRange(cfg.velMin.y, cfg.velMax.y);
                    out.velocity = Vector2{ vx, vy };

                    out.acceleration = Vector2{ 0.0f, 0.0f };
                    out.color = cfg.startColor;
                    out.alive = true;
                    };
            }

            // 下雪発射：エリア上端から生成して下に落とす
            SpawnFunc MakeSnowfallSpawn() {
                return [](const EmitterConfig& cfg, Particle& out, const Vector2& emitterPos) {

                    float x = RandomRange(
                        emitterPos.x - cfg.areaHalfSize.x,
                        emitterPos.x + cfg.areaHalfSize.x
                    );
                    float yTop = emitterPos.y - cfg.areaHalfSize.y;
                    float y = yTop - 10.0f; // 少し上にずらす

                    out.transform.position = Vector2{ x, y };

                    out.lifetime = RandomRange(cfg.lifeMin, cfg.lifeMax);
                    out.age = 0.0f;
                    out.t = 0.0f;

                    // 雪の粒の大きさ
                    out.size = RandomRange(cfg.sizeMin, cfg.sizeMax);

                    // 速度
                    float vy = RandomRange(cfg.speedMin, cfg.speedMax);
                    float vx = RandomRange(-30.0f, 30.0f);
                    out.velocity = Vector2{ vx, vy };

                    // 空気抵抗
                    out.acceleration = Vector2{ 0.0f, 0.0f };

                    out.color = cfg.startColor;
                    out.alive = true;
                    };
            }
            // 円環状に発生させる
            SpawnFunc MakeShockwaveRingSpawn() {
                return [](const EmitterConfig& cfg, Particle& out, const Vector2& emitterPos) {
                    const float pi = 3.14159265358979323846f;

                    static int sIndex = 0;
                    int segments = (cfg.ringSegments > 0) ? cfg.ringSegments : 32;
                    int idx = sIndex++;
                    float angle = (2.0f * pi * static_cast<float>(idx % segments)) / static_cast<float>(segments);

                    Vector2 dir{ std::cos(angle), std::sin(angle) };

                    float radius = (cfg.ringRadius > 0.0f)
                        ? cfg.ringRadius
                        : (cfg.sizeMax * 2.0f);

                    out.transform.position.x = emitterPos.x + dir.x * radius;
                    out.transform.position.y = emitterPos.y + dir.y * radius;

                    out.lifetime = RandomRange(cfg.lifeMin, cfg.lifeMax);
                    out.age = 0.0f;
                    out.t = 0.0f;

                    out.size = RandomRange(cfg.sizeMin, cfg.sizeMax);

                    float speed = RandomRange(cfg.speedMin, cfg.speedMax);
                    out.velocity.x = dir.x * speed;
                    out.velocity.y = dir.y * speed;
                    out.transform.rotation = RandomRange(0.0f, 2.0f * pi);

                    out.angularVelocity = RandomRange(-4.0f, 4.0f);
                    out.acceleration = Vector2{ 0.0f, 0.0f };
                    out.color = cfg.startColor;
                    out.alive = true;
                    };
            }
            // エリア内をふわふわ漂う
            SpawnFunc MakeAreaFogSpawn() {
                return [](const EmitterConfig& cfg, Particle& out, const Vector2& emitterPos) {
                    // エリア内のランダムな位置で生成する
                    float x = RandomRange(
                        emitterPos.x - cfg.areaHalfSize.x,
                        emitterPos.x + cfg.areaHalfSize.x
                    );
                    float y = RandomRange(
                        emitterPos.y - cfg.areaHalfSize.y,
                        emitterPos.y + cfg.areaHalfSize.y
                    );
                    out.transform.position = Vector2{ x, y };

                    out.lifetime = RandomRange(cfg.lifeMin, cfg.lifeMax);
                    out.age = 0.0f;
                    out.t = 0.0f;

                    out.size = RandomRange(cfg.sizeMin, cfg.sizeMax);

                    Vector2 dir = Normalize(Vector2{
                        RandomRange(-1.0f, 1.0f),
                        RandomRange(-1.0f, 1.0f)
                        });
                    float speed = RandomRange(cfg.speedMin, cfg.speedMax);
                    out.velocity.x = dir.x * speed;
                    out.velocity.y = dir.y * speed;

                    out.acceleration = Vector2{ 0.0f, 0.0f };
                    out.color = cfg.startColor;
                    out.alive = true;
                    };
            }
            // 噴水
            SpawnFunc MakeFountainSpawn() {
                return MakeConeStreamSpawn();
            }
            // 周囲を回るように発生させる
            SpawnFunc MakeOrbitSpawn() {
                return [](const EmitterConfig& cfg, Particle& out, const Vector2& emitterPos) {
                    const float pi = 3.14159265358979323846f;

                    float angle = RandomRange(0.0f, 2.0f * pi);
                    float radius = cfg.ringRadius > 0.0f ? cfg.ringRadius : 32.0f;

                    Vector2 dir{ std::cos(angle), std::sin(angle) };

                    out.transform.position.x = emitterPos.x + dir.x * radius;
                    out.transform.position.y = emitterPos.y + dir.y * radius;

                    out.lifetime = RandomRange(cfg.lifeMin, cfg.lifeMax);
                    out.age = 0.0f;
                    out.t = 0.0f;

                    out.size = RandomRange(cfg.sizeMin, cfg.sizeMax);

                    Vector2 tangent{ -dir.y, dir.x };
                    float tangSpeed = RandomRange(cfg.speedMin, cfg.speedMax);
                    out.velocity.x = tangent.x * tangSpeed;
                    out.velocity.y = tangent.y * tangSpeed;
                    out.angularVelocity = RandomRange(-8.0f, 8.0f);
                    out.acceleration = Vector2{ 0.0f, 0.0f };
                    out.color = cfg.startColor;
                    out.alive = true;
                    };
            }
            // 吸い寄せ（ホーミング）
            SpawnFunc MakeHomingSpawn() {
                return [](const EmitterConfig& cfg, Particle& out, const Vector2& emitterPos) {
                    float x = RandomRange(
                        emitterPos.x - cfg.areaHalfSize.x,
                        emitterPos.x + cfg.areaHalfSize.x
                    );
                    float y = RandomRange(
                        emitterPos.y - cfg.areaHalfSize.y,
                        emitterPos.y + cfg.areaHalfSize.y
                    );
                    out.transform.position = Vector2{ x, y };

                    out.lifetime = RandomRange(cfg.lifeMin, cfg.lifeMax);
                    out.age = 0.0f;
                    out.t = 0.0f;

                    out.size = RandomRange(cfg.sizeMin, cfg.sizeMax);

                    Vector2 dir = Normalize(Vector2{
                        RandomRange(-1.0f, 1.0f),
                        RandomRange(-1.0f, 1.0f)
                        });
                    float speed = RandomRange(cfg.speedMin, cfg.speedMax);
                    out.velocity.x = dir.x * speed;
                    out.velocity.y = dir.y * speed;

                    out.acceleration = Vector2{ 0.0f, 0.0f };
                    out.color = cfg.startColor;
                    out.alive = true;
                    };
            }

            // 雷撃（上から下にビカッと落ちる）
            SpawnFunc MakeLightningStrikeSpawn() {
                return [](const EmitterConfig& cfg, Particle& out, const Vector2& emitterPos) {
                    out.transform.position = emitterPos;
                    out.transform.rotation = 0.0f;
                    out.transform.scale = Vector2{ 1.0f, 1.0f };
                    out.transform.pivotPx = Vector2{ 0.0f, 0.0f };

                    out.lifetime = RandomRange(cfg.lifeMin, cfg.lifeMax);
                    out.age = 0.0f;
                    out.t = 0.0f;

                    float boltLen = RandomRange(cfg.speedMin, cfg.speedMax);
                    if (boltLen < 0.0f) {
                        boltLen = -boltLen;
                    }

                    float thickness = RandomRange(cfg.sizeMin, cfg.sizeMax);
                    if (thickness < 1.0f) {
                        thickness = 1.0f;
                    }

                    // user パラメータへ格納
                    out.user0 = boltLen; 
                    out.user1 = thickness;  

                    out.size = thickness;

                    out.velocity = Vector2{ 0.0f, 0.0f };
                    out.acceleration = Vector2{ 0.0f, 0.0f };
                    out.angularVelocity = 0.0f;

                    out.color = cfg.startColor;
                    out.alive = true;
                    };
            }

            // 画面全体スピードライン用Spawn
            SpawnFunc MakeScreenSpeedLineSpawn() {
                return [](const EmitterConfig& cfg, Particle& out, const Vector2& emitterPos) {

                    float x = RandomRange(
                        emitterPos.x - cfg.areaHalfSize.x,
                        emitterPos.x + cfg.areaHalfSize.x
                    );
                    float y = RandomRange(
                        emitterPos.y - cfg.areaHalfSize.y,
                        emitterPos.y + cfg.areaHalfSize.y
                    );
                    out.transform.position = Vector2{ x, y };
                    out.transform.rotation = 0.0f;
                    out.transform.scale = Vector2{ 1.0f, 1.0f };
                    out.transform.pivotPx = Vector2{ 0.0f, 0.0f };


                    out.lifetime = RandomRange(cfg.lifeMin, cfg.lifeMax);
                    out.age = 0.0f;
                    out.t = 0.0f;

                    out.size = RandomRange(cfg.sizeMin, cfg.sizeMax);

                    Vector2 dir = cfg.baseDirection;
                    float lenSq = dir.x * dir.x + dir.y * dir.y;
                    if (lenSq <= 1e-4f) {

                        dir = Vector2{ 1.0f, 0.0f };
                        lenSq = 1.0f;
                    }
                    float invLen = 1.0f / std::sqrt(lenSq);
                    dir.x *= invLen;
                    dir.y *= invLen;

                    const float pi = 3.14159265358979323846f;
                    float baseAngle = std::atan2(dir.y, dir.x);
                    float jitterDeg = 10.0f; // ±10°くらい
                    float offsetDeg = RandomRange(-jitterDeg, jitterDeg);
                    float offsetRad = offsetDeg * (pi / 180.0f);

                    float angle = baseAngle + offsetRad;

                    float speed = RandomRange(cfg.speedMin, cfg.speedMax);
                    out.velocity.x = std::cos(angle) * speed;
                    out.velocity.y = std::sin(angle) * speed;

                    out.acceleration = Vector2{ 0.0f, 0.0f };
                    out.angularVelocity = 0.0f;

                    out.color = cfg.startColor;
                    out.alive = true;
                    };
            }

#pragma endregion

#pragma region $EmitterConfig$
            // 設定：拡散系の爆発
            EmitterConfig MakeRadialBurstConfig(
                const Vector2& origin,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float speedMin,
                float speedMax,
                unsigned int startColor,
                unsigned int endColor
            ) {
                EmitterConfig cfg;
                cfg.originPosition = origin;
                cfg.maxParticles = maxParticles;

                cfg.emitRate = 0.0f;   // 連続発射しない
                cfg.burstCount = 32;   // デフォルトで32個発生させる

                cfg.lifeMin = lifeMin;
                cfg.lifeMax = lifeMax;
                cfg.sizeMin = 4.0f;
                cfg.sizeMax = 16.0f;

                cfg.speedMin = speedMin;
                cfg.speedMax = speedMax;

                cfg.startColor = startColor;
                cfg.endColor = endColor;

                cfg.physics.gravity = Vector2{ 0.0f, 0.0f };
                cfg.physics.damping = 0.0f;
                cfg.physics.useDamping = false;

                return cfg;
            }

            // 設定：集中噴射（火炎放射／スパークなど）
            EmitterConfig MakeConeStreamConfig(
                const Vector2& origin,
                int maxParticles,
                const Vector2& direction,
                float spreadDeg,
                float lifeMin,
                float lifeMax,
                float speedMin,
                float speedMax,
                unsigned int startColor,
                unsigned int endColor
            ) {
                EmitterConfig cfg;
                cfg.originPosition = origin;
                cfg.maxParticles = maxParticles;

                cfg.emitRate = 60.0f;
                cfg.burstCount = 1;

                cfg.lifeMin = lifeMin;
                cfg.lifeMax = lifeMax;
                cfg.sizeMin = 4.0f;
                cfg.sizeMax = 10.0f;

                cfg.speedMin = speedMin;
                cfg.speedMax = speedMax;

                cfg.startColor = startColor;
                cfg.endColor = endColor;

                cfg.baseDirection = direction;
                cfg.spreadDeg = spreadDeg;

                cfg.physics.gravity = Vector2{ 0.0f, 0.0f };
                cfg.physics.damping = 5.0f;
                cfg.physics.useDamping = true;

                return cfg;
            }

            // 設定：トレイル
            EmitterConfig MakeTrailConfig(
                const HIKARI::Transform2D* follow,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float emitRate,
                unsigned int startColor,
                unsigned int endColor
            ) {
                EmitterConfig cfg;
                cfg.originPosition = Vector2{ 0.0f, 0.0f };
                cfg.maxParticles = maxParticles;

                cfg.emitRate = emitRate;
                cfg.burstCount = 1;

                cfg.lifeMin = lifeMin;
                cfg.lifeMax = lifeMax;
                cfg.sizeMin = 4.0f;
                cfg.sizeMax = 12.0f;

                cfg.speedMin = 0.0f;
                cfg.speedMax = 0.0f;

                cfg.startColor = startColor;
                cfg.endColor = endColor;

                cfg.followTransform = follow;

                cfg.velMin = Vector2{ -20.0f, -20.0f };
                cfg.velMax = Vector2{ 20.0f, 20.0f };

                cfg.physics.gravity = Vector2{ 0.0f, 0.0f };
                cfg.physics.damping = 4.0f;
                cfg.physics.useDamping = true;

                return cfg;
            }

            // 設定：降雪
            EmitterConfig MakeSnowfallConfig(
                const Vector2& areaCenter,
                const Vector2& areaHalfSize,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float fallSpeedMin,
                float fallSpeedMax,
                unsigned int startColor,
                unsigned int endColor
            ) {
                EmitterConfig cfg;
                cfg.originPosition = areaCenter;
                cfg.areaHalfSize = areaHalfSize;
                cfg.maxParticles = maxParticles;

                cfg.emitRate = 120.0f;
                cfg.burstCount = 1;

                cfg.lifeMin = lifeMin;
                cfg.lifeMax = lifeMax;

                // 雪の粒のサイズ
                cfg.sizeMin = 4.0f;
                cfg.sizeMax = 10.0f;

                cfg.speedMin = fallSpeedMin;
                cfg.speedMax = fallSpeedMax;

                cfg.startColor = startColor;
                cfg.endColor = endColor;

                // 弱めの減衰
                cfg.physics.gravity = Vector2{ 0.0f, 0.0f };
                cfg.physics.damping = 0.0f;
                cfg.physics.useDamping = false;

                return cfg;
            }
            // 設定：ショックウェーブリング
            EmitterConfig MakeShockwaveRingConfig(
                const Vector2& origin,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float radius,
                float speed,
                unsigned int startColor,
                unsigned int endColor,
                int ringSegments
            ) {
                EmitterConfig cfg;
                cfg.originPosition = origin;
                cfg.maxParticles = maxParticles;

                cfg.emitRate = 0.0f;
                cfg.burstCount = ringSegments;

                cfg.lifeMin = lifeMin;
                cfg.lifeMax = lifeMax;

                cfg.sizeMin = 4.0f;
                cfg.sizeMax = 12.0f;

                cfg.speedMin = speed;
                cfg.speedMax = speed;

                cfg.startColor = startColor;
                cfg.endColor = endColor;

                cfg.ringRadius = radius;
                cfg.ringSegments = ringSegments;

                cfg.physics.gravity = Vector2{ 0.0f, 0.0f };
                cfg.physics.damping = 0.0f;
                cfg.physics.useDamping = false;

                return cfg;
            }
            // 設定：漂う霧
            EmitterConfig MakeAreaFogConfig(
                const Vector2& areaCenter,
                const Vector2& areaHalfSize,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float moveSpeedMin,
                float moveSpeedMax,
                unsigned int startColor,
                unsigned int endColor
            ) {
                EmitterConfig cfg;
                cfg.originPosition = areaCenter;
                cfg.areaHalfSize = areaHalfSize;
                cfg.maxParticles = maxParticles;

                cfg.emitRate = 80.0f;
                cfg.burstCount = 1;

                cfg.lifeMin = lifeMin;
                cfg.lifeMax = lifeMax;

                cfg.sizeMin = 3.0f;
                cfg.sizeMax = 8.0f;

                cfg.speedMin = moveSpeedMin;
                cfg.speedMax = moveSpeedMax;

                cfg.startColor = startColor;
                cfg.endColor = endColor;

                cfg.physics.gravity = Vector2{ 0.0f, 0.0f };
                cfg.physics.damping = 0.0f;
                cfg.physics.useDamping = false;

                return cfg;
            }
            // 設定：噴水
            EmitterConfig MakeFountainConfig(
                const Vector2& origin,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float speedMin,
                float speedMax,
                float spreadDeg,
                unsigned int startColor,
                unsigned int endColor,
                float gravityY
            ) {
                EmitterConfig cfg;
                cfg.originPosition = origin;
                cfg.maxParticles = maxParticles;

                cfg.emitRate = 80.0f;
                cfg.burstCount = 1;

                cfg.lifeMin = lifeMin;
                cfg.lifeMax = lifeMax;

                cfg.sizeMin = 4.0f;
                cfg.sizeMax = 10.0f;

                cfg.speedMin = speedMin;
                cfg.speedMax = speedMax;

                cfg.startColor = startColor;
                cfg.endColor = endColor;

                cfg.baseDirection = Vector2{ 0.0f, -1.0f };
                cfg.spreadDeg = spreadDeg;

                cfg.physics.gravity = Vector2{ 0.0f, gravityY };
                cfg.physics.damping = 0.5f;
                cfg.physics.useDamping = true;

                return cfg;
            }
            // 設定：オービット（光輪）
            EmitterConfig MakeOrbitConfig(
                const HIKARI::Transform2D* center,
                int maxParticles,
                float radius,
                float lifeMin,
                float lifeMax,
                float tangentialSpeedMin,
                float tangentialSpeedMax,
                unsigned int startColor,
                unsigned int endColor,
                float attractStrength
            ) {
                EmitterConfig cfg;
                cfg.originPosition = Vector2{ 0.0f, 0.0f };
                cfg.maxParticles = maxParticles;

                cfg.emitRate = 60.0f;
                cfg.burstCount = 1;

                cfg.lifeMin = lifeMin;
                cfg.lifeMax = lifeMax;

                cfg.sizeMin = 4.0f;
                cfg.sizeMax = 8.0f;

                cfg.speedMin = tangentialSpeedMin;
                cfg.speedMax = tangentialSpeedMax;

                cfg.startColor = startColor;
                cfg.endColor = endColor;

                cfg.followTransform = center;
                cfg.targetTransform = center;
                cfg.ringRadius = radius;
                cfg.ringSegments = 32;

                cfg.attractStrength = attractStrength;

                cfg.physics.gravity = Vector2{ 0.0f, 0.0f };
                cfg.physics.damping = 1.5f;
                cfg.physics.useDamping = true;

                return cfg;
            }
            // 設定：ホーミング（吸い寄せ）
            EmitterConfig MakeHomingConfig(
                const Vector2& spawnCenter,
                const Vector2& spawnHalfSize,
                const HIKARI::Transform2D* target,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float speedMin,
                float speedMax,
                unsigned int startColor,
                unsigned int endColor,
                float attractStrength
            ) {
                EmitterConfig cfg;
                cfg.originPosition = spawnCenter;
                cfg.areaHalfSize = spawnHalfSize;
                cfg.maxParticles = maxParticles;

                cfg.emitRate = 0.0f;
                cfg.burstCount = 16;

                cfg.lifeMin = lifeMin;
                cfg.lifeMax = lifeMax;

                cfg.sizeMin = 4.0f;
                cfg.sizeMax = 10.0f;

                cfg.speedMin = speedMin;
                cfg.speedMax = speedMax;

                cfg.startColor = startColor;
                cfg.endColor = endColor;

                cfg.targetTransform = target;
                cfg.attractStrength = attractStrength;

                cfg.physics.gravity = Vector2{ 0.0f, 0.0f };
                cfg.physics.damping = 1.0f;
                cfg.physics.useDamping = true;

                return cfg;
            }
            // 設定：円周上に配置して一斉に飛ばす（その2）
            EmitterConfig MakeBurstOnCircleConfig(
                const Vector2& origin,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float radius,
                float speed,
                unsigned int startColor,
                unsigned int endColor,
                int ringSegments
            ) {
                EmitterConfig cfg = MakeShockwaveRingConfig(
                    origin,
                    maxParticles,
                    lifeMin,
                    lifeMax,
                    radius,
                    speed,
                    startColor,
                    endColor,
                    ringSegments
                );
                return cfg;
            }

            // 設定：雷撃（上から下へ稲妻が落ちる）
            EmitterConfig MakeLightningStrikeConfig(
                const Vector2& origin,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float boltLengthMin,
                float boltLengthMax,
                float thicknessMin,
                float thicknessMax,
                unsigned int startColor,
                unsigned int endColor
            ) {
                EmitterConfig cfg;
                cfg.originPosition = origin;
                cfg.maxParticles = maxParticles;

                cfg.emitRate = 0.0f;
                cfg.burstCount = 1;

                cfg.lifeMin = lifeMin;
                cfg.lifeMax = lifeMax;

                cfg.sizeMin = thicknessMin;
                cfg.sizeMax = thicknessMax;

                cfg.speedMin = boltLengthMin;
                cfg.speedMax = boltLengthMax;

                cfg.startColor = startColor;
                cfg.endColor = endColor;

                cfg.physics.gravity = Vector2{ 0.0f, 0.0f };
                cfg.physics.damping = 0.0f;
                cfg.physics.useDamping = false;

                return cfg;
            }

            // 設定：スピードライン・トレイル
            EmitterConfig MakeSpeedLineTrailConfig(
                const HIKARI::Transform2D* follow,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float emitRate,
                float speedMin,
                float speedMax,
                float thicknessMin,
                float thicknessMax,
                unsigned int startColor,
                unsigned int endColor
            ) {
                EmitterConfig cfg;
                cfg.originPosition = Vector2{ 0.0f, 0.0f };
                cfg.maxParticles = maxParticles;

                cfg.emitRate = emitRate;
                cfg.burstCount = 1;

                cfg.lifeMin = lifeMin;
                cfg.lifeMax = lifeMax;
                cfg.sizeMin = thicknessMin;
                cfg.sizeMax = thicknessMax;
                cfg.speedMin = speedMin;
                cfg.speedMax = speedMax;

                cfg.startColor = startColor;
                cfg.endColor = endColor;

                cfg.followTransform = follow;

                float vMax = speedMax;
                cfg.velMin = Vector2{ -vMax, -vMax };
                cfg.velMax = Vector2{ vMax,  vMax };
                cfg.physics.gravity = Vector2{ 0.0f, 0.0f };
                cfg.physics.damping = 4.0f;
                cfg.physics.useDamping = true;

                return cfg;
            }

            // 設定：画面全体のスピードライン
            EmitterConfig MakeScreenSpeedLineConfig(
                const Vector2& origin,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float emitRate,
                const Vector2& dir,
                const Vector2& areaHalfSize,
                float speedMin,
                float speedMax,
                float thicknessMin,
                float thicknessMax,
                unsigned int startColor,
                unsigned int endColor
            ) {
                EmitterConfig cfg;
                cfg.originPosition = origin;
                cfg.maxParticles = maxParticles;

                cfg.emitRate = emitRate;
                cfg.burstCount = 1;

                cfg.lifeMin = lifeMin;
                cfg.lifeMax = lifeMax;

                cfg.sizeMin = thicknessMin;
                cfg.sizeMax = thicknessMax;


                cfg.speedMin = speedMin;
                cfg.speedMax = speedMax;

                cfg.startColor = startColor;
                cfg.endColor = endColor;

                cfg.areaHalfSize = areaHalfSize;

                cfg.baseDirection = dir;
                cfg.velMin = Vector2{ -speedMax, -speedMax };
                cfg.velMax = Vector2{ speedMax,  speedMax };

                cfg.physics.gravity = Vector2{ 0.0f, 0.0f };
                cfg.physics.damping = 2.0f;
                cfg.physics.useDamping = true;

                cfg.followTransform = nullptr;
                cfg.targetTransform = nullptr;

                return cfg;
            }

#pragma endregion

        } // namespace PRESET

    } // namespace PARTICLE
} // namespace HIKARI
